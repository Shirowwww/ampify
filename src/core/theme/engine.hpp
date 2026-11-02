#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../journal.hpp"
#include "theme.hpp"
#include "tree.hpp"

namespace ampify::theme {

// Applies a theme to the live tree and takes it back off.
//
// Every value the engine changes is first read and stored as its original value. Restore writes those
// originals back, so removing a theme (or choosing "none") returns the interface to what it was, without a restart.
// Applying is idempotent: a value that is already in place is not written again, so the engine can run on a timer
// to style elements that appear later (a page opened for the first time).
//
// The position of an element among its siblings (kOrderProperty) is not a value like the others: moving one element
// shifts the others. Moves are made in the order of their target position, which places every element in one pass,
// and Restore undoes them in the reverse order.
//
// The :hover rules make a cascade of their own. Their winners go to the backend (TreeBackend::SetHover), which shows
// them while the pointer is over the element; the engine remembers what it gave, so it calls the backend only when
// that changes, and takes every hover off in Restore (the backend then stops following the pointer).
//
// The ::before and ::after rules make a decorative image per panel (TreeBackend::SetDecor). The engine asks for it on
// every pass, so the backend can put it back if the app emptied the panel, and takes it off when the rule goes or the
// theme comes off.
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

    // Before a new version of the same set goes over the current one (a theme saved again, another scheme): the next
    // pass is reported in the log like a first one, and refusals are reported again.
    void Rearm() {
        m_passes = 0;
        m_reported.clear();
    }

    bool Active() const { return m_active; }
    size_t OriginalCount() const {
        return m_originals.size() + m_resourceOriginals.size() + m_moves.size() + m_hovers.size() + m_decors.size();
    }

private:
    void ApplyAssignments(const Theme& theme, TreeBackend& tree, const std::vector<NodeInfo>& nodes, Report& report);
    void ApplyResources(const Theme& theme, TreeBackend& tree, Report& report);
    void ApplyDictionary(const Theme& theme, TreeBackend& tree, Report& report);
    void Refuse(const std::string& key, const std::string& message, Report& report);

    struct Move {
        NodeId id = 0;
        Value before;  // the position before this move
    };
    struct PendingMove {
        const NodeInfo* node = nullptr;
        const Assignment* assignment = nullptr;
    };
    void ApplyMoves(std::vector<PendingMove>& pending, TreeBackend& tree, Report& report);
    bool WriteValue(const NodeInfo& node, const Assignment& assignment, TreeBackend& tree, Report& report, bool keep);
    std::optional<Value> Resolve(NodeId id, const std::string& property, const Value& value, const Value& current,
                                 TreeBackend& tree);
    // The winning declaration of each state and property of an element (:hover, :focus, :active).
    using StateWinners = std::map<std::pair<Trigger, std::string>, const Assignment*>;
    void ApplyHover(const NodeInfo& node, const StateWinners& winners, TreeBackend& tree, Report& report);
    void GiveBack(const NodeInfo& node, const std::map<std::string, const Assignment*>& winners, TreeBackend& tree,
                  Report& report);
    // The winning declaration of each ::before and ::after property of an element.
    using DecorWinners = std::map<std::pair<PseudoElement, std::string>, const Assignment*>;
    void ApplyDecors(const NodeInfo& node, const DecorWinners& winners, TreeBackend& tree, Report& report);
    void PlaceDecor(const NodeInfo& node, PseudoElement slot, const Decor& decor, const Assignment& content,
                    TreeBackend& tree, Report& report);
    void ForgetDecors(const std::function<bool(NodeId)>& kept, TreeBackend& tree);

    Journal& m_journal;
    std::map<std::pair<NodeId, std::string>, Value> m_originals;  // element and XAML property, first value seen
    std::map<std::pair<NodeId, std::string>, Value> m_baseSizes;  // the same, for a relative font size: the size seen
                                                                   // before the theme, which the next passes no longer read
    std::map<std::string, Value> m_resourceOriginals;             // resource key, first value seen
    std::vector<Move> m_moves;  // every change of position, in the order made
    std::map<NodeId, std::vector<HoverValue>> m_hovers;  // the state values the backend holds for each element
    std::map<std::pair<NodeId, PseudoElement>, Decor> m_decors;  // the ::before and ::after images in each panel
    std::string m_dictionary;  // the resource dictionary currently merged, empty when none
    std::set<std::string> m_reported;  // refusals already written to the log, so a timer does not repeat them
    bool m_active = false;
    // Passes since the theme was applied. The first one writes each value to the log; the later ones (page changes,
    // rows realised while scrolling) only a summary, for the first few, so the log stays readable.
    int m_passes = 0;
    bool m_detailed = true;
};

}  // namespace ampify::theme
