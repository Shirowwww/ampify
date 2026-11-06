#include "hover.hpp"

#include <algorithm>

namespace ampify::theme {

namespace {

// A property the element refuses must not leave the others half shown: each one is written on its own, and a refusal
// counts as a write that did not happen.
bool TryWrite(HoverTracker::Access& access, NodeId id, std::string_view property, const Value& value) {
    try {
        return access.WriteRaw(id, property, value);
    } catch (...) {
        return false;
    }
}

std::optional<Value> TryRead(HoverTracker::Access& access, NodeId id, std::string_view property) {
    try {
        return access.ReadRaw(id, property);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

HoverTracker::Change HoverTracker::Set(NodeId id, const std::vector<HoverValue>& values, Access& access) {
    auto it = m_elements.find(id);
    if (values.empty()) {
        if (it == m_elements.end()) return Change::None;
        PutBack(id, it->second, access);
        m_elements.erase(it);
        Prune();
        return Change::Removed;
    }
    if (it == m_elements.end()) {
        // The element it watches may already be in its state: another element watched it first.
        Element& element = m_elements[id];
        element.values = values;
        Show(id, element, access);
        return Change::Added;
    }
    if (it->second.values == values) return Change::None;

    // In a state, the new values show at once: put back what was underneath, then show again.
    PutBack(id, it->second, access);
    it->second.values = values;
    Prune();
    Show(id, it->second, access);
    return Change::Updated;
}

void HoverTracker::Toggle(NodeId id, Trigger trigger, bool on, Access& access) {
    const auto key = std::make_pair(id, trigger);
    if ((m_on.count(key) > 0) == on) return;
    if (on && !Uses(id, trigger)) return;
    if (on) {
        m_on.insert(key);
    } else {
        m_on.erase(key);
    }
    for (auto& [target, element] : m_elements) {
        if (Shows(target, element, id, trigger)) Show(target, element, access);
    }
}

// Whether a value of the element shows in a state of `watched`: that one, or any when `trigger` is empty.
bool HoverTracker::Shows(NodeId id, const Element& element, NodeId watched, std::optional<Trigger> trigger) {
    return std::any_of(element.values.begin(), element.values.end(), [&](const HoverValue& value) {
        return Watched(id, value) == watched && (!trigger || value.trigger == *trigger);
    });
}

// The value a property shows for the states its values watch, null when none of them changes it. What the user does
// comes before what Apple shows the element as; among values of one state, the first in the list (the engine puts the
// winner of the cascade first).
const HoverValue* HoverTracker::Winner(NodeId id, const Element& element, std::string_view property) const {
    for (Trigger trigger : kStatesByPriority) {
        for (const HoverValue& value : element.values) {
            if (value.property != property || value.trigger != trigger) continue;
            if (m_on.count({Watched(id, value), trigger})) return &value;
        }
    }
    return nullptr;
}

void HoverTracker::Show(NodeId id, Element& element, Access& access) const {
    std::vector<std::string> properties;
    for (const HoverValue& value : element.values) {
        if (std::find(properties.begin(), properties.end(), value.property) == properties.end()) {
            properties.push_back(value.property);
        }
    }
    for (const std::string& property : properties) {
        const HoverValue* winner = Winner(id, element, property);
        auto shown = element.shown.find(property);
        if (!winner) {
            if (shown == element.shown.end()) continue;
            TryWrite(access, id, property, shown->second.under);
            element.shown.erase(shown);
        } else if (shown == element.shown.end()) {
            std::optional<Value> under = TryRead(access, id, property);
            if (!under) continue;  // the element has no such property: nothing to show, nothing to put back
            if (!TryWrite(access, id, property, winner->value)) continue;
            element.shown.emplace(property, Shown{std::move(*under), winner->value});
        } else if (shown->second.showing != winner->value) {
            TryWrite(access, id, property, winner->value);
            shown->second.showing = winner->value;
        }
    }
}

void HoverTracker::PutBack(NodeId id, Element& element, Access& access) {
    for (const auto& [property, shown] : element.shown) TryWrite(access, id, property, shown.under);
    element.shown.clear();
}

void HoverTracker::Prune() {
    std::erase_if(m_on, [&](const std::pair<NodeId, Trigger>& state) { return !Uses(state.first, state.second); });
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
        if (value.property == property && value.trigger == trigger && Watched(id, value) == id) return value.value;
    }
    return std::nullopt;
}

bool HoverTracker::Uses(NodeId id, Trigger trigger) const {
    return std::any_of(m_elements.begin(), m_elements.end(),
                       [&](const auto& entry) { return Shows(entry.first, entry.second, id, trigger); });
}

bool HoverTracker::UsesAny(NodeId id) const {
    return std::any_of(m_elements.begin(), m_elements.end(),
                       [&](const auto& entry) { return Shows(entry.first, entry.second, id, std::nullopt); });
}

bool HoverTracker::WatchedByOthers(NodeId id, Trigger trigger) const {
    return std::any_of(m_elements.begin(), m_elements.end(), [&](const auto& entry) {
        return entry.first != id && Shows(entry.first, entry.second, id, trigger);
    });
}

std::vector<NodeId> HoverTracker::WatchedBy(NodeId id) const {
    std::vector<NodeId> watched;
    auto it = m_elements.find(id);
    if (it == m_elements.end()) return watched;
    for (const HoverValue& value : it->second.values) {
        NodeId other = Watched(id, value);
        if (other != id && std::find(watched.begin(), watched.end(), other) == watched.end()) watched.push_back(other);
    }
    return watched;
}

void HoverTracker::Forget(NodeId id, Access& access) {
    m_elements.erase(id);
    bool was = false;
    for (Trigger trigger : kStatesByPriority) was = m_on.erase({id, trigger}) > 0 || was;
    if (!was) return;
    for (auto& [target, element] : m_elements) {
        if (Shows(target, element, id, std::nullopt)) Show(target, element, access);
    }
}

}  // namespace ampify::theme
