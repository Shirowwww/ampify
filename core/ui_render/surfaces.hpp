#pragma once

// What the native renderer must show, read from core/ui_model: the panels, the app pages, the popups and the buttons
// of the top bar and the play bar. The tracker compares each reading with the previous one, so the interface thread
// only receives the surfaces that changed. Portable and tested under Linux; the Windows side (win/ui_service.cpp)
// polls it from its worker thread, since UiModel takes its own lock.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../json.hpp"
#include "../ui_model.hpp"

namespace ampify::uirender {

enum class SurfaceKind { Panel, Page, Popup, Control, Fullscreen };

struct Surface {
    SurfaceKind kind = SurfaceKind::Panel;
    std::string elementId;
    // panel id, page path, popup key ("#<n>" for an unnamed popup), control id as text, or the fullscreen session as
    // text (each session is a new surface, so a surface the user closed stays closed for the rest of its session)
    std::string id;
    std::string key;         // unique among the surfaces: "<kind>|<element>|<id>"
    std::string nodeId;      // id of the root render node (SurfaceNodeId; "fullscreen-main"), "" for a control
    Json tree;               // the interface tree (not for a control)
    UiControl control;       // for a control only
    std::string title;       // Ampify.ui.title of the element, may be empty
    std::uint64_t revision = 0;  // changes each time the surface changes
};

struct SurfaceChanges {
    std::vector<Surface> added;    // new surfaces, in model order
    std::vector<Surface> changed;  // surfaces whose tree, title or control changed
    std::vector<Surface> removed;  // surfaces that are gone (only kind, element, id and key are meaningful)
    std::optional<std::pair<std::string, std::string>> route;  // a new Ampify.ui.navigate (element, path), once

    bool Empty() const { return added.empty() && changed.empty() && removed.empty() && !route; }
};

class SurfaceTracker {
public:
    // Reads the model and returns what changed since the last call. The first call returns every surface as added.
    SurfaceChanges Poll(const UiModel& model);

    // The surfaces of the last poll, in model order.
    const std::vector<Surface>& Current() const { return m_current; }

private:
    std::vector<Surface> m_current;
    std::map<std::string, std::string> m_signatures;  // key -> what the surface showed, as text
    std::optional<std::pair<std::string, std::string>> m_route;
    std::uint64_t m_routeSerial = 0;
    std::uint64_t m_revision = 0;
};

const char* SurfaceKindName(SurfaceKind kind);

}  // namespace ampify::uirender
