#include "win_services.hpp"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "../core/json.hpp"
#include "../core/natives/player.hpp"
#include "../core/natives/service_flags.hpp"
#include "../core/output_files.hpp"
#include "discord_ipc.hpp"
#include "midi.hpp"
#include "remote_server.hpp"
#include "toast_hold.hpp"
#include "tray.hpp"
#include "win_common.hpp"

namespace ampify::win {

namespace {

constexpr auto kRemoteAnswerTimeout = std::chrono::seconds(2);
constexpr auto kNotificationRulePeriod = std::chrono::seconds(2);

int IntSetting(Host& host, const std::string& key, int fallback, int low, int high) {
    std::optional<std::string> raw = host.ReadSetting(key);
    if (!raw || raw->empty()) return fallback;
    char* end = nullptr;
    long value = std::strtol(raw->c_str(), &end, 10);
    if (!end || *end != '\0' || value < low || value > high) return fallback;
    return static_cast<int>(value);
}

}  // namespace

Services::Services(Host& host, std::string process, std::filesystem::path kit, Journal& journal)
    : m_host(host), m_process(std::move(process)), m_kit(std::move(kit)), m_journal(journal) {}

Services::~Services() {
    // The threads go first: none of them may call back into a half-destroyed object.
    if (m_remote) m_remote->Stop();
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        for (auto& task : m_remoteQueue) task->done.set_value(std::nullopt);
        m_remoteQueue.clear();
    }
    if (m_midi) m_midi->Close();
    if (m_tray) m_tray->Stop();
    m_discord.reset();
}

void Services::SetEventSink(EventSink sink) { m_sink = std::move(sink); }

void* Services::Query(std::string_view name) {
    if (name == DiscordHost::kCapabilityName) return static_cast<DiscordHost*>(this);
    if (name == TrayHost::kCapabilityName) return static_cast<TrayHost*>(this);
    if (name == MidiHost::kCapabilityName) return static_cast<MidiHost*>(this);
    if (name == RemoteHost::kCapabilityName) return static_cast<RemoteHost*>(this);
    if (name == OutputFilesHost::kCapabilityName) return static_cast<OutputFilesHost*>(this);
    if (name == LibraryHost::kCapabilityName) return static_cast<LibraryHost*>(this);
    return nullptr;
}

void Services::EnsureStarted() {
    if (m_started) return;
    m_started = true;
    if (m_process == "ui" && ServiceEnabled(m_host, "tray.enabled")) StartTray();
    if (m_process == "engine" && ServiceEnabled(m_host, "remote.enabled")) StartRemote();
    if (m_process == "engine" && ServiceEnabled(m_host, "midi.enabled")) StartMidi();
}

// The song notifications come from this process (toast_hold.hpp): it follows the card's rule. settings.txt is read
// again when it changes (looked at every 2 s): the host's copy of the settings is the one of the start.
void Services::FollowNotificationRule() {
    auto now = std::chrono::steady_clock::now();
    if (now < m_nextNotificationRule) return;
    m_nextNotificationRule = now + kNotificationRulePeriod;
    std::error_code error;
    auto written = std::filesystem::last_write_time(m_kit / "settings.txt", error);
    if (error || (m_settingsStamp && *m_settingsStamp == written)) return;
    m_settingsStamp = written;
    Settings settings;
    settings.Load((m_kit / "settings.txt").string());
    SetToastRule(uirender::ReadAppleTweaks(settings).notifications, m_journal);
}

void Services::Emit(const std::string& entry, const std::string& payload) {
    if (m_sink) m_sink(entry, payload);
}

// ---- Notification area icon ----------------------------------------------------------------------------------

void Services::StartTray() {
    auto tray = std::make_unique<TrayIcon>();
    std::string error;
    bool started = tray->Start(FromUtf8(i18n::Text("tray.tooltip")),
                               [this](UINT command) {
                                   std::lock_guard<std::mutex> guard(m_queueMutex);
                                   m_trayQueue.push_back(command);
                               },
                               &error);
    if (!started) {
        m_journal.Write(i18n::Text("tray.failed", {{"error", error}}));
        return;
    }
    m_tray = std::move(tray);
    RebuildTrayMenu();
    m_journal.Write(i18n::Text("tray.started"));
}

void Services::RebuildTrayMenu() {
    if (!m_tray) return;
    std::vector<TrayEntry> entries;
    entries.push_back({kTrayPrevious, FromUtf8(i18n::Text("tray.previous"))});
    entries.push_back({kTrayNext, FromUtf8(i18n::Text("tray.next"))});
    m_trayTargets.clear();
    unsigned next = kTrayFirstElementItem;
    for (const auto& [elementId, items] : m_trayItems) {
        if (items.empty()) continue;
        TrayEntry separator;
        separator.separator = true;
        entries.push_back(separator);
        for (const TrayItem& item : items) {
            TrayEntry entry;
            entry.separator = item.separator;
            if (!item.separator) {
                entry.command = next++;
                entry.label = FromUtf8(item.label);
                entry.checked = item.checked;
                entry.enabled = item.enabled;
                m_trayTargets[entry.command] = {elementId, item.id};
            }
            entries.push_back(entry);
        }
    }
    TrayEntry separator;
    separator.separator = true;
    entries.push_back(separator);
    entries.push_back({kTrayOpenKit, FromUtf8(i18n::Text("tray.openKit"))});
    m_tray->SetMenu(std::move(entries));
}

bool Services::SetTrayMenu(const std::string& elementId, const std::vector<TrayItem>& items) {
    EnsureStarted();
    if (!m_tray) return false;
    if (items.empty()) {
        m_trayItems.erase(elementId);
    } else {
        m_trayItems[elementId] = items;
    }
    RebuildTrayMenu();
    return true;
}

void Services::RemoveTrayMenu(const std::string& elementId) {
    if (m_trayItems.erase(elementId)) RebuildTrayMenu();
}

void Services::InjectTrayCommand(unsigned command) {
    std::lock_guard<std::mutex> guard(m_queueMutex);
    m_trayQueue.push_back(command);
}

bool Services::trayRunning() const { return m_tray && m_tray->running(); }

// ---- Discord ---------------------------------------------------------------------------------------------------

bool Services::SetActivity(const std::string& elementId, const std::string& clientId,
                           const discord::Activity& activity) {
    EnsureStarted();
    if (!m_discord) m_discord = std::make_unique<DiscordIpc>(m_journal);
    m_discordOwner = elementId;
    m_discord->Set(clientId, activity);
    return true;
}

void Services::ClearActivity(const std::string& elementId) {
    if (!m_discord || m_discordOwner != elementId) return;
    m_discord->Set({}, std::nullopt);
    m_discordOwner.clear();
}

// ---- MIDI ------------------------------------------------------------------------------------------------------

void Services::StartMidi() {
    std::string error;
    if (std::optional<std::string> map = m_host.ReadSetting("midi.map"); map && !map->empty()) {
        if (!m_midiMap.Parse(*map, &error)) m_journal.Write(i18n::Text("midi.mapRefused", {{"error", error}}));
    }
    std::vector<std::string> devices = MidiInput::Devices();
    if (devices.empty()) {
        m_journal.Write(i18n::Text("midi.none"));
        return;
    }
    auto input = std::make_unique<MidiInput>();
    unsigned index = static_cast<unsigned>(IntSetting(m_host, "midi.device", 0, 0, 255));
    bool opened = input->Open(index,
                              [this](uint32_t packed) {
                                  std::lock_guard<std::mutex> guard(m_queueMutex);
                                  if (m_midiQueue.size() < 1024) m_midiQueue.push_back(packed);  // drop a flood
                              },
                              &error);
    if (!opened) {
        m_journal.Write(i18n::Text("midi.failed", {{"error", error}}));
        return;
    }
    m_journal.Write(i18n::Text("midi.opened", {{"name", input->name()}}));
    m_midi = std::move(input);
}

bool Services::SubscribeMidi(const std::string& elementId) {
    EnsureStarted();
    if (std::find(m_midiSubscribers.begin(), m_midiSubscribers.end(), elementId) == m_midiSubscribers.end()) {
        m_midiSubscribers.push_back(elementId);
    }
    return midiOpen();
}

void Services::UnsubscribeMidi(const std::string& elementId) {
    m_midiSubscribers.erase(std::remove(m_midiSubscribers.begin(), m_midiSubscribers.end(), elementId),
                            m_midiSubscribers.end());
}

std::vector<std::string> Services::MidiDevices() { return MidiInput::Devices(); }

void Services::InjectMidi(uint32_t packed) {
    std::lock_guard<std::mutex> guard(m_queueMutex);
    m_midiQueue.push_back(packed);
}

bool Services::midiOpen() const { return m_midi && m_midi->open(); }

// ---- Remote control --------------------------------------------------------------------------------------------

void Services::StartRemote() {
    RemoteServer::Config config;
    config.port = static_cast<uint16_t>(IntSetting(m_host, "remote.port", remote::kDefaultPort, 0, 65535));
    config.token = m_host.ReadSetting("remote.token").value_or("");
    if (!remote::IsValidToken(config.token)) {
        // Created once, on the first start, and kept in settings.txt: ampify config remote.token shows it.
        config.token = remote::NewToken();
        m_host.WriteSetting("remote.token", config.token);
        m_journal.Write(i18n::Text("remote.tokenCreated"));
    }
    RefreshStatus();
    auto server = std::make_unique<RemoteServer>();
    std::string error;
    if (!server->Start(config, *this, &error)) {
        m_journal.Write(i18n::Text("remote.failed", {{"error", error}}));
        return;
    }
    m_journal.Write(i18n::Text("remote.started", {{"port", std::to_string(server->port())}}));
    m_remote = std::move(server);
}

bool Services::PublishRemoteState(const std::string& elementId, const std::string& json) {
    EnsureStarted();
    if (!m_remote) return false;
    m_remoteStates[elementId] = json;
    RefreshStatus();
    return true;
}

void Services::RemoveRemoteState(const std::string& elementId) {
    if (m_remoteStates.erase(elementId)) RefreshStatus();
}

void Services::RefreshStatus() {
    Json status = natives::PlayerJson(m_host.Player());
    status.Set("gainDb", Json(m_remoteGainDb));
    Json extensions(Json::Object{});
    for (const auto& [elementId, json] : m_remoteStates) {
        if (std::optional<Json> value = Json::Parse(json)) extensions.Set(elementId, std::move(*value));
    }
    status.Set("extensions", std::move(extensions));
    std::lock_guard<std::mutex> guard(m_queueMutex);
    m_status = status.Dump();
}

std::string Services::StatusJson() {
    std::lock_guard<std::mutex> guard(m_queueMutex);
    return m_status;
}

uint16_t Services::remotePort() const { return m_remote ? m_remote->port() : 0; }

std::optional<double> Services::Queue(const std::string& action, double db) {
    auto task = std::make_shared<RemoteTask>();
    task->action = action;
    task->db = db;
    std::future<std::optional<double>> answer = task->done.get_future();
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        m_remoteQueue.push_back(task);
    }
    // The JavaScript thread runs the command; it may be busy (a network call), so the client does not wait forever.
    if (answer.wait_for(kRemoteAnswerTimeout) != std::future_status::ready) return std::nullopt;
    return answer.get();
}

bool Services::Command(remote::Action action) {
    return Queue(std::string(remote::ActionName(action)), 0).has_value();
}

std::optional<double> Services::SetGainDb(double db) { return Queue("gain", db); }

// ---- Output files ----------------------------------------------------------------------------------------------

bool Services::WriteOutputText(const std::string& elementId, const std::string& name, const std::string& text,
                               std::string* error) {
    return WriteOutputFile(m_kit / "output", elementId, name, text, error);
}

// Apple Music keeps its library under the Music known folder, which may be moved off %USERPROFILE%\Music.
std::filesystem::path Services::LibraryFile() {
    std::filesystem::path music = KnownFolder(FOLDERID_Music);
    if (music.empty()) return {};
    return music / L"Apple Music" / L"Apple Music Library.musiclibrary" / L"Library.musicdb";
}

// ---- The JavaScript thread's turn ------------------------------------------------------------------------------

void Services::Service() {
    EnsureStarted();
    if (m_process == "engine") FollowNotificationRule();

    std::vector<uint32_t> midiMessages;
    std::vector<unsigned> trayCommands;
    std::deque<std::shared_ptr<RemoteTask>> remoteTasks;
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        midiMessages.swap(m_midiQueue);
        trayCommands.swap(m_trayQueue);
        remoteTasks.swap(m_remoteQueue);
    }

    for (const std::shared_ptr<RemoteTask>& task : remoteTasks) {
        std::optional<double> result;
        if (task->action == "gain") {
            // The internal gain on the samples, under Ampify's ceiling. The volume slider never moves.
            m_host.SetGainDb("remote", task->db);
            m_remoteGainDb = task->db;
            result = task->db;
        } else {
            PlayerCommand command = task->action == "play"    ? PlayerCommand::Play
                                    : task->action == "pause" ? PlayerCommand::Pause
                                    : task->action == "next"  ? PlayerCommand::Next
                                                              : PlayerCommand::Previous;
            if (m_host.Control(command, 0)) result = 0.0;
        }
        task->done.set_value(result);
    }

    for (uint32_t packed : midiMessages) {
        std::optional<midi::Event> event = midi::Decode(packed);
        if (!event) continue;
        if (std::optional<midi::Decision> decision = m_midiMap.Handle(*event)) {
            switch (decision->action) {
                case midi::Action::Play: m_host.Control(PlayerCommand::Play, 0); break;
                case midi::Action::Pause: m_host.Control(PlayerCommand::Pause, 0); break;
                case midi::Action::Next: m_host.Control(PlayerCommand::Next, 0); break;
                case midi::Action::Previous: m_host.Control(PlayerCommand::Previous, 0); break;
                case midi::Action::Gain: m_host.SetGainDb("midi", decision->gainDb); break;
            }
        }
        if (!m_midiSubscribers.empty()) Emit("midi", midi::EventJson(*event));
    }

    for (unsigned command : trayCommands) {
        if (command == 0) {
            Emit("tray", R"({"click":true})");
        } else if (command == kTrayPrevious) {
            m_host.Control(PlayerCommand::Previous, 0);
        } else if (command == kTrayNext) {
            m_host.Control(PlayerCommand::Next, 0);
        } else if (command == kTrayOpenKit) {
            ShellExecuteW(nullptr, L"open", m_kit.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else if (auto found = m_trayTargets.find(command); found != m_trayTargets.end()) {
            Json payload(Json::Object{});
            payload.Set("to", Json(found->second.first));
            payload.Set("item", Json(static_cast<double>(found->second.second)));
            Emit("tray", payload.Dump());
        }
    }

    // The status of GET /status follows the player four times a second.
    if (m_remote && ++m_ticks % 5 == 0) RefreshStatus();
}

}  // namespace ampify::win
