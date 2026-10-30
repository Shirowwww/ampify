#include "surfaces.hpp"

#include <set>

#include "render_tree.hpp"

namespace amk::uirender {

namespace {

std::string KeyOf(SurfaceKind kind, const std::string& elementId, const std::string& id) {
    return std::string(SurfaceKindName(kind)) + "|" + elementId + "|" + id;
}

Surface FromSlot(SurfaceKind kind, const UiSlot& slot, const std::string& id, const UiModel& model) {
    Surface surface;
    surface.kind = kind;
    surface.elementId = slot.elementId;
    surface.id = id;
    surface.key = KeyOf(kind, slot.elementId, id);
    // An unnamed popup ("#2") gets the node id "popup-open-2".
    std::string forNode = !id.empty() && id[0] == '#' ? "open-" + id.substr(1) : id;
    surface.nodeId = SurfaceNodeId(SurfaceKindName(kind), forNode);
    surface.tree = slot.tree;
    surface.title = model.Title(slot.elementId).value_or("");
    return surface;
}

std::string Signature(const Surface& surface) {
    if (surface.kind == SurfaceKind::Control) {
        return surface.control.placement + "\n" + surface.control.label + "\n" + surface.control.icon;
    }
    return surface.title + "\n" + surface.tree.Dump();
}

}  // namespace

const char* SurfaceKindName(SurfaceKind kind) {
    switch (kind) {
        case SurfaceKind::Panel: return "panel";
        case SurfaceKind::Page: return "page";
        case SurfaceKind::Popup: return "popup";
        case SurfaceKind::Control: return "control";
    }
    return "panel";
}

SurfaceChanges SurfaceTracker::Poll(const UiModel& model) {
    std::vector<Surface> now;
    for (const UiSlot& slot : model.Panels()) now.push_back(FromSlot(SurfaceKind::Panel, slot, slot.id, model));
    for (const UiSlot& slot : model.Pages()) now.push_back(FromSlot(SurfaceKind::Page, slot, slot.id, model));
    std::map<std::string, int> unnamed;  // element -> unnamed popups seen so far
    for (const UiSlot& slot : model.Popups()) {
        std::string id = slot.id;
        if (id.empty()) id = "#" + std::to_string(++unnamed[slot.elementId]);
        now.push_back(FromSlot(SurfaceKind::Popup, slot, id, model));
    }
    for (const UiControl& control : model.Controls()) {
        Surface surface;
        surface.kind = SurfaceKind::Control;
        surface.elementId = control.elementId;
        surface.id = std::to_string(control.id);
        surface.key = KeyOf(SurfaceKind::Control, control.elementId, surface.id);
        surface.control = control;
        now.push_back(std::move(surface));
    }

    SurfaceChanges changes;
    std::map<std::string, std::string> signatures;
    std::set<std::string> present;
    for (Surface& surface : now) {
        std::string signature = Signature(surface);
        auto previous = m_signatures.find(surface.key);
        if (previous == m_signatures.end()) {
            surface.revision = ++m_revision;
            changes.added.push_back(surface);
        } else if (previous->second != signature) {
            surface.revision = ++m_revision;
            changes.changed.push_back(surface);
        } else {
            for (const Surface& old : m_current) {
                if (old.key == surface.key) surface.revision = old.revision;
            }
        }
        signatures[surface.key] = std::move(signature);
        present.insert(surface.key);
    }
    for (const Surface& old : m_current) {
        if (present.count(old.key)) continue;
        Surface gone;
        gone.kind = old.kind;
        gone.elementId = old.elementId;
        gone.id = old.id;
        gone.key = old.key;
        gone.nodeId = old.nodeId;
        changes.removed.push_back(std::move(gone));
    }

    std::optional<std::pair<std::string, std::string>> route = model.Route();
    if (route && route != m_route) changes.route = route;
    m_route = route;

    m_current = std::move(now);
    m_signatures = std::move(signatures);
    return changes;
}

}  // namespace amk::uirender
