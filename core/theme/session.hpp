#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../journal.hpp"
#include "engine.hpp"
#include "theme.hpp"
#include "tree.hpp"

namespace amk::theme {

// What the interface thread should have on screen, as the worker thread decides it (see worker.hpp).
struct SessionRequest {
    std::string themeId;                 // folder of the wanted theme, empty for none
    std::shared_ptr<const Theme> theme;  // the loaded theme, null when themeId is empty
    std::string schemeReason;            // why this scheme (« mode sombre de Windows », « horaire 19:00 »), may be empty
    std::vector<std::shared_ptr<const Theme>> snippets;  // the enabled snippets, loaded with LoadSnippet, in id order
    std::shared_ptr<const Theme> layout;  // the rules of sidebar.json and home.json, null when neither file exists
    std::uint64_t generation = 0;        // changes each time the worker loads again (settings, files, snippets, time)
    bool inspect = false;                // ask for an inspector dump on this tick
};

// Keeps the tree in line with the request. Runs on the interface thread, once per tick.
class ThemeSession {
public:
    explicit ThemeSession(Journal& journal);

    // A new generation restores everything and applies the new set: the theme, then the snippets over it, then the
    // layout. So switching a snippet off (which the worker turns into a new generation) brings back what was under it.
    // The same generation applies again, which styles the elements that appeared since the last tick and writes
    // nothing else. Empty themeId with no snippet and no layout restores. The problems of each source go to the
    // journal once per generation, with the name of the source. Returns the inspector dump when the request asks for one.
    std::optional<std::string> Tick(TreeBackend& tree, const SessionRequest& request);

    bool Active() const { return m_engine.Active(); }

    // What the session applies, theme and snippets together; null when nothing is applied. For AMK.fonts and the like.
    std::shared_ptr<const Theme> Applied() const { return m_composed; }

private:
    void JournalSources(const SessionRequest& request, bool themeWanted);

    Journal& m_journal;
    ThemeEngine m_engine;
    std::uint64_t m_generation = 0;
    bool m_hasGeneration = false;
    std::string m_themeId;
    std::shared_ptr<const Theme> m_composed;
};

}  // namespace amk::theme
