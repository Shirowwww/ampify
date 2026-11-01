#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tree.hpp"

namespace ampify::theme {

// The :hover, :focus and :active values of the elements, for a backend (TreeBackend::SetHover). Portable, so the tests
// drive the same logic as the Windows backend: the backend only follows the element and calls Toggle.
//
// While an element is in a state, each property that state changes shows the state's value and the tracker keeps the
// value underneath. Pressed beats focused, which beats hovered, as in a stylesheet written :hover, :focus, :active. Read
// gives the value underneath and Write changes it, without touching the element: the engine, which runs on a timer,
// sees the values it wrote and never fights a state. Leaving the last state writes the values underneath back.
class HoverTracker {
public:
    // The element itself, as the backend reads and writes it without the states.
    class Access {
    public:
        virtual ~Access() = default;
        virtual std::optional<Value> ReadRaw(NodeId id, std::string_view property) = 0;
        virtual bool WriteRaw(NodeId id, std::string_view property, const Value& value) = 0;
    };

    enum class Change {
        None,     // same values as before
        Added,    // a new element: the backend starts following it
        Updated,  // other values for an element already followed
        Removed,  // no value left: the backend stops following it
    };

    Change Set(NodeId id, const std::vector<HoverValue>& values, Access& access);

    // The element enters or leaves a state.
    void Toggle(NodeId id, Trigger trigger, bool on, Access& access);
    void Enter(NodeId id, Access& access) { Toggle(id, Trigger::Hover, true, access); }
    void Exit(NodeId id, Access& access) { Toggle(id, Trigger::Hover, false, access); }

    // Read and Write through the states. For a property no state changes now, the element.
    std::optional<Value> Read(NodeId id, std::string_view property, Access& access);
    bool Write(NodeId id, std::string_view property, const Value& value, Access& access);

    // The value one property shows in a state, whether the element is in it or not.
    std::optional<Value> ValueOf(NodeId id, std::string_view property, Trigger trigger = Trigger::Hover) const;

    // Whether some value of the element shows in that state: the backend follows only those states.
    bool Uses(NodeId id, Trigger trigger) const;

    // The element is gone: forget it without writing anything.
    void Forget(NodeId id) { m_elements.erase(id); }

    bool Has(NodeId id) const { return m_elements.count(id) > 0; }
    size_t Count() const { return m_elements.size(); }

private:
    struct Shown {
        Value under;      // what the element had before a state changed the property
        Trigger trigger;  // the state whose value it shows now
    };
    struct Element {
        std::vector<HoverValue> values;
        std::map<std::string, Shown, std::less<>> shown;  // the properties a state changes now
        bool hovered = false;
        bool focused = false;
        bool pressed = false;
    };

    // Writes what the states the element is in show, and puts back the properties none of them changes any more.
    static void Show(NodeId id, Element& element, Access& access);
    static const HoverValue* Winner(const Element& element, std::string_view property);
    static void PutBack(NodeId id, Element& element, Access& access);

    std::map<NodeId, Element> m_elements;
};

}  // namespace ampify::theme
