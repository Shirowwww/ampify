#include "session.hpp"

#include "inspector.hpp"

namespace amk::theme {

ThemeSession::ThemeSession(Journal& journal) : m_journal(journal), m_engine(journal) {}

std::optional<std::string> ThemeSession::Tick(TreeBackend& tree, const SessionRequest& request) {
    bool wanted = !request.themeId.empty() && request.theme && request.theme->usable;

    if (!m_hasGeneration || request.generation != m_generation || request.themeId != m_themeId) {
        if (m_engine.Active()) m_engine.Restore(tree);
        m_generation = request.generation;
        m_hasGeneration = true;
        m_themeId = request.themeId;
        if (wanted) m_journal.Write("Thème « " + request.theme->name + " » chargé");
        else if (!request.themeId.empty()) m_journal.Write("Thème « " + request.themeId + " » refusé, rien n'est appliqué");
    }

    if (wanted) m_engine.Apply(*request.theme, tree);

    if (request.inspect) return InspectTree(tree);
    return std::nullopt;
}

}  // namespace amk::theme
