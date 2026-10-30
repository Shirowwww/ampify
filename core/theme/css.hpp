#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amk::theme {

// The CSS subset that themes use (user.css). Selectors name XAML elements:
//   Grid            every Grid (short type name, or the full name)
//   #NowPlayingBar  the element whose x:Name is NowPlayingBar
//   *               any element
//   A B             a B somewhere inside an A
//   A > B           a B whose direct parent is an A
// Parts can be combined: `Grid#Bar > TextBlock`. Attributes and pseudo-classes are not supported yet.
//
// One more block is read: `:root { --name: value; }` defines variables, which values use as `var(--name)` or
// `var(--name, fallback)`. The variables named --font-<role> describe the font roles (see fonts.hpp).

enum class Combinator {
    Descendant,  // whitespace
    Child,       // >
};

struct Compound {
    std::string type;  // empty means any element
    std::string name;  // without the '#', empty when absent
    Combinator combinator = Combinator::Descendant;  // relation to the compound before it; unused for the first
};

struct Selector {
    std::vector<Compound> parts;  // left to right; the last part is the element that receives the declarations
    std::string text;             // as written, for the journal
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
};

struct Stylesheet {
    std::vector<Rule> rules;
    std::vector<std::string> problems;  // French, with the line number. A bad rule is skipped, the others still load.
};

Stylesheet ParseStylesheet(std::string_view css);

// Parses one selector. On failure returns nothing and, when `problem` is given, writes the reason.
std::optional<Selector> ParseSelector(std::string_view text, std::string* problem = nullptr);

}  // namespace amk::theme
