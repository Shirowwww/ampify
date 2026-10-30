#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "extension.hpp"
#include "host.hpp"
#include "journal.hpp"
#include "reload.hpp"
#include "script_extension.hpp"

namespace amk {

// The `expose_api` setting (Spicetify's `expose_apis`): 0 keeps every JavaScript element (extension, app, theme
// script) off, whatever its own setting says. Missing means 1.
inline constexpr std::string_view kExposeApiKey = "expose_api";

struct LoadReport {
    std::vector<std::string> loaded;    // ids registered in the host (HostId: app:<id> for an app)
    std::vector<std::string> rejected;  // folder names that were skipped, with the reason in the journal
    std::vector<std::string> skipped;   // valid, but declared for the other process
};

// Finds the JavaScript elements in the kit folders, one sub-folder per element, and registers them in the extension
// host: extensions in `extensions`, apps in `apps` (SetAppRoot), theme scripts in `themes` (SetThemeRoot). Each
// element is disabled unless its setting enables it, as for the native extensions. Snippets are not loaded: they
// have no script.
class ExtensionLoader {
public:
    ExtensionLoader(ExtensionHost& hosts, Host& host, Journal& journal, std::filesystem::path root, Clock clock = {});

    // The folder of the apps. An app is switched by apps.<id>.enabled. Call it before Discover.
    void SetAppRoot(std::filesystem::path root);

    // The folder of the themes. A theme with a main.js is an element too: it runs in AppleMusic.exe while it is the
    // active theme, and only when theme.parts.script is on. Call it before Discover.
    void SetThemeRoot(std::filesystem::path root);

    // Loads the folders that are not loaded yet. A missing root is not an error: nothing is installed yet. Each
    // folder is checked on its own, so one bad manifest does not stop the others. Nothing is started here.
    LoadReport Discover();

    // The id of an element in the extension host and in reload.txt: its manifest id, or app:<id> for an app, so that
    // an app and an extension can share an id.
    static std::string HostId(const Manifest& manifest);

    // Requests of reload.txt (core/reload.hpp). Each returns false when no element answers to the target. A target
    // that no folder element has may be a native extension of the host, such as atmos-leveler.
    // Start: loads the element if it is new (Rescan), then starts it if the settings enable it.
    bool Start(const std::string& target, const Settings& settings);
    // Stop: stops the element; it stays loaded.
    bool Stop(const std::string& target);
    // Restart: stops the element, reads its folder again (manifest and code), then starts it if the settings enable it.
    bool Restart(const std::string& target, const Settings& settings);
    // Rescan: drops the elements whose folder is gone, loads the new folders and starts those the settings enable.
    LoadReport Rescan(const Settings& settings);

    // Runs the requests in order, with one journal line each.
    void Apply(const std::vector<ReloadRequest>& requests, const Settings& settings);

    // Owner thread: runs the queued tasks and the timers of every element.
    void Pump();

    // Any thread: queues a player event for every running element.
    void DeliverPlayer(const std::string& event, const PlayerSnapshot& snapshot);

    // Any thread: queues a shortcut press for the element that registered it.
    void DeliverKey(const std::string& elementId, const std::string& combo);

    // Any thread: queues a click on a control of the interface, for the element that added it.
    void DeliverClick(int controlId);

    // Any thread: queues dispatch.<entry>(payload) for every running element (ScriptExtension::DeliverEvent).
    void DeliverEvent(const std::string& entry, const std::string& payload);

    // Owner thread only: a rescan or a restart changes the list.
    const std::vector<std::unique_ptr<ScriptExtension>>& elements() const { return m_elements; }

private:
    enum class Origin { Extensions, Apps, Themes };

    void LoadFolders(const std::filesystem::path& root, Origin origin, LoadReport& report);
    void LoadFolder(const std::filesystem::path& folder, Origin origin, LoadReport& report);
    ScriptExtension* Find(const std::string& hostId) const;
    std::filesystem::path FolderOf(const ScriptExtension& element) const;
    Origin OriginOf(const ScriptExtension& element) const;
    void Forget(const std::string& hostId);

    ExtensionHost& m_hosts;
    Host& m_host;
    Journal& m_journal;
    std::filesystem::path m_root;
    std::filesystem::path m_appRoot;
    std::filesystem::path m_themeRoot;
    Clock m_clock;
    // Changed on the owner thread only (load, rescan, restart); the Deliver calls of other threads read it under
    // this lock.
    std::mutex m_elementsMutex;
    std::vector<std::unique_ptr<ScriptExtension>> m_elements;
};

}  // namespace amk
