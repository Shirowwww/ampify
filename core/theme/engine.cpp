#include "engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <vector>

namespace ampify::theme {

namespace {

// True when `candidate` should replace `current` for the same element and property. !important first, then the layer
// (a snippet beats the theme, sidebar.json beats both), then specificity, then the place in the file.
bool Beats(const Assignment& candidate, const Assignment& current) {
    if (candidate.important != current.important) return candidate.important;
    if (candidate.layer != current.layer) return candidate.layer > current.layer;
    if (candidate.specificity != current.specificity) return current.specificity < candidate.specificity;
    return candidate.order > current.order;
}

std::string Describe(const NodeInfo& node) {
    std::string text = node.type;
    if (!node.name.empty()) text += "#" + node.name;
    return text;
}

// The assignments grouped by the last compound of their selector (the element that receives them): by #name when it
// has one, else by type, else in `any`. An element then only tries the groups of its own name and type, instead of
// every rule of the theme: a pass over Apple Music's ~5000 elements stays short.
struct RuleIndex {
    std::map<std::string, std::vector<const Assignment*>, std::less<>> byName;
    std::map<std::string, std::vector<const Assignment*>, std::less<>> byType;
    std::vector<const Assignment*> any;

    explicit RuleIndex(const std::vector<Assignment>& assignments) {
        for (const Assignment& assignment : assignments) {
            if (assignment.selector.parts.empty()) continue;
            const Compound& last = assignment.selector.parts.back();
            if (!last.name.empty()) {
                byName[last.name].push_back(&assignment);
            } else if (!last.type.empty()) {
                byType[last.type].push_back(&assignment);
            } else {
                any.push_back(&assignment);
            }
        }
    }

    template <typename Visit>
    void ForCandidates(const NodeInfo& node, Visit&& visit) const {
        auto group = [&](const auto& map, const std::string& key) {
            if (key.empty()) return;
            if (auto it = map.find(key); it != map.end()) {
                for (const Assignment* assignment : it->second) visit(*assignment);
            }
        };
        group(byName, node.name);
        group(byType, node.type);
        if (node.fullType != node.type) group(byType, node.fullType);
        for (const Assignment* assignment : any) visit(*assignment);
    }
};

// Each @media condition of the theme once, for TreeBackend::WatchSize.
std::vector<MediaQuery> MediaQueries(const Theme& theme) {
    std::vector<MediaQuery> queries;
    for (const Assignment& assignment : theme.assignments) {
        if (assignment.media && std::find(queries.begin(), queries.end(), *assignment.media) == queries.end()) {
            queries.push_back(*assignment.media);
        }
    }
    return queries;
}

}  // namespace

ThemeEngine::ThemeEngine(Journal& journal) : m_journal(journal) {}

ThemeEngine::Report ThemeEngine::Apply(const Theme& theme, TreeBackend& tree) {
    Report report;
    m_detailed = m_passes == 0;
    m_passes++;
    std::vector<NodeInfo> nodes = tree.Snapshot();

    // What the engine must give back to an element stays while the element exists, in the interface or out of it (the
    // entries of a closed menu). The elements that are gone are forgotten, so a long session stays small.
    std::set<NodeId> live;
    for (const NodeInfo& node : nodes) live.insert(node.id);
    std::map<NodeId, bool> absent;  // the elements missing from this snapshot, and whether they still exist
    auto kept = [&](NodeId id) {
        if (live.count(id)) return true;
        auto [it, fresh] = absent.try_emplace(id, false);
        if (fresh) it->second = tree.Alive(id);
        return it->second;
    };
    for (auto it = m_originals.begin(); it != m_originals.end();) {
        it = kept(it->first.first) ? std::next(it) : m_originals.erase(it);
    }
    std::erase_if(m_baseSizes, [&](const auto& entry) { return !kept(entry.first.first); });
    std::erase_if(m_moves, [&](const Move& move) { return !kept(move.id); });
    std::erase_if(m_hovers, [&](const auto& entry) { return !kept(entry.first); });

    ApplyDictionary(theme, tree, report);
    ApplyResources(theme, tree, report);
    ApplyAssignments(theme, tree, nodes, report);
    m_active = true;

    if ((report.applied > 0 || report.refused > 0) && (m_detailed || (report.applied > 0 && m_passes <= 5))) {
        m_journal.Write("Theme \"" + theme.name + "\": " + std::to_string(report.applied) + " value(s) applied, " +
                        std::to_string(report.unchanged) + " already in place, " + std::to_string(report.refused) +
                        " refused");
    }
    return report;
}

void ThemeEngine::ApplyAssignments(const Theme& theme, TreeBackend& tree, const std::vector<NodeInfo>& nodes,
                                   Report& report) {
    std::vector<PendingMove> pending;
    const RuleIndex index(theme.assignments);
    const std::optional<ViewSize> size = tree.WindowSize();
    tree.WatchSize(MediaQueries(theme));
    for (int position = 0; position < static_cast<int>(nodes.size()); position++) {
        const NodeInfo& node = nodes[position];

        // The winning declaration for each property of this element, and apart from them the :hover ones.
        std::map<std::string, const Assignment*> winners;
        std::map<std::string, const Assignment*> hoverWinners;
        index.ForCandidates(node, [&](const Assignment& assignment) {
            if (assignment.media && !(size && assignment.media->Matches(size->width, size->height))) return;
            if (!Matches(assignment.selector, nodes, position)) return;

            auto& target = assignment.selector.hover ? hoverWinners : winners;
            auto [it, inserted] = target.try_emplace(assignment.property, &assignment);
            if (!inserted && Beats(assignment, *it->second)) it->second = &assignment;
        });

        // The transition first, so a transform or opacity written in the same pass already animates.
        if (auto transition = winners.find("Transition"); transition != winners.end()) {
            WriteValue(node, *transition->second, tree, report, true);
        }
        for (const auto& [property, assignment] : winners) {
            if (property == "Transition") continue;
            if (property == kOrderProperty) {
                pending.push_back({&node, assignment});
                continue;
            }
            WriteValue(node, *assignment, tree, report, true);
        }
        GiveBack(node, winners, tree, report);
        // An element that lost its :hover rules (a theme edited live) is visited too, to take its hover off.
        if (!hoverWinners.empty() || m_hovers.count(node.id)) ApplyHover(node, hoverWinners, tree, report);
    }
    ApplyMoves(pending, tree, report);
}

// Puts back the original of each property the theme no longer gives this element: an @media rule that stopped
// matching, a rule edited out of a theme, an element that moved out of a selector.
void ThemeEngine::GiveBack(const NodeInfo& node, const std::map<std::string, const Assignment*>& winners,
                           TreeBackend& tree, Report& report) {
    for (bool templates : {true, false}) {  // the template first, as in Restore
        for (auto it = m_originals.lower_bound({node.id, std::string()});
             it != m_originals.end() && it->first.first == node.id;) {
            if ((it->first.second == kTemplateProperty) != templates || winners.count(it->first.second)) {
                ++it;
                continue;
            }
            if (tree.Write(node.id, it->first.second, it->second)) report.applied++;
            m_baseSizes.erase(it->first);
            it = m_originals.erase(it);
        }
    }
}

// Gives the backend the :hover values of one element when they differ from the ones it holds. A property the element
// does not have is refused here, as a plain value would be. No value left takes the hover off.
void ThemeEngine::ApplyHover(const NodeInfo& node, const std::map<std::string, const Assignment*>& winners,
                             TreeBackend& tree, Report& report) {
    std::vector<HoverValue> values;
    for (const auto& [property, assignment] : winners) {
        std::optional<Value> current = tree.Read(node.id, property);
        if (!current) {
            Refuse(Describe(node) + " " + property, "\"" + Describe(node) + "\" has no property " + property, report);
            continue;
        }
        // A relative size under the pointer scales the size underneath, the theme's own when it has one.
        std::optional<Value> value = IsRelativeSize(assignment->value) ? ScaleFontSize(assignment->value, *current)
                                                                       : assignment->value;
        if (!value) {
            Refuse(Describe(node) + " " + property, "\"" + Describe(node) + " " + property + "\" has no size to scale",
                   report);
            continue;
        }
        values.push_back({property, *value});
    }

    auto known = m_hovers.find(node.id);
    if (known != m_hovers.end() && known->second == values) {
        report.unchanged += static_cast<int>(values.size());
        return;
    }
    if (known == m_hovers.end() && values.empty()) return;

    if (!tree.SetHover(node.id, values)) {
        Refuse(Describe(node) + " :hover", "\"" + Describe(node) + "\" does not follow the pointer (:hover)", report);
        return;
    }
    if (values.empty()) {
        m_hovers.erase(node.id);
        m_journal.Write("Hover removed: " + Describe(node));
        return;
    }
    report.applied += static_cast<int>(values.size());
    std::string list;
    for (const HoverValue& value : values) list += (list.empty() ? "" : ", ") + value.property + " = " + value.value.text;
    if (m_detailed) m_journal.Write("Hover: " + Describe(node) + " " + list);
    m_hovers[node.id] = std::move(values);
}

// Writes one value when it differs from the current one. `keep` stores the first value seen, for Restore.
bool ThemeEngine::WriteValue(const NodeInfo& node, const Assignment& assignment, TreeBackend& tree, Report& report,
                             bool keep) {
    const std::string& property = assignment.property;
    if (node.icon && property == "FontFamily") {
        report.unchanged++;  // left in its icon font on purpose
        return false;
    }
    std::optional<Value> current = tree.Read(node.id, property);
    if (!current) {
        Refuse(Describe(node) + " " + property, "\"" + Describe(node) + "\" has no property " + property, report);
        return false;
    }
    std::optional<Value> value = Resolve(node.id, property, assignment.value, *current);
    if (!value) {
        Refuse(Describe(node) + " " + property, "\"" + Describe(node) + " " + property + "\" has no size to scale", report);
        return false;
    }
    if (SameValue(*current, *value)) {
        report.unchanged++;
        return false;
    }

    auto key = std::make_pair(node.id, property);
    if (keep && !m_originals.count(key)) m_originals.emplace(key, tree.ReadOriginal(node.id, property).value_or(*current));

    if (!tree.Write(node.id, property, *value)) {
        Refuse(Describe(node) + " " + property, "value refused for \"" + Describe(node) + " " + property + "\"", report);
        return false;
    }
    if (!keep) m_moves.push_back({node.id, *current});
    report.applied++;
    if (m_detailed) m_journal.Write("Applied: " + Describe(node) + " " + property + " = " + value->text + " (" +
                    assignment.source + " line " + std::to_string(assignment.line) + ")");
    return true;
}

// A relative font size becomes pixels: the factor times the size the element had before the theme, kept from the
// first pass since the next ones read back the theme's own. Other values pass as they are.
std::optional<Value> ThemeEngine::Resolve(NodeId id, const std::string& property, const Value& value,
                                          const Value& current) {
    if (!IsRelativeSize(value)) return value;
    const Value& before = m_baseSizes.try_emplace({id, property}, current).first->second;
    return ScaleFontSize(value, before);
}

// Moves the elements in the order of their target position: placing position 0 first, then 1, and so on, never
// disturbs an element already placed, so one pass is enough. Each move is recorded, Restore undoes them backwards.
void ThemeEngine::ApplyMoves(std::vector<PendingMove>& pending, TreeBackend& tree, Report& report) {
    auto position = [](const PendingMove& move) { return std::atoi(move.assignment->value.text.c_str()); };
    std::stable_sort(pending.begin(), pending.end(),
                     [&](const PendingMove& a, const PendingMove& b) { return position(a) < position(b); });
    for (const PendingMove& move : pending) WriteValue(*move.node, *move.assignment, tree, report, false);
}

void ThemeEngine::ApplyResources(const Theme& theme, TreeBackend& tree, Report& report) {
    for (const ResourceOverride& resource : theme.resources) {
        std::optional<Value> current = tree.ReadResource(resource.key);
        if (!current) {
            Refuse("resource " + resource.key, "resource missing from the app: " + resource.key, report);
            continue;
        }
        if (*current == resource.value) {
            report.unchanged++;
            continue;
        }

        if (!m_resourceOriginals.count(resource.key)) m_resourceOriginals.emplace(resource.key, *current);
        if (!tree.WriteResource(resource.key, resource.value)) {
            Refuse("resource " + resource.key, "resource refused: " + resource.key, report);
            continue;
        }
        report.applied++;
        m_journal.Write("Resource: " + resource.key + " = " + resource.value.text);
    }
}

void ThemeEngine::ApplyDictionary(const Theme& theme, TreeBackend& tree, Report& report) {
    if (theme.resourceDictionary == m_dictionary) return;

    if (!tree.SetDictionary(theme.resourceDictionary)) {
        Refuse("dictionary", "resource dictionary refused by the interface", report);
        return;
    }
    m_dictionary = theme.resourceDictionary;
    report.applied++;
    m_journal.Write(m_dictionary.empty() ? "Resource dictionary removed"
                                         : "Resource dictionary merged (resources.xaml)");
}

void ThemeEngine::Refuse(const std::string& key, const std::string& message, Report& report) {
    report.refused++;
    if (m_reported.insert(key).second) m_journal.Write("Refused: " + message);
}

void ThemeEngine::Restore(TreeBackend& tree) {
    int restored = 0;
    int gone = 0;

    // The hovers first: an element under the pointer gets back the values underneath before those are restored too.
    for (const auto& [id, values] : m_hovers) {
        if (tree.SetHover(id, {})) {
            restored++;
        } else {
            gone++;
        }
    }
    for (auto it = m_moves.rbegin(); it != m_moves.rend(); ++it) {
        if (tree.Write(it->id, kOrderProperty, it->before)) {
            restored++;
        } else {
            gone++;
        }
    }
    // A control's own template before its other values: a theme template's Border refuses some of Apple Music's own
    // values (the search box's buttons have a negative padding), and WinUI then stops the app.
    for (bool templates : {true, false}) {
        for (const auto& [key, original] : m_originals) {
            if ((key.second == kTemplateProperty) != templates) continue;
            if (tree.Write(key.first, key.second, original)) {
                restored++;
            } else {
                gone++;
            }
        }
    }
    for (const auto& [key, original] : m_resourceOriginals) {
        if (tree.WriteResource(key, original)) {
            restored++;
        } else {
            gone++;
        }
    }

    if (!m_dictionary.empty()) {
        if (tree.SetDictionary("")) {
            restored++;
        } else {
            gone++;
        }
        m_dictionary.clear();
    }

    m_hovers.clear();
    m_originals.clear();
    m_baseSizes.clear();
    m_resourceOriginals.clear();
    m_moves.clear();
    m_reported.clear();
    m_active = false;
    m_passes = 0;
    m_journal.Write("Theme removed: " + std::to_string(restored) + " value(s) restored, " + std::to_string(gone) +
                    " element(s) gone");
}

}  // namespace ampify::theme
