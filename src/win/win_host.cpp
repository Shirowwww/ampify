#include "win_host.hpp"

#include <windows.h>
#include <appmodel.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "../core/apple_music.hpp"
#include "../core/audio/audio_engine.hpp"
#include "../core/gain_board.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_watchdog.hpp"
#include "audio_share.hpp"
#include "clipboard.hpp"
#include "kit_command.hpp"
#include "player_bridge.hpp"
#include "player_buttons.hpp"
#include "smtc_session.hpp"
#include "ui_thread.hpp"
#include "video_gif.hpp"
#include "wic_image.hpp"
#include "win_common.hpp"
#include "win_services.hpp"

namespace ampify {

namespace {

// Apple Music's web player script, read for its public token every few weeks, weighs about 3.3 MB.
constexpr size_t kMaxResponseBytes = size_t{8} << 20;
constexpr std::chrono::seconds kAnswerDeadline{15};       // reading the answer, after the headers came
constexpr auto kBalloonDuration = std::chrono::seconds(8);

std::wstring ToWide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// Returns the virtual key of a key name written by Ampify's normalizer (see src/core/prelude/keyboard.cpp), or 0.
UINT VirtualKeyOf(const std::string& name) {
    static const std::map<std::string, UINT> named = {
        {"Left", VK_LEFT}, {"Right", VK_RIGHT}, {"Up", VK_UP}, {"Down", VK_DOWN}, {"Space", VK_SPACE},
        {"Enter", VK_RETURN}, {"Escape", VK_ESCAPE}, {"Tab", VK_TAB}, {"Backspace", VK_BACK},
        {"Delete", VK_DELETE}, {"Home", VK_HOME}, {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT},
        {"MediaPlayPause", VK_MEDIA_PLAY_PAUSE}, {"MediaNextTrack", VK_MEDIA_NEXT_TRACK},
        {"MediaPreviousTrack", VK_MEDIA_PREV_TRACK}, {"MediaStop", VK_MEDIA_STOP},
    };
    if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0]))) {
        return static_cast<UINT>(std::toupper(static_cast<unsigned char>(name[0])));  // A-Z and 0-9 match their ASCII
    }
    if (name.size() >= 2 && name[0] == 'F') {
        int number = std::atoi(name.c_str() + 1);
        if (number >= 1 && number <= 24) return VK_F1 + static_cast<UINT>(number - 1);
    }
    auto found = named.find(name);
    return found == named.end() ? 0 : found->second;
}

// "Ctrl+Alt+Right" becomes the modifiers and the virtual key for RegisterHotKey. False when unknown.
bool ParseCombo(const std::string& combo, UINT& modifiers, UINT& key) {
    modifiers = MOD_NOREPEAT;
    std::string_view rest = combo;
    while (true) {
        size_t plus = rest.find('+');
        if (plus == std::string_view::npos) break;
        std::string part(rest.substr(0, plus));
        if (part == "Ctrl") modifiers |= MOD_CONTROL;
        else if (part == "Alt") modifiers |= MOD_ALT;
        else if (part == "Shift") modifiers |= MOD_SHIFT;
        else if (part == "Win") modifiers |= MOD_WIN;
        else return false;
        rest.remove_prefix(plus + 1);
    }
    key = VirtualKeyOf(std::string(rest));
    return key != 0;
}

}  // namespace

namespace win {

namespace {
std::atomic<ReadPlayerModesFn> g_readModes{nullptr};
std::atomic<PressPlayerButtonFn> g_pressButton{nullptr};
std::atomic<SetPlayerVolumeFn> g_setVolume{nullptr};
std::atomic<PlayerVolumeProblemFn> g_volumeProblem{nullptr};
}  // namespace

void InstallPlayerButtons(ReadPlayerModesFn read, PressPlayerButtonFn press) {
    g_readModes = read;
    g_pressButton = press;
}

void InstallPlayerVolume(SetPlayerVolumeFn setter, PlayerVolumeProblemFn problem) {
    g_setVolume = setter;
    g_volumeProblem = problem;
}

}  // namespace win

namespace {

constexpr unsigned kInterfaceTimeoutMs = 1500;
constexpr auto kButtonSettle = std::chrono::milliseconds(60);  // Apple redraws its button on its own turn
constexpr int kSettleChecks = 70;  // a press is read back for 4.2 s at most: Apple's first one took 2.8 s, the next 0.1 s
std::atomic<unsigned> g_repeatGlyphSeen{0};

// Apple's buttons as the interface thread reads them. The result is shared with the work, which may still run after
// the wait ended; nullopt when the support is absent or the interface did not answer in time.
std::optional<win::MainPlayerModes> ReadButtons() {
    win::ReadPlayerModesFn read = win::g_readModes.load();
    if (!read) return std::nullopt;
    auto result = std::make_shared<win::MainPlayerModes>();
    auto readOnUi = [read, result] {
        ampify::ActivityScope activity("player buttons read");
        *result = read();
    };
    if (!win::RunOnUiThreadAndWait(readOnUi, kInterfaceTimeoutMs)) return std::nullopt;
    return *result;
}

bool PressButton(bool shuffle) {
    win::PressPlayerButtonFn press = win::g_pressButton.load();
    if (!press) return false;
    auto pressed = std::make_shared<bool>(false);
    auto pressOnUi = [press, pressed, shuffle] {
        ampify::ActivityScope activity("player buttons press");
        *pressed = press(shuffle);
    };
    if (!win::RunOnUiThreadAndWait(pressOnUi, kInterfaceTimeoutMs)) {
        return false;
    }
    return *pressed;
}

}  // namespace

PlayerModes WinHost::Modes() {
    PlayerModes modes;
    std::optional<win::MainPlayerModes> read = ReadButtons();
    if (!read) return modes;
    if (read->shuffle >= 0) modes.shuffle = read->shuffle == 1;
    static constexpr const char* kRepeatNames[] = {"off", "all", "one"};
    if (read->repeat >= 0 && read->repeat <= 2) modes.repeat = kRepeatNames[read->repeat];
    static std::mutex detailLock;
    static std::string detailSeen;
    {
        std::lock_guard<std::mutex> guard(detailLock);
        if (!read->detail.empty() && read->detail != detailSeen) {
            detailSeen = read->detail;
            m_journal.Write("Player: the repeat button looks like " + detailSeen);
        }
    }
    if (read->repeatGlyph != g_repeatGlyphSeen.exchange(read->repeatGlyph)) {
        std::ostringstream line;
        line << "Player: the repeat button shows icon U+" << std::hex << std::uppercase << read->repeatGlyph << std::dec
             << " (repeat " << (modes.repeat.empty() ? "unknown" : modes.repeat) << ")";
        m_journal.Write(line.str());
    }
    return modes;
}

// Presses Apple's button until its state is the asked one: shuffle needs one press, repeat up to two (off, all, one).
// The state is read back after each press, for Apple redraws on its own turn; never more presses than the cycle holds.
bool WinHost::SetShuffle(bool on) {
    std::optional<bool> now = Modes().shuffle;
    if (!now) return false;
    if (*now == on) return true;
    if (!PressButton(true)) return false;
    for (int check = 0; check < kSettleChecks; check++) {
        std::this_thread::sleep_for(kButtonSettle);
        if (Modes().shuffle == on) return true;
    }
    return false;
}

bool WinHost::SetRepeat(const std::string& mode) {
    std::string now = Modes().repeat;
    for (int press = 0; !now.empty() && now != mode && press < 3; press++) {
        if (!PressButton(false)) return false;
        std::string before = now;
        for (int check = 0; check < kSettleChecks && now == before; check++) {
            std::this_thread::sleep_for(kButtonSettle);
            now = Modes().repeat;
        }
        if (now == before) return false;  // Apple's button did not react
    }
    return !now.empty() && now == mode;
}

// In the interface process the setter works on Apple's volume control, which only its own thread may touch; in the audio
// process it hooks the player, from any thread.
bool WinHost::SetVolume(double volume) {
    win::SetPlayerVolumeFn setter = win::g_setVolume.load();
    if (!setter) return false;
    if (m_process != "ui") return setter(volume);
    auto done = std::make_shared<bool>(false);
    auto setOnUi = [setter, volume, done] {
        ampify::ActivityScope activity("player volume");
        *done = setter(volume);
    };
    if (!win::RunOnUiThreadAndWait(setOnUi, kInterfaceTimeoutMs)) {
        m_journal.Write("Player: the volume was not set: the interface thread did not answer in time");
        return false;
    }
    if (!*done) {
        win::PlayerVolumeProblemFn problem = win::g_volumeProblem.load();
        std::string reason = problem ? problem() : std::string();
        if (!reason.empty()) m_journal.Write("Player: the volume was not set: " + reason);
    }
    return *done;
}

WinHost::WinHost(std::string process, std::filesystem::path kitDirectory, Settings& settings, Journal& journal)
    : m_process(std::move(process)), m_kit(std::move(kitDirectory)), m_settings(settings), m_journal(journal),
      m_player(std::make_unique<win::PlayerBridge>(m_process, m_settings, m_journal)),
      m_services(std::make_unique<win::Services>(*this, m_process, m_kit, m_journal)),
      m_kitCommands(std::make_unique<win::KitCommandRunner>(m_journal)) {}

void WinHost::SetEventSink(EventSink sink) { m_services->SetEventSink(std::move(sink)); }

void* WinHost::QueryCapability(std::string_view name) {
    if (void* found = Offer<HttpPostHost>(name, this)) return found;
    if (void* found = Offer<BrowserHost>(name, this)) return found;
    if (void* found = Offer<ImageHost>(name, this)) return found;
    if (void* found = Offer<ArtworkHost>(name, this)) return found;
    if (void* found = Offer<GifHost>(name, this)) return found;
    if (void* found = Offer<KeyUnbindHost>(name, this)) return found;
    if (void* found = Offer<ClipboardHost>(name, this)) return found;
    if (void* found = Offer<AppInfoHost>(name, this)) return found;
    // Ampify.player.queue, in both processes: what each one knows of the queue (src/win/player_bridge.*).
    if (name == QueueHost::kCapabilityName) return static_cast<QueueHost*>(m_player.get());
    // Ampify.player.lyrics: Apple's lyrics, read in the process that has Apple's player.
    if (name == LyricsHost::kCapabilityName) return static_cast<LyricsHost*>(m_player.get());
    // Shuffle and repeat are buttons of the interface; the volume is set there too (Apple's volume control, whose slider
    // then moves) or from the audio process (a hook, the slider stays).
    if (m_process == "ui" && name == PlayerModesHost::kCapabilityName) return static_cast<PlayerModesHost*>(this);
    if (name == PlayerVolumeHost::kCapabilityName) return static_cast<PlayerVolumeHost*>(this);
    if (void* found = m_services->Query(name)) return found;
    // The Marketplace page runs ampify.exe from the interface process, where it is shown.
    if (m_process == "ui" && name == KitCommandHost::kCapabilityName) return static_cast<KitCommandHost*>(m_kitCommands.get());
    // AudioHost: in AMPLibraryAgent.exe, where the audio is rendered, the process's engine, fed by the Atmos
    // leveler's hooks; in the interface process, what that engine shares (audio_share.hpp), equalizer excepted.
    if (m_process == "engine" && name == AudioHost::kCapabilityName) {
        audio::AudioEngine& engine = audio::ProcessAudio();
        engine.StartDeliveryThread();  // analysis frames go out from their own thread, 20 a second at most
        return static_cast<AudioHost*>(&engine);
    }
    if (m_process == "ui" && name == AudioHost::kCapabilityName) return static_cast<AudioHost*>(&win::InterfaceAudio());
    return Host::QueryCapability(name);
}

std::string WinHost::Process() const { return m_process; }

std::string WinHost::Locale() const {
    // "language = de" in settings.txt shows Ampify in German whatever the Windows language.
    if (std::optional<std::string> chosen = m_settings.Get("language"); chosen && !chosen->empty()) return *chosen;
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) == 0) return "fr-FR";
    std::string narrow;
    for (const wchar_t* c = name; *c; ++c) narrow += static_cast<char>(*c);  // locale names are ASCII
    return narrow;
}

std::optional<std::string> WinHost::ReadSetting(const std::string& key) {
    return m_settings.Get(key);
}

void WinHost::WriteSetting(const std::string& key, const std::string& value) {
    m_settings.Set(key, value);
    std::string error;
    if (!Settings::UpdateFile((m_kit / "settings.txt").string(), key, value, error)) {
        m_journal.Write("Settings: " + key + " kept for this run only, " + error);
    }
}

std::optional<std::string> WinHost::ReadStorage(const std::string& elementId) {
    return ReadWholeFile(m_kit / "storage" / (elementId + ".json"));
}

bool WinHost::WriteStorage(const std::string& elementId, const std::string& json) {
    std::error_code error;
    std::filesystem::create_directories(m_kit / "storage", error);
    std::filesystem::path target = m_kit / "storage" / (elementId + ".json");
    return WriteFileAtomically(target, json) == WriteResult::Written;
}

// Ampify.player: Apple's player in AppleMusic.exe, the Windows media controls (SMTC), then the media keys. See
// src/win/player_bridge.*. Never the volume.
PlayerSnapshot WinHost::Player() { return m_player->Snapshot(); }

bool WinHost::Control(PlayerCommand command, int64_t seekMs) { return m_player->Control(command, seekMs); }

void WinHost::SetGainDb(const std::string& elementId, double db) {
    // Only AMPLibraryAgent.exe renders the audio, so only there does the gain reach the samples.
    if (m_process != "engine") {
        static bool noticed = false;
        if (!noticed) m_journal.Write("audio gain: applied only in " + std::string(kLibraryAgentImage) + ", ignored here");
        noticed = true;
        return;
    }
    // The same ceiling as the script side: audio.maxGainDb, from 0 to 12 dB, 6 by default.
    double ceiling = 6.0;
    if (std::optional<std::string> raw = ReadSetting("audio.maxGainDb")) {
        ceiling = std::clamp(std::strtod(raw->c_str(), nullptr), 0.0, 12.0);
    }
    ElementGains().Set(elementId, db, ceiling);
    std::ostringstream line;
    line << std::fixed << std::setprecision(1) << "[" << elementId << "] audio gain: " << db
         << " dB requested, total " << ElementGains().TotalDb() << " dB";
    m_journal.Write(line.str());
}

WinHost::~WinHost() {
    m_services.reset();
    // The kernel's host is a static, destroyed while the process exits, when AMP.Services.dll may already be unloaded:
    // releasing Apple's objects then would call into freed code. The player's few references are left to the system.
    (void)m_player.release();
    for (const auto& [id, hotkey] : m_hotkeys) UnregisterHotKey(nullptr, id);
    if (m_notifyWindow) DestroyWindow(static_cast<HWND>(m_notifyWindow));
}

void WinHost::Service(const KeyCallback& onKey) {
    MSG message;
    while (PeekMessageW(&message, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
        auto found = m_hotkeys.find(static_cast<int>(message.wParam));
        if (found != m_hotkeys.end() && onKey) onKey(found->second.elementId, found->second.combo);
    }
    m_services->Service();

    if (m_iconVisible && std::chrono::steady_clock::now() >= m_balloonUntil) {
        NOTIFYICONDATAW icon{};
        icon.cbSize = sizeof icon;
        icon.hWnd = static_cast<HWND>(m_notifyWindow);
        icon.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &icon);
        m_iconVisible = false;
    }
}

void WinHost::Notify(const std::string& elementId, const std::string& title, const std::string& text) {
    m_journal.Write("[" + elementId + "] notification: " + title + (text.empty() ? "" : " - " + text));

    // A balloon needs a notification icon. A message-only window owns it, and the icon is removed after the balloon.
    if (!MessageWindow()) {
        m_journal.Write("[" + elementId + "] notification not shown: could not create the window");
        return;
    }

    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof icon;
    icon.hWnd = static_cast<HWND>(m_notifyWindow);
    icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    icon.hIcon = win::AmpifyIcon(false);
    wcsncpy_s(icon.szTip, L"Ampify", _TRUNCATE);
    wcsncpy_s(icon.szInfoTitle, ToWide(title).c_str(), _TRUNCATE);
    wcsncpy_s(icon.szInfo, ToWide(text).c_str(), _TRUNCATE);
    icon.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;  // the balloon shows Ampify's logo
    icon.hBalloonIcon = win::AmpifyIcon(true);

    DWORD action = m_iconVisible ? NIM_MODIFY : NIM_ADD;
    if (!Shell_NotifyIconW(action, &icon)) {
        m_journal.Write("[" + elementId + "] notification not shown: the notification icon was refused");
        return;
    }
    m_iconVisible = true;
    m_balloonUntil = std::chrono::steady_clock::now() + kBalloonDuration;
}

void* WinHost::MessageWindow() {
    if (!m_notifyWindow) {
        m_notifyWindow = CreateWindowExW(0, L"STATIC", L"AmpifyNotify", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                         GetModuleHandleW(nullptr), nullptr);
    }
    return m_notifyWindow;
}

bool WinHost::CopyText(const std::string& text) { return win::CopyTextToClipboard(MessageWindow(), text); }

std::optional<std::string> WinHost::PasteText() { return win::ReadClipboardText(); }

std::string WinHost::AppVersion() { return AppleMusicVersion(win::PackageFullName()); }

std::string WinHost::ActiveTheme() {
    std::string id = m_settings.Get("theme.active").value_or("");
    return id == "none" ? std::string() : id;
}

bool WinHost::RegisterKey(const std::string& elementId, const std::string& combo) {
    UINT modifiers = 0;
    UINT key = 0;
    if (!ParseCombo(combo, modifiers, key)) return false;

    int id = m_nextHotkeyId++;
    // Registered without a window, so WM_HOTKEY goes to the calling thread: the JavaScript thread.
    if (!RegisterHotKey(nullptr, id, modifiers, key)) return false;
    m_hotkeys[id] = Hotkey{id, elementId, combo};
    return true;
}

void WinHost::UnregisterKeys(const std::string& elementId) {
    for (auto it = m_hotkeys.begin(); it != m_hotkeys.end();) {
        if (it->second.elementId == elementId) {
            UnregisterHotKey(nullptr, it->first);
            it = m_hotkeys.erase(it);
        } else {
            ++it;
        }
    }
}

bool WinHost::UnregisterKey(const std::string& elementId, const std::string& combo) {
    for (auto it = m_hotkeys.begin(); it != m_hotkeys.end(); ++it) {
        if (it->second.elementId != elementId || it->second.combo != combo) continue;
        UnregisterHotKey(nullptr, it->first);
        m_hotkeys.erase(it);
        return true;
    }
    return false;
}

HttpResponse WinHost::HttpGet(const std::string& url) { return Send(L"GET", url, {}, {}); }

std::optional<RgbaImage> WinHost::DecodeImage(const std::string& bytes, uint32_t maxSide) {
    return win::DecodeWithWic(bytes, maxSide);
}

std::optional<std::string> WinHost::GifFromVideo(const std::string& video, const GifHost::Options& options,
                                                 std::string* error) {
    return win::GifFromVideoWithMf(video, options, error);
}

std::optional<ArtworkHost::File> WinHost::CurrentArtwork() {
    constexpr DWORD kStepTimeoutMs = 3000;
    return win::ReadSmtcThumbnail(kStepTimeoutMs);
}

HttpResponse WinHost::HttpPost(const std::string& url, const std::string& body, const HttpHeaders& headers) {
    return Send(L"POST", url, body, headers);
}

HttpResponse WinHost::HttpGetWithHeaders(const std::string& url, const HttpHeaders& headers) {
    return Send(L"GET", url, {}, headers);
}

HttpResponse WinHost::HttpSend(const std::string& method, const std::string& url, const std::string& body,
                               const HttpHeaders& headers) {
    return Send(ToWide(method).c_str(), url, body, headers);
}

namespace {

// The ProgId Apple Music's package gives a URL scheme, from its package's capabilities. Windows has no default app for
// "music:" until the user picks one, so a plain ShellExecute asked "Open with" instead of opening Apple Music.
std::wstring PackageProgId(const wchar_t* scheme) {
    UINT32 length = 0;
    if (GetCurrentPackageFullName(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0) return {};
    std::wstring full(length, L'\0');
    if (GetCurrentPackageFullName(&length, full.data()) != ERROR_SUCCESS) return {};
    full.resize(length - 1);
    std::wstring path = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\"
                        L"Repository\\Packages\\" + full;
    HKEY package = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &package) != ERROR_SUCCESS) return {};
    std::wstring progId;
    for (DWORD index = 0; progId.empty(); index++) {
        wchar_t app[256];
        DWORD size = 256;
        if (RegEnumKeyExW(package, index, app, &size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        wchar_t value[256];
        DWORD bytes = sizeof value;
        std::wstring associations = std::wstring(app) + L"\\Capabilities\\URLAssociations";
        if (RegGetValueW(package, associations.c_str(), scheme, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS) {
            progId = value;
        }
    }
    RegCloseKey(package);
    return progId;
}

}  // namespace

namespace {

// Opens a URL with Windows, through `progId` when it is not empty (the music: scheme has no default app of its own).
bool ShellOpen(const std::string& url, const std::wstring& progId) {
    std::wstring wide = ToWide(url);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_NOASYNC | (progId.empty() ? 0 : SEE_MASK_CLASSNAME);
    info.lpVerb = L"open";
    info.lpFile = wide.c_str();
    info.lpClass = progId.empty() ? nullptr : progId.c_str();
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) != FALSE;
}

}  // namespace

bool OpenInAppleMusic(const std::string& url) {
    return url.rfind("music:", 0) == 0 && ShellOpen(url, PackageProgId(L"music"));
}

bool WinHost::OpenInBrowser(const std::string& url) {
    // The core has checked the URL: https, a declared host, no user name or port; an Apple Music link comes as music:.
    bool apple = url.rfind("music:", 0) == 0;
    bool opened = ShellOpen(url, apple ? PackageProgId(L"music") : std::wstring());
    std::string where = apple ? "Apple Music" : "the browser";
    m_journal.Write((opened ? "Showed a page in " : "Could not show a page in ") + where);
    return opened;
}

HttpResponse WinHost::Send(const wchar_t* verb, const std::string& url, const std::string& body,
                           const HttpHeaders& headers) {
    std::wstring wide = ToWide(url);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof parts;
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    parts.dwExtraInfoLength = 1;  // ask for the query string too
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) return {0, "invalid address"};
    std::wstring target(parts.lpszUrlPath, parts.dwUrlPathLength);
    target += std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    HINTERNET session = WinHttpOpen(L"Ampify/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {0, "network not available"};
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);

    HttpResponse response{0, {}};
    HINTERNET connection = WinHttpConnect(session, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(),
                                          INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, verb, target.c_str(), nullptr,
                                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        WINHTTP_FLAG_SECURE)
                                   : nullptr;
    // The names were checked by the http module (content-type, authorization, accept), the values have no line break.
    std::wstring extra;
    for (const auto& [name, value] : headers) extra += ToWide(name) + L": " + ToWide(value) + L"\r\n";
    bool ok = request &&
              WinHttpSendRequest(request, extra.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extra.c_str(),
                                 extra.empty() ? 0 : static_cast<DWORD>(-1L),
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                                 static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) &&
              WinHttpReceiveResponse(request, nullptr);

    if (ok) {
        DWORD status = 0;
        DWORD size = sizeof status;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        response.status = static_cast<int>(status);

        // The receive timeout counts per read: a host that sends a byte every few seconds would keep the request, and
        // the element's restart that waits for it, going for minutes. The whole answer gets a deadline instead.
        auto deadline = std::chrono::steady_clock::now() + kAnswerDeadline;
        std::vector<char> chunk(16 * 1024);
        DWORD read = 0;
        while (response.text.size() < kMaxResponseBytes &&
               WinHttpReadData(request, chunk.data(), static_cast<DWORD>(chunk.size()), &read) && read > 0) {
            response.text.append(chunk.data(), read);
            if (std::chrono::steady_clock::now() > deadline) {
                response = {0, "network: the answer took longer than 15 s"};
                break;
            }
        }
    } else {
        response.text = "network: the request failed (code " + std::to_string(GetLastError()) + ")";
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

}  // namespace ampify
