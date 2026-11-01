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
// Parts can be combined: `Grid#Bar > TextBlock`. One pseudo-class is read, on the last part only:
//   Button:hover    a Button, while the pointer is over it (the values come back when it leaves)
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
// `animation` property runs (animation.hpp). Other at-rules are skipped whole.

// The condition of an @media block, on the window's size in device-independent pixels, bounds included.
struct MediaQuery {
    double minWidth = 0;
    double maxWidth = 1e9;
    double minHeight = 0;
    double maxHeight = 1e9;
    std::string text;  // as written, for the log

    bool Matches(double width, double height) const {
        return width >= minWidth && width <= maxWidth && height >= minHeight && height <= maxHeight;
    }
    bool operator==(const MediaQuery&) const = default;
};

enum class Combinator {
    Descendant,  // whitespace
    Child,       // >
};

struct Compound {
    std::string type;  // empty means any element
    std::string name;  // without the '#', empty when absent
    int nth = 0;        // :nth-child(n), from 1 (:first-child is 1); 0 when absent
    bool last = false;  // :last-child
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

struct Selector {
    std::vector<Compound> parts;  // left to right; the last part is the element that receives the declarations
    std::string text;             // as written, for the log
    Trigger trigger = Trigger::Always;  // a state on the last part: the declarations apply while the element is in it
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
