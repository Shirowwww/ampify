#include "hover.hpp"

#include <algorithm>

namespace ampify::theme {

HoverTracker::Change HoverTracker::Set(NodeId id, const std::vector<HoverValue>& values, Access& access) {
    auto it = m_elements.find(id);
    if (values.empty()) {
        if (it == m_elements.end()) return Change::None;
        PutBack(id, it->second, access);
        m_elements.erase(it);
        return Change::Removed;
    }
    if (it == m_elements.end()) {
        m_elements[id].values = values;
        return Change::Added;
    }
    if (it->second.values == values) return Change::None;

    // In a state, the new values show at once: put back what was underneath, then show again.
    PutBack(id, it->second, access);
    it->second.values = values;
    Show(id, it->second, access);
    return Change::Updated;
}

void HoverTracker::Toggle(NodeId id, Trigger trigger, bool on, Access& access) {
    auto it = m_elements.find(id);
    if (it == m_elements.end()) return;
    Element& element = it->second;
    bool& state = trigger == Trigger::Active  ? element.pressed
                  : trigger == Trigger::Focus ? element.focused
                                              : element.hovered;
    if (state == on) return;
    state = on;
    Show(id, element, access);
}

// The value a property shows for the states the element is in, null when none of them changes it.
const HoverValue* HoverTracker::Winner(const Element& element, std::string_view property) {
    const std::pair<Trigger, bool> order[] = {
        {Trigger::Active, element.pressed}, {Trigger::Focus, element.focused}, {Trigger::Hover, element.hovered}};
    for (const auto& [trigger, on] : order) {
        if (!on) continue;
        for (const HoverValue& value : element.values) {
            if (value.property == property && value.trigger == trigger) return &value;
        }
    }
    return nullptr;
}

void HoverTracker::Show(NodeId id, Element& element, Access& access) {
    std::vector<std::string> properties;
    for (const HoverValue& value : element.values) {
        if (std::find(properties.begin(), properties.end(), value.property) == properties.end()) {
            properties.push_back(value.property);
        }
    }
    for (const std::string& property : properties) {
        const HoverValue* winner = Winner(element, property);
        auto shown = element.shown.find(property);
        if (!winner) {
            if (shown == element.shown.end()) continue;
            access.WriteRaw(id, property, shown->second.under);
            element.shown.erase(shown);
        } else if (shown == element.shown.end()) {
            std::optional<Value> under = access.ReadRaw(id, property);
            if (!under) continue;  // the element has no such property: nothing to show, nothing to put back
            if (!access.WriteRaw(id, property, winner->value)) continue;
            element.shown.emplace(property, Shown{std::move(*under), winner->trigger});
        } else if (shown->second.trigger != winner->trigger) {
            access.WriteRaw(id, property, winner->value);
            shown->second.trigger = winner->trigger;
        }
    }
}

void HoverTracker::PutBack(NodeId id, Element& element, Access& access) {
    for (const auto& [property, shown] : element.shown) access.WriteRaw(id, property, shown.under);
    element.shown.clear();
}

std::optional<Value> HoverTracker::Read(NodeId id, std::string_view property, Access& access) {
    auto it = m_elements.find(id);
    if (it != m_elements.end()) {
        auto shown = it->second.shown.find(property);
        if (shown != it->second.shown.end()) return shown->second.under;
    }
    return access.ReadRaw(id, property);
}

bool HoverTracker::Write(NodeId id, std::string_view property, const Value& value, Access& access) {
    auto it = m_elements.find(id);
    if (it != m_elements.end()) {
        auto shown = it->second.shown.find(property);
        if (shown != it->second.shown.end()) {
            // Shown when the element leaves its states. A value it refuses then is simply not shown.
            shown->second.under = value;
            return true;
        }
    }
    return access.WriteRaw(id, property, value);
}

std::optional<Value> HoverTracker::ValueOf(NodeId id, std::string_view property, Trigger trigger) const {
    auto it = m_elements.find(id);
    if (it == m_elements.end()) return std::nullopt;
    for (const HoverValue& value : it->second.values) {
        if (value.property == property && value.trigger == trigger) return value.value;
    }
    return std::nullopt;
}

bool HoverTracker::Uses(NodeId id, Trigger trigger) const {
    auto it = m_elements.find(id);
    if (it == m_elements.end()) return false;
    return std::any_of(it->second.values.begin(), it->second.values.end(),
                       [&](const HoverValue& value) { return value.trigger == trigger; });
}

}  // namespace ampify::theme
