#include "engine.hpp"

#include <iterator>
#include <optional>
#include <vector>

namespace amk::theme {

namespace {

// True when `candidate` should replace `current` for the same element and property.
bool Beats(const Assignment& candidate, const Assignment& current) {
    if (candidate.important != current.important) return candidate.important;
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
    for (int index = 0; index < static_cast<int>(nodes.size()); index++) {
        const NodeInfo& node = nodes[index];

        // The winning declaration for each property of this element.
        std::map<std::string, const Assignment*> winners;
        for (const Assignment& assignment : theme.assignments) {
            if (!Matches(assignment.selector, nodes, index)) continue;

            auto [it, inserted] = winners.try_emplace(assignment.property, &assignment);
            if (!inserted && Beats(assignment, *it->second)) it->second = &assignment;
        }

        for (const auto& [property, assignment] : winners) {
            std::optional<Value> current = tree.Read(node.id, property);
            if (!current) {
                Refuse(Describe(node) + " " + property,
                       "« " + Describe(node) + " » n'a pas de propriété " + property, report);
                continue;
            }
            if (*current == assignment->value) {
                report.unchanged++;
                continue;
            }

            auto key = std::make_pair(node.id, property);
            if (!m_originals.count(key)) m_originals.emplace(key, *current);

            if (!tree.Write(node.id, property, assignment->value)) {
                Refuse(Describe(node) + " " + property, "valeur refusée pour « " + Describe(node) + " " + property + " »",
                       report);
                continue;
            }
            report.applied++;
            m_journal.Write("Appliqué : " + Describe(node) + " " + property + " = " + assignment->value.text +
                            " (user.css ligne " + std::to_string(assignment->line) + ")");
        }
    }
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

    m_originals.clear();
    m_resourceOriginals.clear();
    m_reported.clear();
    m_active = false;
    m_journal.Write("Thème retiré : " + std::to_string(restored) + " valeur(s) restaurée(s), " + std::to_string(gone) +
                    " élément(s) disparu(s)");
}

}  // namespace amk::theme
