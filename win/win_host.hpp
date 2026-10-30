#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "../core/host.hpp"
#include "../core/journal.hpp"
#include "../core/natives/http.hpp"
#include "../core/settings.hpp"

namespace amk {

// The kernel's Host on Windows. Settings live in settings.txt and each element's storage in
// storage\<id>.json, both under the kit folder. Shortcuts are registered on the thread that runs the
// JavaScript, and Service() on that same thread receives their presses. HTTPS goes through WinHTTP.
// The audio gain (M4) is not connected yet: it does nothing. Nothing here throws, because an exception
// cannot cross the C frames of QuickJS. The kit services (Discord, icon, remote control, MIDI, output files) are
// capabilities kept in win/win_services.*.
namespace win {
class PlayerBridge;
class Services;
}

class WinHost : public Host, public HttpPostHost {
public:
    using KeyCallback = std::function<void(const std::string& elementId, const std::string& combo)>;
    using EventSink = std::function<void(const std::string& entry, const std::string& payload)>;

    WinHost(std::string process, std::filesystem::path kitDirectory, Settings& settings, Journal& journal);
    ~WinHost() override;

    WinHost(const WinHost&) = delete;
    WinHost& operator=(const WinHost&) = delete;

    // Call it on the JavaScript thread, regularly. Delivers the shortcut presses queued for this thread, and
    // removes the notification icon once its balloon has been shown.
    void Service(const KeyCallback& onKey);

    // Where the services send the events of the elements (tray clicks, MIDI): ExtensionLoader::DeliverEvent.
    void SetEventSink(EventSink sink);

    std::string Process() const override;
    std::string Locale() const override;

    std::optional<std::string> ReadSetting(const std::string& key) override;
    void WriteSetting(const std::string& key, const std::string& value) override;

    std::optional<std::string> ReadStorage(const std::string& elementId) override;
    bool WriteStorage(const std::string& elementId, const std::string& json) override;

    PlayerSnapshot Player() override;
    bool Control(PlayerCommand command, int64_t seekMs) override;

    void SetGainDb(const std::string& elementId, double db) override;

    void Notify(const std::string& elementId, const std::string& title, const std::string& text) override;

    bool RegisterKey(const std::string& elementId, const std::string& combo) override;
    void UnregisterKeys(const std::string& elementId) override;

    HttpResponse HttpGet(const std::string& url) override;
    HttpResponse HttpPost(const std::string& url, const std::string& body, const HttpHeaders& headers) override;

    UiModel& Ui() override { return m_ui; }

    // AudioHost (core/audio/audio_host.hpp) in AMPLibraryAgent.exe only, where the audio is rendered: the process's
    // engine, fed by the Atmos leveler's hooks. QueueHost (core/player_queue.hpp) in both processes.
    void* QueryCapability(std::string_view name) override;

private:
    struct Hotkey {
        int id;
        std::string elementId;
        std::string combo;
    };

    HttpResponse Send(const wchar_t* verb, const std::string& url, const std::string& body, const HttpHeaders& headers);

    UiModel m_ui;
    std::string m_process;
    std::filesystem::path m_kit;
    Settings& m_settings;
    Journal& m_journal;

    std::map<int, Hotkey> m_hotkeys;
    int m_nextHotkeyId = 1;

    void* m_notifyWindow = nullptr;  // a message-only window that owns the notification icon
    bool m_iconVisible = false;
    std::chrono::steady_clock::time_point m_balloonUntil;

    std::unique_ptr<win::PlayerBridge> m_player;  // AMK.player and the queue capability, see win/player_bridge.*
    std::unique_ptr<win::Services> m_services;
};

}  // namespace amk
