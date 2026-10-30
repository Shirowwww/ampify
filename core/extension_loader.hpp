#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "extension.hpp"
#include "host.hpp"
#include "journal.hpp"
#include "script_extension.hpp"

namespace amk {

struct LoadReport {
    std::vector<std::string> loaded;    // ids registered in the host
    std::vector<std::string> rejected;  // folder names that were skipped, with the reason in the journal
    std::vector<std::string> skipped;   // valid, but declared for the other process
};

// Finds the JavaScript elements (kind extension and app) in a folder with one sub-folder per element, and
// registers them in the extension host. Each element is disabled unless its setting enables it, as for the
// native extensions. Themes and snippets are not loaded by this version of the kernel.
class ExtensionLoader {
public:
    ExtensionLoader(ExtensionHost& hosts, Host& host, Journal& journal, std::filesystem::path root, Clock clock = {});

    // A missing root is not an error: nothing is installed yet. Each folder is checked on its own, so one bad
    // manifest does not stop the others.
    LoadReport Discover();

    // Reloads one element, as the settings say. Returns false for an unknown id.
    bool Restart(const std::string& id, const Settings& settings);

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

    // The folder of the themes. A theme with a main.js is an element too: it runs in AppleMusic.exe while it is the
    // active theme, and only when theme.parts.script is on. Call it before Discover.
    void SetThemeRoot(std::filesystem::path root);

    const std::vector<std::unique_ptr<ScriptExtension>>& elements() const { return m_elements; }

private:
    void LoadFolders(const std::filesystem::path& root, LoadReport& report, bool themes);

    ExtensionHost& m_hosts;
    Host& m_host;
    Journal& m_journal;
    std::filesystem::path m_root;
    std::filesystem::path m_themeRoot;
    Clock m_clock;
    std::vector<std::unique_ptr<ScriptExtension>> m_elements;
};

}  // namespace amk
