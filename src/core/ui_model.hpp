#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace ampify {

// What the elements add to the interface. It records and checks, and touches no window: the Windows host renders
// it (XAML) and the tests read it. Every method takes the lock, because the renderer and the owner thread of the
// elements both use it.
//
// Placements: the list, with their permissions, is in ui_vocab.cpp.
struct UiControl {
    int id = 0;  // reported back by the host when the control is clicked
    std::string elementId;
    std::string placement;
    std::string label;
    std::string icon;  // SVG text, as Ampify.icons gives it, or a Segoe Fluent glyph of 8 bytes at most
    bool clickable = true;  // false: a play bar or top bar text that only shows something, or a menu's submenu
    int parent = 0;         // the submenu (a menu control that is not clickable) this menu entry sits in; 0: the menu itself
    bool active = false;    // a top bar or play bar button that is on: it takes the accent colour
    bool disabled = false;  // drawn greyed and ignores clicks (top bar, play bar, menu entry)
    // A context menu entry shows only in the menus of these kinds of item (IsMenuItemKind); empty: in every one.
    std::vector<std::string> kinds;
    // A play bar button's stretch of the progress line, as parts of the song from 0 to 1 (the A-B loop's passage);
    // -1: not marked. A start without an end shows the start alone.
    double markFrom = -1;
    double markTo = -1;
};

// The readout under the pointer on the progress line of the playback bar (Ampify.ui.playbar.readout and onHover).
// `totalMs` is the length the line stands for, set by the last element that asked for a readout (0: no readout);
// `listeners` are the elements told where the pointer is. `serial` changes each time either changes.
struct UiPlaybarHover {
    double totalMs = 0;
    std::vector<std::string> listeners;
    std::uint64_t serial = 0;
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

    // Greys a control the element added, or brings it back. False when the element has no such control.
    bool SetControlDisabled(const std::string& elementId, int controlId, bool disabled);

    // Marks a stretch of the progress line for a play bar button the element added (see UiControl::markFrom), kept
    // within 0 to 1 and in order; a negative `from` clears it, a negative `to` marks the start alone. False when the
    // element has no such play bar button.
    bool SetControlMark(const std::string& elementId, int controlId, double from, double to);

    // Shows, under the pointer on the progress line, a marker and the time at that point of a span `totalMs` long; 0,
    // a negative number or NaN takes the element's readout away. The last element to ask is the one shown.
    void SetPlaybarReadout(const std::string& elementId, double totalMs);
    // Whether the element is told where the pointer is on the progress line.
    void SetPlaybarHoverListener(const std::string& elementId, bool listening);
    UiPlaybarHover PlaybarHover() const;

    // The kinds of item a context menu entry is for (see UiControl::kinds). False when the element has no such control.
    bool SetControlKinds(const std::string& elementId, int controlId, std::vector<std::string> kinds);

    // Takes out a control the element added, and the entries of a submenu with it. False when it has no such control.
    bool RemoveControl(const std::string& elementId, int controlId);

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

    void Navigate(const std::string& elementId, const std::string& path);

    // Ampify.ui.applePlayer.open: one more each time an element asks for Apple's own full-screen player.
    void RequestApplePlayer();
    std::uint64_t ApplePlayerRequests() const;

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
    std::vector<std::pair<std::string, double>> m_readouts;  // element and length, the last asked at the end
    std::vector<std::string> m_hoverListeners;
    std::uint64_t m_hoverSerial = 0;
};

// Checks a tree sent by an element: only the primitives of ui_vocab.cpp, with the depth, size and text limits of
// ui_vocab.hpp. Attributes are scalars, never objects or lists. On failure `error` gets one English line.
//
// Checked attributes: onClick and onChange hold a callback id, a positive integer, on the primitives that take them;
// tooltip is a text on any primitive, tooltipPlacement is top, bottom, left or right, and tooltipDelay is a number of
// milliseconds. The style attributes (docs/extensions/api.md, section 5.5) are checked where the renderer reads them,
// by the style kind of the primitive.
bool ValidateTree(const Json& tree, std::string* error);

// True for the placements of ui_vocab.cpp.
bool IsPlacement(std::string_view placement);

// The kinds of item Apple Music's data gives a menu: song, album, artist, composer, genre, playlist, station, video,
// concert and container.
bool IsMenuItemKind(std::string_view kind);

// Whether a context menu entry goes in the menu of an item of `kind` (empty when Apple's data does not say).
bool ShowsForItem(const UiControl& control, std::string_view kind);

// The permission that adds to a placement (ui.menu for a name that is not a placement).
std::string_view PlacementPermission(std::string_view placement);

}  // namespace ampify
