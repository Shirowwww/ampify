#include "engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <vector>

namespace amk::theme {

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

}  // namespace

ThemeEngine::ThemeEngine(Journal& journal) : m_journal(journal) {}

ThemeEngine::Report ThemeEngine::Apply(const Theme& theme, TreeBackend& tree) {
    Report report;
    std::vector<NodeInfo> nodes = tree.Snapshot();

    // The originals of elements that have disappeared cannot be restored any more. Forget them, so a long session stays small.
    std::set<NodeId> live;
    for (const NodeInfo& node : nodes) live.insert(node.id);
    for (auto it = m_originals.begin(); it != m_originals.end();) {
        it = live.count(it->first.first) ? std::next(it) : m_originals.erase(it);
    }
    std::erase_if(m_moves, [&](const Move& move) { return !live.count(move.id); });
    std::erase_if(m_hovers, [&](const auto& entry) { return !live.count(entry.first); });

    ApplyDictionary(theme, tree, report);
    ApplyResources(theme, tree, report);
    ApplyAssignments(theme, tree, nodes, report);
    m_active = true;

    if (report.applied > 0 || report.refused > 0) {
        m_journal.Write("Thème « " + theme.name + " » : " + std::to_string(report.applied) + " valeur(s) appliquée(s), " +
                        std::to_string(report.unchanged) + " déjà en place, " + std::to_string(report.refused) +
                        " refusée(s)");
    }
    return report;
}

void ThemeEngine::ApplyAssignments(const Theme& theme, TreeBackend& tree, const std::vector<NodeInfo>& nodes,
                                   Report& report) {
    std::vector<PendingMove> pending;
    for (int index = 0; index < static_cast<int>(nodes.size()); index++) {
        const NodeInfo& node = nodes[index];

        // The winning declaration for each property of this element, and apart from them the :hover ones.
        std::map<std::string, const Assignment*> winners;
        std::map<std::string, const Assignment*> hoverWinners;
        for (const Assignment& assignment : theme.assignments) {
            if (!Matches(assignment.selector, nodes, index)) continue;

            auto& target = assignment.selector.hover ? hoverWinners : winners;
            auto [it, inserted] = target.try_emplace(assignment.property, &assignment);
            if (!inserted && Beats(assignment, *it->second)) it->second = &assignment;
        }

        for (const auto& [property, assignment] : winners) {
            if (property == kOrderProperty) {
                pending.push_back({&node, assignment});
                continue;
            }
            WriteValue(node, *assignment, tree, report, true);
        }
        // An element that lost its :hover rules (a theme edited live) is visited too, to take its hover off.
        if (!hoverWinners.empty() || m_hovers.count(node.id)) ApplyHover(node, hoverWinners, tree, report);
    }
    ApplyMoves(pending, tree, report);
}

// Gives the backend the :hover values of one element when they differ from the ones it holds. A property the element
// does not have is refused here, as a plain value would be. No value left takes the hover off.
void ThemeEngine::ApplyHover(const NodeInfo& node, const std::map<std::string, const Assignment*>& winners,
                             TreeBackend& tree, Report& report) {
    std::vector<HoverValue> values;
    for (const auto& [property, assignment] : winners) {
        if (!tree.Read(node.id, property)) {
            Refuse(Describe(node) + " " + property, "« " + Describe(node) + " » n'a pas de propriété " + property, report);
            continue;
        }
        values.push_back({property, assignment->value});
    }

    auto known = m_hovers.find(node.id);
    if (known != m_hovers.end() && known->second == values) {
        report.unchanged += static_cast<int>(values.size());
        return;
    }
    if (known == m_hovers.end() && values.empty()) return;

    if (!tree.SetHover(node.id, values)) {
        Refuse(Describe(node) + " :hover", "« " + Describe(node) + " » ne suit pas le pointeur (:hover)", report);
        return;
    }
    if (values.empty()) {
        m_hovers.erase(node.id);
        m_journal.Write("Survol retiré : " + Describe(node));
        return;
    }
    report.applied += static_cast<int>(values.size());
    std::string list;
    for (const HoverValue& value : values) list += (list.empty() ? "" : ", ") + value.property + " = " + value.value.text;
    m_journal.Write("Survol : " + Describe(node) + " " + list);
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
        Refuse(Describe(node) + " " + property, "« " + Describe(node) + " » n'a pas de propriété " + property, report);
        return false;
    }
    if (*current == assignment.value) {
        report.unchanged++;
        return false;
    }

    auto key = std::make_pair(node.id, property);
    if (keep && !m_originals.count(key)) m_originals.emplace(key, *current);

    if (!tree.Write(node.id, property, assignment.value)) {
        Refuse(Describe(node) + " " + property, "valeur refusée pour « " + Describe(node) + " " + property + " »", report);
        return false;
    }
    if (!keep) m_moves.push_back({node.id, *current});
    report.applied++;
    m_journal.Write("Appliqué : " + Describe(node) + " " + property + " = " + assignment.value.text + " (" +
                    assignment.source + " ligne " + std::to_string(assignment.line) + ")");
    return true;
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
            Refuse("resource " + resource.key, "ressource absente de l'application : " + resource.key, report);
            continue;
        }
        if (*current == resource.value) {
            report.unchanged++;
            continue;
        }

        if (!m_resourceOriginals.count(resource.key)) m_resourceOriginals.emplace(resource.key, *current);
        if (!tree.WriteResource(resource.key, resource.value)) {
            Refuse("resource " + resource.key, "ressource refusée : " + resource.key, report);
            continue;
        }
        report.applied++;
        m_journal.Write("Ressource : " + resource.key + " = " + resource.value.text);
    }
}

void ThemeEngine::ApplyDictionary(const Theme& theme, TreeBackend& tree, Report& report) {
    if (theme.resourceDictionary == m_dictionary) return;

    if (!tree.SetDictionary(theme.resourceDictionary)) {
        Refuse("dictionary", "dictionnaire de ressources refusé par l'interface", report);
        return;
    }
    m_dictionary = theme.resourceDictionary;
    report.applied++;
    m_journal.Write(m_dictionary.empty() ? "Dictionnaire de ressources retiré"
                                         : "Dictionnaire de ressources fusionné (resources.xaml)");
}

void ThemeEngine::Refuse(const std::string& key, const std::string& message, Report& report) {
    report.refused++;
    if (m_reported.insert(key).second) m_journal.Write("Refusé : " + message);
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
    for (const auto& [key, original] : m_originals) {
        if (tree.Write(key.first, key.second, original)) {
            restored++;
        } else {
            gone++;
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
    m_resourceOriginals.clear();
    m_moves.clear();
    m_reported.clear();
    m_active = false;
    m_journal.Write("Thème retiré : " + std::to_string(restored) + " valeur(s) restaurée(s), " + std::to_string(gone) +
                    " élément(s) disparu(s)");
}

}  // namespace amk::theme
