#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tree.hpp"

namespace ampify::theme {

// The :hover values of the elements, for a backend (TreeBackend::SetHover). Portable, so the tests drive the same
// logic as the Windows backend: the backend only follows the pointer and calls Enter and Exit.
//
// While the pointer is over an element, each hovered property shows the hover value and the tracker keeps the value
// underneath. Read gives that value and Write changes it, without touching the element: the engine, which runs on a
// timer, sees the values it wrote and never fights the hover. Exit writes the values underneath back.
class HoverTracker {
public:
    // The element itself, as the backend reads and writes it without the hover.
    class Access {
    public:
        virtual ~Access() = default;
        virtual std::optional<Value> ReadRaw(NodeId id, std::string_view property) = 0;
        virtual bool WriteRaw(NodeId id, std::string_view property, const Value& value) = 0;
    };

    enum class Change {
        None,     // same values as before
        Added,    // a new element: the backend starts following the pointer
        Updated,  // other values for an element already followed
        Removed,  // the hover is off: the backend stops following the pointer
    };

    Change Set(NodeId id, const std::vector<HoverValue>& values, Access& access);
    void Enter(NodeId id, Access& access);
    void Exit(NodeId id, Access& access);

    // Read and Write through the hover. Outside a hover (or for a property the hover does not change), the element.
    std::optional<Value> Read(NodeId id, std::string_view property, Access& access);
    bool Write(NodeId id, std::string_view property, const Value& value, Access& access);

    // The element is gone: forget it without writing anything.
    void Forget(NodeId id) { m_elements.erase(id); }

    bool Has(NodeId id) const { return m_elements.count(id) > 0; }
    bool Hovered(NodeId id) const;
    size_t Count() const { return m_elements.size(); }

private:
    struct Element {
        std::vector<HoverValue> values;
        std::map<std::string, Value, std::less<>> under;  // while hovered: the value underneath each changed property
        bool hovered = false;
    };

    std::map<NodeId, Element> m_elements;
};

}  // namespace ampify::theme
