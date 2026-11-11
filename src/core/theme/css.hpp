#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../sidebar_fold.hpp"

namespace ampify::theme {

// The CSS subset that themes use (user.css). Selectors name XAML elements:
//   Grid            every Grid (short type name, or the full name)
//   #NowPlayingBar  the element whose x:Name is NowPlayingBar
//   *               any element
//   A B             a B somewhere inside an A
//   A > B           a B whose direct parent is an A
//   A + B, A ~ B    a B right after an A among the children of one parent, or anywhere after it
// Parts can be combined: `Grid#Bar > TextBlock`. Structural pseudo-classes go on any part: :first-child, :last-child,
// :only-child, :nth-child(n), :nth-last-child(n), and :not(part) with one part made of a type, a #name and those.
// :has(selector) holds when an element inside matches the selector; `:has(> X)` wants a direct child.
// One state per selector, on any part:
//   Button:hover           a Button, while the pointer is over it (the values come back when it leaves)
//   Grid#Row:hover Button  a Button inside Grid#Row, while the pointer is over that Grid#Row
// `Grid#Bar::after` (or `::before`, or the older `:after`) ends a selector: see PseudoElement.
// Attributes and the other pseudo-classes are not supported.
//
// A part may also be a stable name, written as a class (`.sidebar`, `.card:hover`, `.playbar Button`): it stands for
// the path to that zone of Apple Music (builtins.hpp). `@import "ampify:<layout>";` brings in a ready-made layout at
// that place of the stylesheet, so the rules after it override it.
//
// One more block is read: `:root { --name: value; }` defines variables, which values use as `var(--name)` or
// `var(--name, fallback)`. The variables named --font-<role> describe the font roles (see fonts.hpp).
//
// `@media (max-width: 1200px) { ... }` holds rules that apply only while the window has that size: min-width,
// max-width, min-height and max-height, joined by `and`. `@keyframes name { ... }` describes an animation, which the
// `animation` property runs (animation.hpp). `@setting key { ... }` and `@setting key = value { ... }` hold rules
// that apply only while a setting the theme declares is on, or has that value (`key != value`: another value). One
// @setting may sit inside another: all their conditions must hold. Other at-rules are skipped whole.

// The condition of an @media block, on the window's size in device-independent pixels, bounds included.
struct MediaQuery {
    double minWidth = 0;
    double maxWidth = 1e9;
    double minHeight = 0;
    double maxHeight = 1e9;
    std::optional<bool> dark;  // (prefers-color-scheme: dark) or (light): the theme's scheme in use, not Windows'
    std::string text;  // as written, for the log

    // While Ampify keeps the sidebar folded at every width (apple.sidebar = hover), the two bounds of Apple's fold
    // follow the sidebar instead of the window.
    static constexpr double kSidebarFoldsAt = ampify::kSidebarFoldsAt;

    bool Matches(double width, double height, bool sidebarFolded = false) const {
        double max = maxWidth;
        if (sidebarFolded && maxWidth == kSidebarFoldsAt) max = 1e9;
        if (sidebarFolded && minWidth == kSidebarFoldsAt + 1) return false;
        return width >= minWidth && width <= max && height >= minHeight && height <= maxHeight;
    }
    bool MatchesScheme(bool darkScheme) const { return !dark || *dark == darkScheme; }
    bool operator==(const MediaQuery&) const = default;
};

// The condition of a @setting block. Without a value, it holds while the setting is true (a toggle that is on).
// With one, while the setting has that value, compared without case; `negated` (`key != value`) while it has another.
struct SettingCondition {
    std::string key;
    std::optional<std::string> value;
    bool negated = false;
};

enum class Combinator {
    Descendant,  // whitespace
    Child,       // >
    Adjacent,    // +, the sibling just before
    Sibling,     // ~, any sibling before
};

struct Compound {
    std::string type;  // empty means any element
    std::string name;  // without the '#', empty when absent
    int nth = 0;        // :nth-child(n), from 1 (:first-child is 1); 0 when absent
    int nthLast = 0;    // :nth-last-child(n), from 1 counting from the end; 0 when absent
    bool last = false;  // :last-child
    bool only = false;  // :only-child
    bool playing = false;  // :playing, the row of the song playing and its parts (NodeInfo::playing)
    bool checked = false;   // :checked, a toggle, check box or switch that is on (NodeInfo::checked)
    bool selected = false;  // :selected, a list row or sidebar entry that is selected (NodeInfo::selected)
    bool disabled = false;  // :disabled, a control that takes no input (NodeInfo::disabled)
    bool idle = false;      // :idle, nothing in the player (NodeInfo::idle)
    std::vector<Compound> negations;  // :not(...): parts the element must not match
    // :has(...): the parts of each relative selector, one of whose elements inside this one must match. The combinator
    // of the first part is how that part stands to this element: Child for `:has(> X)`, else Descendant.
    std::vector<std::vector<Compound>> has;
    bool counted = true;  // a negation that weighs in the specificity; not the internal :except() of a built-in name
    Combinator combinator = Combinator::Descendant;  // relation to the compound before it; unused for the first
};

// When the declarations of a rule show: always, or while an element of the selector is in a state: the one that
// receives them, or the one that matched the part the state is written on (Selector::statePart).
enum class Trigger {
    Always,
    Hover,   // :hover, the pointer is over the element
    Focus,   // :focus or :focus-within, the keyboard focus is in the element or one of its parts
    Active,  // :active, the element is pressed
    Current,  // :current, Apple shows the element as the current one: the lyrics line being sung, and its parts
};

struct StateClass {
    std::string_view name;
    Trigger trigger;
    size_t priority;  // 0 wins over 1 when several states hold (HoverTracker)
};

// The pseudo-classes that name a state, in the order the messages list them. :focus-within is :focus: the backend
// follows the focus of the element's parts too. The first one of a state is its name. Priority: what the user does,
// pressed before focused before hovered, then what Apple shows the element as.
inline constexpr StateClass kStateClasses[] = {
    {":hover", Trigger::Hover, 2},   {":focus", Trigger::Focus, 1},     {":focus-within", Trigger::Focus, 1},
    {":active", Trigger::Active, 0}, {":current", Trigger::Current, 3},
};

// Every state once, by priority, from the table above.
inline constexpr std::array<Trigger, 4> kStatesByPriority = [] {
    std::array<Trigger, 4> states{};
    for (const StateClass& state : kStateClasses) states[state.priority] = state.trigger;
    return states;
}();
static_assert(std::ranges::none_of(kStatesByPriority, [](Trigger state) { return state == Trigger::Always; }),
              "each priority of kStateClasses names one state");

// ":hover", ":focus", ":active" or ":current"; empty for Always.
const char* TriggerName(Trigger trigger);

// Every pseudo-class that names a state, for the messages: ":hover, :focus, ... and :current" with `last` " and ".
std::string StateList(std::string_view last);

// `::before` or `::after` at the end of a selector: the declarations describe a decorative image the engine puts into
// the panel the selector names (TreeBackend::SetDecor), behind its children or on top of them.
enum class PseudoElement {
    None,
    Before,
    After,
};

// "::before" or "::after"; empty for None.
const char* PseudoElementName(PseudoElement pseudo);

struct Selector {
    std::vector<Compound> parts;  // left to right; the last part is the element that receives the declarations
    std::string text;             // as written, for the log
    Trigger trigger = Trigger::Always;  // the declarations apply while the element of `statePart` is in that state
    size_t statePart = 0;               // the index in `parts` of the part the state is written on
    PseudoElement pseudo = PseudoElement::None;  // the declarations go to the panel's decorative image
};

struct Declaration {
    std::string property;  // as written, such as "background"
    std::string value;     // trimmed, without !important
    bool important = false;
    int line = 0;
};

struct Rule {
    bool root = false;  // a `:root` block: no selectors, its declarations are variables
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
    int line = 0;
    std::string origin;  // "ampify:<layout>" for a rule brought by @import, empty for the stylesheet's own
    std::optional<MediaQuery> media;  // the condition of the @media block around the rule
    std::vector<SettingCondition> settings;  // the conditions of the @setting blocks around the rule, all to hold
};

// One stop of a @keyframes block: `from`, `to` or percentages, several joined by commas, and its declarations.
struct KeyframeStop {
    std::vector<double> offsets;  // from 0 to 1: `from` is 0, `50%` is 0.5, `to` is 1
    std::vector<Declaration> declarations;
    int line = 0;
};

struct Keyframes {
    std::string name;
    std::vector<KeyframeStop> stops;
    int line = 0;
};

struct Stylesheet {
    std::vector<Rule> rules;
    std::vector<Keyframes> keyframes;  // in file order (`@-webkit-keyframes` counts as `@keyframes`)
    std::vector<std::string> problems;  // with the line number. A bad rule is skipped, the others still load.
};

// What the names of a stylesheet stand for, which its syntax does not say: the classes (`.sidebar`) and the layouts
// of `@import "ampify:<layout>"`. The zone table of Apple Music gives them (ZoneNames in builtins.hpp).
class StylesheetNames {
public:
    virtual ~StylesheetNames() = default;
    // The selectors `selector` stands for once its classes are replaced. Nothing, with `problem` set, when it cannot.
    virtual std::optional<std::vector<std::string>> Expand(std::string_view selector, std::string* problem) const = 0;
    // The CSS of a layout, or nothing for an unknown name.
    virtual std::optional<std::string> Layout(std::string_view name) const = 0;
    virtual std::vector<std::string> LayoutNames() const = 0;
};

Stylesheet ParseStylesheet(std::string_view css, const StylesheetNames& names);

// Parses one selector. On failure returns nothing and, when `problem` is given, writes the reason.
std::optional<Selector> ParseSelector(std::string_view text, std::string* problem = nullptr);

}  // namespace ampify::theme
