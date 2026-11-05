#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "css.hpp"

namespace ampify::theme {

// Everything the engine knows about the live interface goes through TreeBackend. The Windows backend
// implements it with XAML; the tests implement it with a fake tree. Nothing here is Windows-specific.
// Every call happens on the interface thread: the backend marshals work there if it must.

// An opaque identifier for one element, valid while the element lives. The backend picks it.
using NodeId = std::uint64_t;

enum class ValueKind {
    Color,       // "#AARRGGBB", upper case
    Image,       // absolute path of an image file, with '/' separators, then "|Fill", "|Uniform" or "|UniformToFill"
                 // when the theme chose how the image fills the element; kArtworkImage instead of a path for the
                 // artwork of the song playing
    Gradient,    // a linear gradient: "x1,y1,x2,y2;#AARRGGBB@offset;...", points relative to the element (0 to 1),
                 // at least two stops, offsets from 0 to 1 in order
    Acrylic,     // an acrylic brush: "#AARRGGBB;opacity", the tint and its opacity (0 to 1)
    Length,      // device-independent pixels, "18", or "NaN" for auto ("120%" in a theme's font size, which the engine
                 // turns into pixels before Write)
    Thickness,   // "left,top,right,bottom", such as "8,4,8,4"
    Visibility,  // "Visible" or "Collapsed"
    Number,      // a plain number, such as "0.5" or "700" for a font weight
    Text,        // a font family such as "Segoe UI Variable"
    Template,    // the XAML text of a ControlTemplate
    Keyword,     // the name of an enumeration member as XAML spells it, such as "RightToLeft" for FlowDirection
    Transform,   // a CompositeTransform: "scaleX,scaleY,rotation,translateX,translateY", rotation in degrees
    Point,       // a point relative to the element, "x,y" from 0 to 1, such as "0.5,0.5" for RenderTransformOrigin
    Shadow,      // a ThemeShadow raised by this depth in pixels (the Z of UIElement.Translation), such as "16"
    Transition,  // the animated properties and their duration in ms, "transform:200;opacity:150;background:300",
                 // empty for none
    Animation,   // the animations running on the element, as EncodeAnimations writes them (animation.hpp), empty for
                 // none
    Clip,        // a rectangle inset from the element's edges, "left,top,right,bottom" in pixels (negative reaches past
                 // them), that the element and its children are cut to; empty for no clip
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
    // Text made only of icon glyphs (private-use characters, as in Apple Music's badges and stars): its font is never
    // changed, or the icon would show as an empty box in the theme's font.
    bool icon = false;
    // The place of the element among its parent's children (from 0) and how many children the parent has, for
    // :nth-child, :first-child and :last-child. -1 and 0 when unknown: those selectors then match nothing.
    int position = -1;
    int siblings = 0;
    // In one of Apple's player windows (full screen, mini player), not the main window. Only a selector that names that
    // player's page or view reaches it (see Matches), so the main window's rules do not leak there.
    bool player = false;
    // A Panel whose Children Ampify may add to (Grid, StackPanel...): not the items host of a list, whose children
    // the list makes itself. Only a panel takes a ::before or ::after image (SetDecor).
    bool panel = false;
    // A list row that shows Apple's "now playing" bars (Grid#NowPlayingIndicator visible), and every element between the
    // row and those bars: what `:playing` matches.
    bool playing = false;
};

inline constexpr std::string_view kOrderProperty = "Order";
// `move-to: <selector>`: the element moves into the first element the selector matches (TreeBackend::Reparent). It is
// not a value of the element: the engine moves it once, takes it back on Restore, and moves it again when Apple
// rebuilds it.
inline constexpr std::string_view kMoveToProperty = "MoveTo";
// `checked-color`: the ToggleButtonForegroundChecked keys in a control's own resources, not a property of it.
inline constexpr std::string_view kCheckedForegroundProperty = "CheckedForeground";
// A control's template, given back before the control's other values (ThemeEngine::Restore).
inline constexpr std::string_view kTemplateProperty = "Template";

// The path of an Image value that stands for the artwork of the song playing (`background: artwork` in user.css).
inline constexpr std::string_view kArtworkImage = "artwork:";

// The original of a property the element had no value of its own for (TreeBackend::ReadOriginal).
inline const Value kNoOwnValue{ValueKind::Opaque, "no-own-value"};

// `content` of ::before and ::after: the image of a decor (Decor::image), never a property of an element.
inline constexpr std::string_view kContentProperty = "Content";

// The text of `content: bars(n)`: kBarsContent then the count, such as "bars:12". Bars that follow the music.
inline constexpr std::string_view kBarsContent = "bars:";
inline constexpr int kMinBars = 2;
inline constexpr int kMaxBars = 32;

// A decorative image a theme puts into a panel with ::before or ::after and `content: url(...)`, or bars that move
// with the music (`content: bars(n)`).
struct Decor {
    // The text of an Image value: absolute path, then "|Fill", "|Uniform" or "|UniformToFill"; or kBarsContent and
    // the count of bars.
    std::string image;
    // The other declarations, XAML name and value in name order (Width, Margin, Grid.Row, Animation...): those
    // AppliesToDecor accepts (properties.hpp).
    std::vector<std::pair<std::string, Value>> properties;

    bool operator==(const Decor&) const = default;
};

enum class DecorChange {
    Unchanged,  // already in place as asked (or nothing to take off)
    Applied,    // inserted, changed or taken off
    Refused,    // the element is gone, is not a panel, or refused the image
};

// The size of the window's content, in device-independent pixels.
struct ViewSize {
    double width = 0;
    double height = 0;
    bool sidebarFolded = false;  // Ampify keeps the sidebar folded at every width (MediaQuery::kSidebarFoldsAt)
};

// One value of a :hover, :focus or :active rule: shown while the element is in that state.
struct HoverValue {
    std::string property;  // XAML name, as for Read and Write
    Value value;
    Trigger trigger = Trigger::Hover;  // the state that shows it: :hover, :focus or :active

    bool operator==(const HoverValue&) const = default;
};

class TreeBackend {
public:
    virtual ~TreeBackend() = default;

    // Every element currently in the interface, parents before children.
    virtual std::vector<NodeInfo> Snapshot() = 0;

    // True for an element out of the interface that still exists: WinUI takes the entries of a menu out of its tree
    // when the menu closes, and puts the same ones back at the next opening. The engine keeps what it must give back
    // to such an element, and Restore writes to it. An element missing from Snapshot is gone otherwise.
    virtual bool Alive(NodeId id) {
        (void)id;
        return false;
    }

    // The current value of a XAML property, in the form Write accepts. Nothing when the element has no such property.
    // Besides the XAML properties, the engine uses one property of its own, kOrderProperty ("Order"): the position of
    // the element among the items of its parent (a Number, 0 first). Writing it moves the element to that position,
    // as NavigationView.MenuItems.Move or Panel.Children.Move would. Read gives nothing when the parent cannot reorder.
    virtual std::optional<Value> Read(NodeId id, std::string_view property) = 0;

    // False when the element is gone or does not support the property or the value.
    virtual bool Write(NodeId id, std::string_view property, const Value& value) = 0;

    // Moves the element into the children of `parent`, at `index` (from 0; -1 or past the end: last). The element keeps
    // its values. False when either element is gone or the parent holds no children. Restore moves it back.
    virtual bool Reparent(NodeId id, NodeId parent, int index) {
        (void)id;
        (void)parent;
        (void)index;
        return false;
    }

    // The value to put back when the theme comes off, read before the first write. kNoOwnValue when the element had
    // no value of its own (it showed its template's, its style's or its parent's): writing kNoOwnValue then clears the
    // property, and the element follows them again. Writing back the value it showed would freeze it there.
    virtual std::optional<Value> ReadOriginal(NodeId id, std::string_view property) { return Read(id, property); }

    // Application-level resources, such as brushes in the app dictionaries. Keys are the resource names.
    virtual std::vector<std::string> ResourceKeys() = 0;
    virtual std::optional<Value> ReadResource(std::string_view key) = 0;
    virtual bool WriteResource(std::string_view key, const Value& value) = 0;

    // Merges a resource dictionary (the XAML of a theme's resources.xaml) into the application. Empty text removes
    // the dictionary merged last. False when the XAML is refused.
    virtual bool SetDictionary(std::string_view xaml) = 0;

    // The values to show while the element is in a state (the pointer over it, the focus in it, pressed), in place of
    // the ones given before. The backend follows those states itself (once per element) and puts back what was there
    // when the element leaves them; meanwhile, Read and Write of a changed property see and change the value
    // underneath, so the engine never notices. An empty list takes the states off: the values underneath come back and
    // the backend stops following the element. False when the element is gone or cannot follow its states.
    // HoverTracker (hover.hpp) holds the logic a backend needs.
    virtual bool SetHover(NodeId id, const std::vector<HoverValue>& values) {
        (void)id;
        return values.empty();
    }

    // Puts the image of `decor` into the panel `id`, as ::before (first child, behind the others) or ::after (last
    // child, on top), or takes it off with a null `decor`. One image per panel and place: the same decor again changes
    // nothing, another one replaces it where it stands. The image never takes the pointer and never appears in
    // Snapshot, so no selector reaches it. Called on every pass: an image the app took out goes back in.
    virtual DecorChange SetDecor(NodeId id, PseudoElement slot, const Decor* decor) {
        (void)id;
        (void)slot;
        return decor ? DecorChange::Refused : DecorChange::Unchanged;
    }

    // The window's size, for @media rules. Nothing when unknown: those rules then apply nowhere.
    virtual std::optional<ViewSize> WindowSize() { return std::nullopt; }

    // The @media conditions of the theme, given on each pass: when the window's size makes one of them start or stop
    // matching, the backend asks for a pass, as it does for a new page.
    virtual void WatchSize(const std::vector<MediaQuery>& queries) { (void)queries; }
};

}  // namespace ampify::theme
