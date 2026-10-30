#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amk::theme {

// Everything the engine knows about the live interface goes through TreeBackend. The Windows backend
// implements it with XAML; the tests implement it with a fake tree. Nothing here is Windows-specific.
// Every call happens on the interface thread: the backend marshals work there if it must.

// An opaque identifier for one element, valid while the element lives. The backend picks it.
using NodeId = std::uint64_t;

enum class ValueKind {
    Color,       // "#AARRGGBB", upper case
    Image,       // absolute path of an image file, with '/' separators
    Length,      // device-independent pixels, "18", or "NaN" for auto
    Thickness,   // "left,top,right,bottom", such as "8,4,8,4"
    Visibility,  // "Visible" or "Collapsed"
    Number,      // a plain number, such as "0.5" or "700" for a font weight
    Text,        // a font family such as "Segoe UI Variable"
    Template,    // the XAML text of a ControlTemplate
    Keyword,     // the name of an enumeration member as XAML spells it, such as "RightToLeft" for FlowDirection
    Opaque,      // a token that Read gave back for an object the engine cannot express as text; Write restores it
};

struct Value {
    ValueKind kind = ValueKind::Text;
    std::string text;

    bool operator==(const Value&) const = default;
};

// One element of the snapshot. `parent` is the index of the visual parent in the same snapshot, or -1.
// Snapshot lists the parents before their children.
struct NodeInfo {
    NodeId id = 0;
    std::string type;      // short name, such as "Grid"
    std::string fullType;  // such as "Microsoft.UI.Xaml.Controls.Grid", may be empty
    std::string name;      // x:Name, may be empty
    int parent = -1;
    int depth = 0;
};

inline constexpr std::string_view kOrderProperty = "Order";

class TreeBackend {
public:
    virtual ~TreeBackend() = default;

    // Every element currently in the interface, parents before children.
    virtual std::vector<NodeInfo> Snapshot() = 0;

    // The current value of a XAML property, in the form Write accepts. Nothing when the element has no such property.
    // Besides the XAML properties, the engine uses one property of its own, kOrderProperty ("Order"): the position of
    // the element among the items of its parent (a Number, 0 first). Writing it moves the element to that position,
    // as NavigationView.MenuItems.Move or Panel.Children.Move would. Read gives nothing when the parent cannot reorder.
    virtual std::optional<Value> Read(NodeId id, std::string_view property) = 0;

    // False when the element is gone or does not support the property or the value.
    virtual bool Write(NodeId id, std::string_view property, const Value& value) = 0;

    // Application-level resources, such as brushes in the app dictionaries. Keys are the resource names.
    virtual std::vector<std::string> ResourceKeys() = 0;
    virtual std::optional<Value> ReadResource(std::string_view key) = 0;
    virtual bool WriteResource(std::string_view key, const Value& value) = 0;

    // Merges a resource dictionary (the XAML of a theme's resources.xaml) into the application. Empty text removes
    // the dictionary merged last. False when the XAML is refused.
    virtual bool SetDictionary(std::string_view xaml) = 0;
};

}  // namespace amk::theme
