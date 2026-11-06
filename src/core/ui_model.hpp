#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json.hpp"

namespace ampify {

// What the elements add to the interface. It records and checks, and touches no window: the Windows host renders
// it (XAML) and the tests read it. Every method takes the lock, because the renderer and the owner thread of the
// elements both use it.
//
// Placements: "topbar" and "playbar" (buttons), "menu-context" and "menu-app" (menu items).
struct UiControl {
    int id = 0;  // reported back by the host when the control is clicked
    std::string elementId;
    std::string placement;
    std::string label;
    std::string icon;  // SVG text, as Ampify.icons gives it
    bool clickable = true;  // false: a play bar or top bar text that only shows something, or a menu's submenu
    int parent = 0;         // the submenu (a menu control that is not clickable) this menu entry sits in; 0: the menu itself
    bool active = false;    // a top bar or play bar button that is on: it takes the accent colour
};

struct UiSlot {
    std::string elementId;
    std::string id;  // panel or page path; for a popup, its name (SetPopup) or empty (OpenPopup)
    Json tree;
};

class UiModel {
public:
    // Returns the id of the new control, or 0 when the placement is unknown.
    // `parent` makes the control an entry of a submenu the same element added to the same menu: otherwise 0 is returned
    // as well. A submenu holds entries only, not submenus.
    int AddControl(const std::string& elementId, const std::string& placement, const std::string& label,
                   const std::string& icon, bool clickable = true, int parent = 0);

    // A new label for a control the element added: the host shows it on a top bar or play bar button (its text
    // without an icon, its tooltip with one). False when the element has no such control.
    bool SetControlLabel(const std::string& elementId, int controlId, const std::string& label);

    // The "on" state of a control the element added (the host colours a top bar or play bar button with the accent).
    // False when the element has no such control.
    bool SetControlActive(const std::string& elementId, int controlId, bool active);

    // Stores the tree of a panel. Registering the same panel again replaces it.
    void SetPanel(const std::string& elementId, const std::string& panelId, Json tree);
    // Takes the panel out (the host slides it away); registering it again brings it back. False when there was none.
    bool RemovePanel(const std::string& elementId, const std::string& panelId);

    // Stores the tree of an app page. Registering the same path again replaces it.
    void SetPage(const std::string& elementId, const std::string& path, Json tree);

    // The popups that were opened, in order. The host shows each one once.
    void OpenPopup(const std::string& elementId, Json tree);

    // A popup with a name (Ampify.ReactDOM.render): the first call opens it, the next ones replace its tree in place,
    // so a re-render does not stack popups. ClosePopup removes it.
    void SetPopup(const std::string& elementId, const std::string& popupId, Json tree);
    void ClosePopup(const std::string& elementId, const std::string& popupId);

    // The fullscreen surface: one for the whole interface, the last element that opens it wins. `session` is the one
    // the element got back from its last call, 0 to open: a new session starts when it opens or when it takes the
    // surface from another element (whose session is then listed by TakeDisplacedFullscreens, so the host tells it).
    // A call with an older session (the user or another element closed it meanwhile) changes nothing and returns 0.
    // Otherwise returns the session the tree now belongs to.
    std::uint64_t SetFullscreen(const std::string& elementId, Json tree, std::uint64_t session);
    // Closes the element's fullscreen surface, only while it is still `session` (0: whatever its session).
    void CloseFullscreen(const std::string& elementId, std::uint64_t session);
    // The open fullscreen surface; its id is the session, as text.
    std::optional<UiSlot> Fullscreen() const;
    // The (element, session) pairs whose surface another element took since the last call.
    std::vector<std::pair<std::string, std::uint64_t>> TakeDisplacedFullscreens();

    // A new id for a callback inside a tree (attributes onClick and onChange). It comes from the same counter as the
    // controls, so it never equals a control id: the host reports a click with ScriptExtension::DeliverClick(id),
    // and a change with DeliverEvent("change", {"id":id,"value":...}).
    int ReserveId();

    void SetTitle(const std::string& elementId, const std::string& title);

    // The page that the last navigate call asked for.
    void Navigate(const std::string& elementId, const std::string& path);

    // Ampify.ui.applePlayer.open: one more each time an element asks for Apple's own full-screen player.
    void RequestApplePlayer();
    std::uint64_t ApplePlayerRequests() const;

    // Removes everything that the element added, when it stops.
    void RemoveElement(const std::string& elementId);

    std::optional<UiControl> Find(int controlId) const;
    std::vector<UiControl> Controls() const;
    std::optional<UiSlot> Panel(const std::string& elementId, const std::string& panelId) const;
    std::optional<UiSlot> Page(const std::string& elementId, const std::string& path) const;
    std::vector<UiSlot> Panels() const;  // every registered panel, in registration order (the native renderer)
    std::vector<UiSlot> Pages() const;   // every registered page, in registration order
    std::vector<UiSlot> Popups() const;
    std::optional<std::string> Title(const std::string& elementId) const;
    std::optional<std::pair<std::string, std::string>> Route() const;  // element and path
    std::uint64_t RouteSerial() const;  // one more at each Navigate, so a second visit to the same page shows too

private:
    mutable std::mutex m_mutex;
    int m_nextId = 1;
    std::vector<UiControl> m_controls;
    std::vector<UiSlot> m_panels;
    std::vector<UiSlot> m_pages;
    std::vector<UiSlot> m_popups;
    std::optional<UiSlot> m_fullscreen;
    std::uint64_t m_fullscreenSession = 0;  // of m_fullscreen
    std::uint64_t m_fullscreenSessions = 0;
    std::vector<std::pair<std::string, std::uint64_t>> m_displaced;
    std::map<std::string, std::string> m_titles;
    std::optional<std::pair<std::string, std::string>> m_route;
    std::uint64_t m_routeSerial = 0;
    std::uint64_t m_applePlayerRequests = 0;
};

// Checks a tree sent by an element: only the known primitives, with bounded depth, size and text. Attributes are
// scalars, never objects or lists. On failure `error` gets one English line.
//
// Primitives: stack, row, list, layer, card (with children), text, button, toggle, slider, textfield, image, icon,
// dropdown (whose children are options only) and option (only inside a dropdown). Checked attributes: onClick
// (button, card, image, icon) and onChange (toggle, slider, textfield, dropdown) hold a callback id, a positive
// integer; tooltip is a text on any primitive, tooltipPlacement is top, bottom, left or right, and tooltipDelay is
// a number of milliseconds between 0 and 10000. The style attributes (docs/extensions/api.md, section 5.5) are
// checked where the renderer reads them: opacity, scale, transition, align, valign, grow and inert on any node;
// background, padding, cornerRadius, width, height, minWidth and maxWidth on stack, row, layer and card; fit,
// cornerRadius, width and height on image; size, weight, color, wrap and maxLines on text.
bool ValidateTree(const Json& tree, std::string* error);

// True for the placements listed above.
bool IsPlacement(std::string_view placement);

// The permission that adds to a placement: ui.topbar, ui.playbar or ui.menu.
std::string_view PlacementPermission(std::string_view placement);

// The pointer moves over the full-screen view are passed to its element at most every 250 ms, so that a timer there
// only wakes up a few times a second. `lastMs` is the time the last move was passed, if any.
constexpr std::int64_t kFullscreenPointerGapMs = 250;
bool FullscreenPointerDue(std::int64_t nowMs, std::optional<std::int64_t> lastMs);

// Lets through the pointer events of the full-screen view that moved the pointer, at most every 250 ms. WinUI also
// raises one when the view is redrawn under a still pointer: passed on, it kept the faded controls shown.
class FullscreenPointerGate {
public:
    bool Pass(std::int64_t nowMs, double x, double y);

private:
    std::optional<std::int64_t> m_lastMs;
    std::optional<std::pair<double, double>> m_lastAt;
};

}  // namespace ampify
