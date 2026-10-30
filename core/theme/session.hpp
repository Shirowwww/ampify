#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "../journal.hpp"
#include "engine.hpp"
#include "theme.hpp"
#include "tree.hpp"

namespace amk::theme {

// What the interface thread should have on screen, as the worker thread decides it.
struct SessionRequest {
    std::string themeId;                 // folder of the wanted theme, empty for none
    std::shared_ptr<const Theme> theme;  // the loaded theme, null when themeId is empty
    std::uint64_t generation = 0;        // changes each time the worker loads a theme again (settings or files changed)
    bool inspect = false;                // ask for an inspector dump on this tick
};

// Keeps the tree in line with the request. Runs on the interface thread, once per tick.
class ThemeSession {
public:
    explicit ThemeSession(Journal& journal);

    // A new generation restores the old theme and applies the new one. The same generation applies again, which
    // styles the elements that appeared since the last tick and writes nothing else. Empty themeId restores.
    // Returns the inspector dump when the request asks for one.
    std::optional<std::string> Tick(TreeBackend& tree, const SessionRequest& request);

    bool Active() const { return m_engine.Active(); }

private:
    Journal& m_journal;
    ThemeEngine m_engine;
    std::uint64_t m_generation = 0;
    bool m_hasGeneration = false;
    std::string m_themeId;
};

}  // namespace amk::theme
