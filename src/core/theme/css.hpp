#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
// A state goes on the last part only:
//   Button:hover    a Button, while the pointer is over it (the values come back when it leaves)
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
// that apply only while a setting the theme declares is on, or has that value. Other at-rules are skipped whole.

// The condition of an @media block, on the window's size in device-independent pixels, bounds included.
struct MediaQuery {
    double minWidth = 0;
    double maxWidth = 1e9;
    double minHeight = 0;
    double maxHeight = 1e9;
    std::optional<bool> dark;  // (prefers-color-scheme: dark) or (light): the theme's scheme in use, not Windows'
    std::string text;  // as written, for the log

    bool Matches(double width, double height) const {
        return width >= minWidth && width <= maxWidth && height >= minHeight && height <= maxHeight;
    }
    bool MatchesScheme(bool darkScheme) const { return !dark || *dark == darkScheme; }
    bool operator==(const MediaQuery&) const = default;
};

// The condition of a @setting block. Without a value, it holds while the setting is true (a toggle that is on).
// With one, while the setting has that value, compared without case.
struct SettingCondition {
    std::string key;
    std::optional<std::string> value;
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
    std::vector<Compound> negations;  // :not(...): parts the element must not match
    bool counted = true;  // a negation that weighs in the specificity; not the internal :except() of a built-in name
    Combinator combinator = Combinator::Descendant;  // relation to the compound before it; unused for the first
};

// When the declarations of a rule show: always, or while the element that receives them is in a state.
enum class Trigger {
    Always,
    Hover,   // :hover, the pointer is over the element
    Focus,   // :focus or :focus-within, the keyboard focus is in the element or one of its parts
    Active,  // :active, the element is pressed
};

// ":hover", ":focus" or ":active"; empty for Always.
const char* TriggerName(Trigger trigger);

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
    Trigger trigger = Trigger::Always;  // a state on the last part: the declarations apply while the element is in it
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
    std::optional<SettingCondition> setting;  // the condition of the @setting block around the rule
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

Stylesheet ParseStylesheet(std::string_view css);

// Parses one selector. On failure returns nothing and, when `problem` is given, writes the reason.
std::optional<Selector> ParseSelector(std::string_view text, std::string* problem = nullptr);

}  // namespace ampify::theme
