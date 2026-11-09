#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "journal.hpp"
#include "settings.hpp"

namespace ampify {

// What an extension tells the kernel about itself. `start` and `stop` run on the kernel's thread.
struct ExtensionInfo {
    std::string id;  // stable and lowercase, used in settings keys: "atmos-leveler"
    std::string name;  // shown in the settings UI
    bool enabledByDefault = false;
    std::function<bool()> start;  // returns false when it cannot start; may throw
    std::function<void()> stop;
    // When set, it decides whether the element starts, in place of the extensions.<id>.enabled setting.
    // Last on purpose: the aggregate initializers above stay valid.
    std::function<bool(const Settings&)> isEnabled;
};

enum class ExtensionState {
    Off,      // disabled in the settings
    Running,
    Failed,   // enabled, but start failed or threw: the other extensions still start
    Blocked,  // enabled, but the kernel is idle (safe mode or unknown app version)
};

// Loads, starts and stops extensions. Extensions are enabled one by one in the settings,
// under the key `extensions.<id>.enabled`.
class ExtensionHost {
public:
    explicit ExtensionHost(Journal& journal);

    // Returns false when the id is empty or already registered.
    bool Add(ExtensionInfo info);

    // An empty `blockReason` lets the enabled extensions start. Otherwise nothing starts and
    // every enabled extension is marked Blocked, with the reason written to the journal.
    void StartAll(const Settings& settings, std::string_view blockReason = {});

    // Stops running extensions in reverse start order. A stop that throws is logged and skipped.
    void StopAll();

    // Reloads one extension: stop, then start again if it is still enabled. Returns false for an unknown id.
    bool Restart(const std::string& id, const Settings& settings);

    // Starts one extension if the settings enable it and it is not running yet. Returns false for an unknown id.
    bool Start(const std::string& id, const Settings& settings);

    // False for an unknown id.
    bool Stop(const std::string& id);

    // Stops one extension and forgets it, so that its id can be added again. Returns false for an unknown id.
    bool Remove(const std::string& id);

    bool Has(const std::string& id) const;

    ExtensionState StateOf(const std::string& id) const;

private:
    struct Entry {
        ExtensionInfo info;
        ExtensionState state = ExtensionState::Off;
    };

    void StartEntry(Entry& entry, const Settings& settings, std::string_view blockReason);
    void StopEntry(Entry& entry);

    Journal& m_journal;
    std::vector<Entry> m_entries;
};

}  // namespace ampify
