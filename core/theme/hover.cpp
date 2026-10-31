#include "hover.hpp"

namespace ampify::theme {

HoverTracker::Change HoverTracker::Set(NodeId id, const std::vector<HoverValue>& values, Access& access) {
    auto it = m_elements.find(id);
    if (values.empty()) {
        if (it == m_elements.end()) return Change::None;
        Exit(id, access);
        m_elements.erase(id);
        return Change::Removed;
    }
    if (it == m_elements.end()) {
        m_elements[id].values = values;
        return Change::Added;
    }
    if (it->second.values == values) return Change::None;

    // Under the pointer, the new values show at once: put back what was underneath, then hover again.
    bool hovered = it->second.hovered;
    Exit(id, access);
    m_elements[id].values = values;
    if (hovered) Enter(id, access);
    return Change::Updated;
}

void HoverTracker::Enter(NodeId id, Access& access) {
    auto it = m_elements.find(id);
    if (it == m_elements.end() || it->second.hovered) return;
    Element& element = it->second;
    element.hovered = true;
    for (const HoverValue& hover : element.values) {
        std::optional<Value> under = access.ReadRaw(id, hover.property);
        if (!under) continue;  // the element has no such property: nothing to show, nothing to put back
        if (!access.WriteRaw(id, hover.property, hover.value)) continue;
        element.under.emplace(hover.property, std::move(*under));
    }
}

void HoverTracker::Exit(NodeId id, Access& access) {
    auto it = m_elements.find(id);
    if (it == m_elements.end() || !it->second.hovered) return;
    Element& element = it->second;
    for (const auto& [property, value] : element.under) access.WriteRaw(id, property, value);
    element.under.clear();
    element.hovered = false;
}

bool HoverTracker::Hovered(NodeId id) const {
    auto it = m_elements.find(id);
    return it != m_elements.end() && it->second.hovered;
}

std::optional<Value> HoverTracker::Read(NodeId id, std::string_view property, Access& access) {
    auto it = m_elements.find(id);
    if (it != m_elements.end() && it->second.hovered) {
        auto under = it->second.under.find(property);
        if (under != it->second.under.end()) return under->second;
    }
    return access.ReadRaw(id, property);
}

bool HoverTracker::Write(NodeId id, std::string_view property, const Value& value, Access& access) {
    auto it = m_elements.find(id);
    if (it != m_elements.end() && it->second.hovered) {
        auto under = it->second.under.find(property);
        if (under != it->second.under.end()) {
            // Shown when the pointer leaves. The element is still there: a value it refuses then is simply not shown.
            under->second = value;
            return true;
        }
    }
    return access.WriteRaw(id, property, value);
}

}  // namespace ampify::theme
