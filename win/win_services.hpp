#pragma once

// The kit services of the Windows host, offered to the API modules as host capabilities: Discord Rich Presence,
// the notification area icon, the local remote control, MIDI input and the output files. Each one is off until its
// setting is on (discord.enabled, tray.enabled, remote.enabled, midi.enabled; all 0 by default), and each element
// still needs its permission (checked by core/natives).
//
// Processes: the icon lives in AppleMusic.exe ("ui"); the remote control and MIDI in AMPLibraryAgent.exe
// ("engine"), where the player state and the internal gain are; Discord and the output files in both.
//
// Threads: every capability method and Service() run on the JavaScript thread, as Host methods do. The service
// threads (icon, server, winmm, Discord) never call the host: they queue, and Service() acts.

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../core/host.hpp"
#include "../core/i18n.hpp"
#include "../core/journal.hpp"
#include "../core/midi_map.hpp"
#include "../core/natives/discord.hpp"
#include "../core/natives/fs.hpp"
#include "../core/natives/midi.hpp"
#include "../core/natives/remote.hpp"
#include "../core/natives/tray.hpp"
#include "../core/remote_protocol.hpp"

namespace ampify::win {

class DiscordIpc;
class TrayIcon;
class RemoteServer;
class MidiInput;

class Services final : public DiscordHost,
                       public TrayHost,
                       public MidiHost,
                       public RemoteHost,
                       public OutputFilesHost,
                       private remote::Backend {
public:
    // Payloads for the elements: ExtensionLoader::DeliverEvent(entry, payload) in the kernel.
    using EventSink = std::function<void(const std::string& entry, const std::string& payload)>;

    // `host` gives the settings, the player and the gain. It is only called on the JavaScript thread, and not
    // before the first capability call or Service().
    Services(Host& host, std::string process, std::filesystem::path kit, Journal& journal);
    ~Services() override;

    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;

    void SetEventSink(EventSink sink);

    // The capability `name` (see Host::QueryCapability), or nullptr.
    void* Query(std::string_view name);

    // JavaScript thread, regularly: starts the services on first use, then runs what their threads queued.
    void Service();

    // DiscordHost
    bool SetActivity(const std::string& elementId, const std::string& clientId,
                     const discord::Activity& activity) override;
    void ClearActivity(const std::string& elementId) override;
    // TrayHost
    bool SetTrayMenu(const std::string& elementId, const std::vector<TrayItem>& items) override;
    void RemoveTrayMenu(const std::string& elementId) override;
    // MidiHost
    bool SubscribeMidi(const std::string& elementId) override;
    void UnsubscribeMidi(const std::string& elementId) override;
    std::vector<std::string> MidiDevices() override;
    // RemoteHost
    bool PublishRemoteState(const std::string& elementId, const std::string& json) override;
    void RemoveRemoteState(const std::string& elementId) override;
    // OutputFilesHost
    bool WriteOutputText(const std::string& elementId, const std::string& name, const std::string& text,
                         std::string* error) override;

    // For the tests: what a device, the icon or a remote client would send.
    void InjectMidi(uint32_t packed);
    void InjectTrayCommand(unsigned command);
    uint16_t remotePort() const;
    bool trayRunning() const;
    bool midiOpen() const;
    std::string remoteStatus();

    // Commands of the kit's own items in the icon menu.
    static constexpr unsigned kTrayPrevious = 1;
    static constexpr unsigned kTrayNext = 2;
    static constexpr unsigned kTrayOpenKit = 3;
    static constexpr unsigned kTrayFirstElementItem = 100;

private:
    // remote::Backend, called on the server thread.
    std::string StatusJson() override;
    bool Command(std::string_view action) override;
    std::optional<double> SetGainDb(double db) override;

    struct RemoteTask {
        std::string action;  // "play", "pause", "next", "previous" or "gain"
        double db = 0;
        std::promise<std::optional<double>> done;
    };

    void EnsureStarted();
    void StartTray();
    void StartRemote();
    void StartMidi();
    void RebuildTrayMenu();
    void RefreshStatus();
    void Emit(const std::string& entry, const std::string& payload);
    std::optional<double> Queue(const std::string& action, double db);

    Host& m_host;
    std::string m_process;
    std::filesystem::path m_kit;
    Journal& m_journal;
    bool m_started = false;
    EventSink m_sink;

    std::unique_ptr<DiscordIpc> m_discord;
    std::string m_discordOwner;

    std::unique_ptr<TrayIcon> m_tray;
    std::map<std::string, std::vector<TrayItem>> m_trayItems;              // element id -> items
    std::map<unsigned, std::pair<std::string, int>> m_trayTargets;         // command -> (element id, item id)

    std::unique_ptr<RemoteServer> m_remote;
    std::map<std::string, std::string> m_remoteStates;  // element id -> JSON object
    double m_remoteGainDb = 0;
    int m_ticks = 0;

    std::unique_ptr<MidiInput> m_midi;
    midi::Map m_midiMap;
    std::vector<std::string> m_midiSubscribers;

    // Filled by the service threads, emptied by Service().
    std::mutex m_queueMutex;
    std::vector<uint32_t> m_midiQueue;
    std::vector<unsigned> m_trayQueue;
    std::deque<std::shared_ptr<RemoteTask>> m_remoteQueue;
    std::string m_status = "{}";
};

}  // namespace ampify::win
