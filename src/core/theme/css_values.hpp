#pragma once

// The small readers every part of the CSS subset shares: the parser (css.hpp), the property conversions
// (properties.hpp), the animations (animation.hpp), the zone classes (builtins.hpp) and the theme loader. One copy of
// each, so a fix reaches every reader.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::theme {

// A character of a type name in a selector: `Grid`, `Microsoft.UI.Xaml.Controls.Grid`.
bool IsTypeChar(char c);

// The bracket that closes the one at `open`, brackets inside it included, or npos.
size_t ClosingBracket(std::string_view text, size_t open);

// `text` without one pair of matching quotes around it, "a" or 'a'; as it is when it has none.
std::string_view Unquoted(std::string_view text);

// The pieces of `text` between each `separator` outside quotes and brackets, as written: `url(a;b)` and "x;y" stay
// whole. For the syntax of a stylesheet, where a piece's place in the text matters.
std::vector<std::string_view> SplitTopLevel(std::string_view text, char separator);

// The pieces of `text` between each `separator` outside brackets, trimmed. A space as separator cuts at any white
// space; with `skipEmpty`, runs of separators make one break, as spaces do between words. For values.
std::vector<std::string_view> SplitArguments(std::string_view text, char separator = ',', bool skipEmpty = false);

// The words of `text` between white space, brackets or not: `rgba(0, 0)` is two words. For the values whose words
// hold no brackets (sides, origins, transition items, the arguments of one function).
std::vector<std::string_view> SplitWords(std::string_view text);

// The whole of `text` as a finite number, `12`, `-0.5`, `1e3`; nothing for anything else, `12px` included.
std::optional<double> ParseNumber(std::string_view text);

// Replaces each `var(--name)` or `var(--name, fallback)` of `value` with the variable (names are case-insensitive).
// Nothing when a variable is missing and has no fallback; `problem` then says which one.
std::optional<std::string> SubstituteVariables(std::string_view value, const std::map<std::string, std::string>& variables,
                                               std::string* problem = nullptr);

// Replaces each `calc(...)` of `value` with its result: numbers with an optional unit (`px`, `ms`...), + - * / and
// brackets, after the variables are in. Nothing when an expression cannot be computed; `problem` then says why.
std::optional<std::string> EvaluateCalc(std::string_view value, std::string* problem = nullptr);

// A declared value as the engine reads it: the variables in, then each calc() computed.
std::optional<std::string> ResolveValue(std::string_view value, const std::map<std::string, std::string>& variables,
                                        std::string* problem = nullptr);

}  // namespace ampify::theme
