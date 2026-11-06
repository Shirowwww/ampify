#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tree.hpp"

namespace ampify::theme {

// The :hover, :focus and :active values of the elements, for a backend (TreeBackend::SetHover). Portable, so the tests
// drive the same logic as the Windows backend: the backend only follows the elements and calls Toggle.
//
// While an element is in a state, each property that state changes shows the state's value and the tracker keeps the
// value underneath. Pressed beats focused, which beats hovered, as in a stylesheet written :hover, :focus, :active. Read
// gives the value underneath and Write changes it, without touching the element: the engine, which runs on a timer,
// sees the values it wrote and never fights a state. Leaving the last state writes the values underneath back.
//
// A value may watch another element (HoverValue::watch, a song's row for `Grid#Row:hover Button`): the states are kept
// per watched element, so the row entering one shows the values of every element that watches it.
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

    // The element enters or leaves a state: the values shown in that state of it, its own and those of the elements
    // that watch it, show or go. Ignored for a state no value shows in.
    void Toggle(NodeId id, Trigger trigger, bool on, Access& access);
    void Enter(NodeId id, Access& access) { Toggle(id, Trigger::Hover, true, access); }
    void Exit(NodeId id, Access& access) { Toggle(id, Trigger::Hover, false, access); }

    // Read and Write through the states. For a property no state changes now, the element.
    std::optional<Value> Read(NodeId id, std::string_view property, Access& access);
    bool Write(NodeId id, std::string_view property, const Value& value, Access& access);

    // The value one property shows in a state of the element itself, whether the element is in it or not.
    std::optional<Value> ValueOf(NodeId id, std::string_view property, Trigger trigger = Trigger::Hover) const;

    // Whether some value shows in that state of the element, its own or one of an element that watches it: the backend
    // follows only those states.
    bool Uses(NodeId id, Trigger trigger) const;
    bool UsesAny(NodeId id) const;
    // Whether a value of another element shows in that state of this one.
    bool WatchedByOthers(NodeId id, Trigger trigger) const;
    // The other elements whose states show some value of this one.
    std::vector<NodeId> WatchedBy(NodeId id) const;

    // The element is gone: forget it without writing anything to it. The elements that showed values in its states put
    // back what was underneath.
    void Forget(NodeId id, Access& access);

    bool Has(NodeId id) const { return m_elements.count(id) > 0; }
    size_t Count() const { return m_elements.size(); }

private:
    struct Shown {
        Value under;    // what the element had before a state changed the property
        Value showing;  // the state's value it shows now
    };
    struct Element {
        std::vector<HoverValue> values;
        std::map<std::string, Shown, std::less<>> shown;  // the properties a state changes now
    };

    static NodeId Watched(NodeId id, const HoverValue& value) { return value.watch ? value.watch : id; }
    static bool Shows(NodeId id, const Element& element, NodeId watched, std::optional<Trigger> trigger);
    // Writes what the states the element's values watch show, and puts back the properties none of them changes now.
    void Show(NodeId id, Element& element, Access& access) const;
    const HoverValue* Winner(NodeId id, const Element& element, std::string_view property) const;
    static void PutBack(NodeId id, Element& element, Access& access);
    // Forgets the states no value shows in any more, so an element followed again later starts outside them.
    void Prune();

    std::map<NodeId, Element> m_elements;
    std::set<std::pair<NodeId, Trigger>> m_on;  // the elements in each state, among those some value watches
};

}  // namespace ampify::theme
