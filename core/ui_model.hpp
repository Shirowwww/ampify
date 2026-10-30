#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace amk {

// What the elements add to the interface. It records and checks, and touches no window: the Windows host renders
// it (XAML, M8) and the tests read it. Every method takes the lock, because the renderer and the owner thread of the
// elements both use it.
//
// Placements: "topbar" and "playbar" (buttons), "menu-context" and "menu-app" (menu items).
struct UiControl {
    int id = 0;  // reported back by the host when the control is clicked
    std::string elementId;
    std::string placement;
    std::string label;
    std::string icon;  // SVG text, as AMK.icons gives it
};

struct UiSlot {
    std::string elementId;
    std::string id;  // panel or page path; empty for a popup
    Json tree;
};

class UiModel {
public:
    // Returns the id of the new control, or 0 when the placement is unknown.
    int AddControl(const std::string& elementId, const std::string& placement, const std::string& label,
                   const std::string& icon);

    // Stores the tree of a panel. Registering the same panel again replaces it.
    void SetPanel(const std::string& elementId, const std::string& panelId, Json tree);

    // Stores the tree of an app page. Registering the same path again replaces it.
    void SetPage(const std::string& elementId, const std::string& path, Json tree);

    // The popups that were opened, in order. The host shows each one once.
    void OpenPopup(const std::string& elementId, Json tree);

    void SetTitle(const std::string& elementId, const std::string& title);

    // The page that the last navigate call asked for.
    void Navigate(const std::string& elementId, const std::string& path);

    // Removes everything that the element added, when it stops.
    void RemoveElement(const std::string& elementId);

    std::optional<UiControl> Find(int controlId) const;
    std::vector<UiControl> Controls() const;
    std::optional<UiSlot> Panel(const std::string& elementId, const std::string& panelId) const;
    std::optional<UiSlot> Page(const std::string& elementId, const std::string& path) const;
    std::vector<UiSlot> Popups() const;
    std::optional<std::string> Title(const std::string& elementId) const;
    std::optional<std::pair<std::string, std::string>> Route() const;  // element and path

private:
    mutable std::mutex m_mutex;
    int m_nextId = 1;
    std::vector<UiControl> m_controls;
    std::vector<UiSlot> m_panels;
    std::vector<UiSlot> m_pages;
    std::vector<UiSlot> m_popups;
    std::map<std::string, std::string> m_titles;
    std::optional<std::pair<std::string, std::string>> m_route;
};

// Checks a tree sent by an element: only the known primitives, with bounded depth, size and text. Attributes are
// scalars, never objects or lists. On failure `error` gets one French line.
bool ValidateTree(const Json& tree, std::string* error);

// True for the placements listed above.
bool IsPlacement(std::string_view placement);

// The permission that adds to a placement: ui.topbar, ui.playbar or ui.menu.
std::string_view PlacementPermission(std::string_view placement);

}  // namespace amk
