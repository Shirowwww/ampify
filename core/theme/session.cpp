#include "session.hpp"

#include "inspector.hpp"
#include "snippets.hpp"

namespace ampify::theme {

ThemeSession::ThemeSession(Journal& journal) : m_journal(journal), m_engine(journal) {}

void ThemeSession::JournalSources(const SessionRequest& request, bool themeWanted) {
    if (themeWanted) {
        std::string text = "Thème « " + request.theme->name + " » chargé";
        if (!request.theme->scheme.empty()) {
            text += ", schéma " + request.theme->scheme;
            if (!request.schemeReason.empty()) text += " (" + request.schemeReason + ")";
        }
        m_journal.Write(text);
    } else if (!request.themeId.empty()) {
        m_journal.Write("Thème « " + request.themeId + " » refusé, rien n'est appliqué");
    }
    if (request.theme && !request.themeId.empty()) {
        for (const std::string& problem : request.theme->problems) {
            m_journal.Write("Thème « " + request.themeId + " » : " + problem);
        }
    }

    for (const std::shared_ptr<const Theme>& snippet : request.snippets) {
        if (!snippet) continue;
        m_journal.Write("Snippet « " + snippet->id + " » " +
                        (snippet->usable ? std::string("chargé") : std::string("refusé, rien n'est appliqué")));
        for (const std::string& problem : snippet->problems) {
            m_journal.Write("Snippet « " + snippet->id + " » : " + problem);
        }
    }

    if (request.layout) {
        for (const std::string& problem : request.layout->problems) m_journal.Write("Disposition : " + problem);
    }
}

std::optional<std::string> ThemeSession::Tick(TreeBackend& tree, const SessionRequest& request) {
    if (!m_hasGeneration || request.generation != m_generation || request.themeId != m_themeId) {
        if (m_engine.Active()) m_engine.Restore(tree);
        m_generation = request.generation;
        m_hasGeneration = true;
        m_themeId = request.themeId;

        bool themeWanted = !request.themeId.empty() && request.theme && request.theme->usable;
        JournalSources(request, themeWanted);

        bool anything = themeWanted || (request.layout && !request.layout->assignments.empty());
        for (const std::shared_ptr<const Theme>& snippet : request.snippets) {
            anything = anything || (snippet && snippet->usable);
        }

        m_composed.reset();
        if (anything) {
            m_composed = std::make_shared<const Theme>(
                ComposeTheme(themeWanted ? request.theme.get() : nullptr, request.snippets, request.layout.get()));
        }
    }

    if (m_composed) m_engine.Apply(*m_composed, tree);

    // An empty tree is the window before its content (first-run licence, start-up): no dump yet, the request stays and
    // the next tick tries again (bench, 30/10: « Arbre (0 éléments) » written two seconds after start).
    if (request.inspect && !tree.Snapshot().empty()) return InspectTree(tree);
    return std::nullopt;
}

}  // namespace ampify::theme
