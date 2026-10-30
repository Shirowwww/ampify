#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../journal.hpp"
#include "theme.hpp"
#include "tree.hpp"

namespace amk::theme {

// Applies a theme to the live tree and takes it back off.
//
// Every value the engine changes is first read and stored as its original value. Restore writes those
// originals back, so removing a theme (or choosing "none") returns the interface to what it was, without a restart.
// Applying is idempotent: a value that is already in place is not written again, so the engine can run on a timer
// to style elements that appear later (a page opened for the first time).
//
// Must run on the interface thread, like the backend it drives.
class ThemeEngine {
public:
    struct Report {
        int applied = 0;    // values written in this pass
        int unchanged = 0;  // values that were already in place
        int refused = 0;    // the backend refused the element, the property, or the value
    };

    explicit ThemeEngine(Journal& journal);

    Report Apply(const Theme& theme, TreeBackend& tree);

    // Puts back every original value and forgets them. Elements that no longer exist are skipped.
    void Restore(TreeBackend& tree);

    bool Active() const { return m_active; }
    size_t OriginalCount() const { return m_originals.size() + m_resourceOriginals.size(); }

private:
    void ApplyAssignments(const Theme& theme, TreeBackend& tree, const std::vector<NodeInfo>& nodes, Report& report);
    void ApplyResources(const Theme& theme, TreeBackend& tree, Report& report);
    void ApplyDictionary(const Theme& theme, TreeBackend& tree, Report& report);
    void Refuse(const std::string& key, const std::string& message, Report& report);

    Journal& m_journal;
    std::map<std::pair<NodeId, std::string>, Value> m_originals;  // element and XAML property, first value seen
    std::map<std::string, Value> m_resourceOriginals;             // resource key, first value seen
    std::string m_dictionary;  // the resource dictionary currently merged, empty when none
    std::set<std::string> m_reported;  // refusals already written to the journal, so a timer does not repeat them
    bool m_active = false;
};

}  // namespace amk::theme
