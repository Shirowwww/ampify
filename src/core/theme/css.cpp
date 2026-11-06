#include "css.hpp"

#include <cctype>
#include <charconv>
#include <iterator>

#include "../text_util.hpp"
#include "css_values.hpp"

namespace ampify::theme {

namespace {

bool IsSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool IsNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

// Comments become spaces, so the offsets and the line numbers of the rest stay exact.
std::string WithoutComments(std::string_view css) {
    std::string text(css);
    for (size_t i = 0; i + 1 < text.size(); i++) {
        if (text[i] != '/' || text[i + 1] != '*') continue;

        size_t end = text.find("*/", i + 2);
        size_t stop = end == std::string::npos ? text.size() : end + 2;
        for (size_t j = i; j < stop; j++) {
            if (text[j] != '\n') text[j] = ' ';
        }
        i = stop - 1;
    }
    return text;
}

int LineOf(std::string_view text, size_t offset) {
    int line = 1;
    for (size_t i = 0; i < offset && i < text.size(); i++) {
        if (text[i] == '\n') line++;
    }
    return line;
}


std::optional<Trigger> StateOf(std::string_view pseudo) {
    for (const StateClass& state : kStateClasses) {
        if (state.name.substr(1) == pseudo) return state.trigger;
    }
    return std::nullopt;
}

std::optional<PseudoElement> PseudoElementOf(std::string_view pseudo) {
    if (pseudo == "before") return PseudoElement::Before;
    if (pseudo == "after") return PseudoElement::After;
    return std::nullopt;
}

// `:nth-child(n)` or `:nth-last-child(n)` from the bracket at `i` into `target`; moves `i` past it. The reason when
// it cannot be read.
std::optional<std::string> ReadNthChild(std::string_view s, size_t& i, int& target, const std::string& pseudo,
                                        const std::string& text) {
    size_t close = s.find(')', i);
    if (i >= s.size() || s[i] != '(' || close == std::string_view::npos) {
        return ":" + pseudo + " needs a number in brackets, like :" + pseudo + "(2), in \"" + text + "\"";
    }
    std::string_view digits = s.substr(i + 1, close - i - 1);
    int n = 0;
    for (char c : digits) n = (c >= '0' && c <= '9' && n < 10000) ? n * 10 + (c - '0') : -1;
    if (digits.empty() || n < 1) return ":" + pseudo + " counts from 1, like :" + pseudo + "(2), in \"" + text + "\"";
    target = n;
    i = close + 1;
    return std::nullopt;
}

std::optional<std::string> ReadCompound(std::string_view s, size_t& i, Compound& compound, Selector& selector);

// `:not(<part>)` from the bracket at `i`; moves `i` past it. The part is one compound: a type, a #name and structural
// pseudo-classes, no state and no pseudo-element. The reason when it cannot be read.
std::optional<std::string> ReadNegation(std::string_view s, size_t& i, Compound& compound, const std::string& text,
                                        bool counted) {
    size_t close = i < s.size() && s[i] == '(' ? ClosingBracket(s, i) : std::string_view::npos;
    const std::string shape = ":not() takes one part made of a type, a #name and structural pseudo-classes, like "
                              ":not(:first-child) or :not(Grid#Bar), in \"" + text + "\"";
    if (close == std::string_view::npos) return shape;
    std::string_view inside = TrimSpace(s.substr(i + 1, close - i - 1));
    if (inside.empty()) return shape;

    Selector scratch;
    scratch.text = text;
    Compound negated;
    size_t at = 0;
    if (std::optional<std::string> problem = ReadCompound(inside, at, negated, scratch)) return problem;
    if (at != inside.size() || scratch.trigger != Trigger::Always || scratch.pseudo != PseudoElement::None) return shape;
    negated.counted = counted;
    compound.negations.push_back(std::move(negated));
    i = close + 1;
    return std::nullopt;
}

// The structural pseudo-classes, which go on any part. Nothing when `pseudo` is none of them; else the reason it
// cannot be read, empty when it was.
std::optional<std::string> ReadStructural(const std::string& pseudo, std::string_view s, size_t& i, Compound& compound,
                                          const std::string& text) {
    if (pseudo == "first-child") {
        compound.nth = 1;
    } else if (pseudo == "last-child") {
        compound.last = true;
    } else if (pseudo == "only-child") {
        compound.only = true;
    } else if (pseudo == "playing") {
        compound.playing = true;
    } else if (pseudo == "nth-child" || pseudo == "nth-last-child") {
        int& target = pseudo == "nth-child" ? compound.nth : compound.nthLast;
        return ReadNthChild(s, i, target, pseudo, text).value_or("");
    } else if (pseudo == "not" || pseudo == "except") {
        // :except() is :not() without weight, for the exclusions inside a built-in name (builtins.cpp).
        return ReadNegation(s, i, compound, text, pseudo == "not").value_or("");
    } else {
        return std::nullopt;
    }
    return std::string();
}

// One pseudo-class or pseudo-element from the colon at `i`; moves `i` past it. The structural ones and one state
// (:hover, :focus, :active, :current) go on any part, and ::before or ::after ends the selector. The reason when it
// cannot be read.
std::optional<std::string> ReadPseudo(std::string_view s, size_t& i, Compound& compound, Selector& selector) {
    const bool element = i + 1 < s.size() && s[i + 1] == ':';
    size_t start = i + (element ? 2 : 1);
    i = start;
    while (i < s.size() && IsNameChar(s[i])) i++;
    std::string written(s.substr(start, i - start));
    std::string pseudo = ToLowerAscii(written);
    if (selector.pseudo != PseudoElement::None) {
        return "\"" + std::string(PseudoElementName(selector.pseudo)) + "\" must end the selector \"" + selector.text +
               "\"";
    }
    if (std::optional<PseudoElement> decor = PseudoElementOf(pseudo)) {
        if (selector.trigger != Trigger::Always) {
            return std::string(TriggerName(selector.trigger)) + " before ::" + pseudo + " is not supported in \"" +
                   selector.text + "\": a decorative image has no state of its own";
        }
        selector.pseudo = *decor;
        return std::nullopt;
    }
    if (element) {
        return "pseudo-element \"::" + written + "\" is not supported in \"" + selector.text +
               "\" (::before and ::after are)";
    }
    if (std::optional<std::string> structural = ReadStructural(pseudo, s, i, compound, selector.text)) {
        if (structural->empty()) return std::nullopt;
        return structural;
    }
    if (std::optional<Trigger> state = StateOf(pseudo)) {
        if (selector.trigger != Trigger::Always) {
            return "only one of " + StateList(" and ") + " is allowed in \"" + selector.text + "\"";
        }
        selector.trigger = *state;
        selector.statePart = selector.parts.size();  // the part being read, pushed once it is complete
        return std::nullopt;
    }
    return "pseudo-class \":" + written + "\" is not supported in \"" + selector.text +
           "\" (" + StateList(", ") + ", :first-child, :last-child, :only-child, :nth-child(n), :nth-last-child(n), "
           ":playing, :not(...), ::before and ::after are)";
}

// One part of a selector from `i`: a type or `*`, a #name, then pseudo-classes. Moves `i` past what it read.
std::optional<std::string> ReadCompound(std::string_view s, size_t& i, Compound& compound, Selector& selector) {
    if (i < s.size() && s[i] == '*') {
        i++;
    } else {
        size_t start = i;
        while (i < s.size() && IsTypeChar(s[i])) i++;
        compound.type = std::string(s.substr(start, i - start));
    }
    if (i < s.size() && s[i] == '#') {
        i++;
        size_t start = i;
        while (i < s.size() && IsNameChar(s[i])) i++;
        compound.name = std::string(s.substr(start, i - start));
        if (compound.name.empty()) return "name missing after \"#\" in \"" + selector.text + "\"";
    }
    while (i < s.size() && s[i] == ':') {
        if (std::optional<std::string> reason = ReadPseudo(s, i, compound, selector)) return reason;
    }
    return std::nullopt;
}

std::optional<Combinator> CombinatorOf(char c) {
    if (c == '>') return Combinator::Child;
    if (c == '+') return Combinator::Adjacent;
    if (c == '~') return Combinator::Sibling;
    return std::nullopt;
}

std::vector<Declaration> ParseDeclarations(std::string_view body, size_t bodyOffset, const std::string& clean,
                                           std::vector<std::string>& problems) {
    std::vector<Declaration> declarations;
    for (std::string_view piece : SplitTopLevel(body, ';')) {
        std::string_view text = TrimSpace(piece);
        if (text.empty()) continue;

        // The text is a view into `body`, so its distance from the start gives its exact offset in the file.
        size_t offset = bodyOffset + static_cast<size_t>(text.data() - body.data());
        int line = LineOf(clean, offset);
        size_t colon = text.find(':');
        if (colon == std::string_view::npos) {
            problems.push_back("line " + std::to_string(line) + ": declaration without \":\" ignored");
            continue;
        }

        Declaration declaration;
        declaration.property = std::string(TrimSpace(text.substr(0, colon)));
        declaration.line = line;

        std::string_view value = TrimSpace(text.substr(colon + 1));
        const std::string_view suffix = "!important";
        if (value.size() >= suffix.size() && ToLowerAscii(value.substr(value.size() - suffix.size())) == suffix) {
            declaration.important = true;
            value = TrimSpace(value.substr(0, value.size() - suffix.size()));
        }
        declaration.value = std::string(value);

        if (declaration.property.empty() || declaration.value.empty()) {
            problems.push_back("line " + std::to_string(line) + ": incomplete declaration ignored");
            continue;
        }
        declarations.push_back(std::move(declaration));
    }
    return declarations;
}

// The brace that closes the block opened at `open`, braces inside it included, or npos.
size_t MatchingBrace(std::string_view text, size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); i++) {
        if (text[i] == '{') depth++;
        if (text[i] == '}' && --depth == 0) return i;
    }
    return std::string_view::npos;
}

// Reads one `(min-width: 600px)` of an @media query into `query`.
bool ReadMediaFeature(std::string_view feature, MediaQuery& query, std::string& problem) {
    size_t colon = feature.find(':');
    std::string name = ToLowerAscii(TrimSpace(feature.substr(0, colon)));
    std::string_view value = colon == std::string_view::npos ? std::string_view() : TrimSpace(feature.substr(colon + 1));
    if (name == "prefers-color-scheme") {
        std::string scheme = ToLowerAscii(value);
        if (scheme != "dark" && scheme != "light") {
            problem = "@media (" + std::string(TrimSpace(feature)) + ") takes dark or light";
            return false;
        }
        query.dark = scheme == "dark";
        return true;
    }
    if (value.size() > 2 && ToLowerAscii(value.substr(value.size() - 2)) == "px") value = TrimSpace(value.substr(0, value.size() - 2));
    double number = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    double* bound = name == "min-width" ? &query.minWidth : name == "max-width" ? &query.maxWidth
                  : name == "min-height" ? &query.minHeight : name == "max-height" ? &query.maxHeight : nullptr;
    if (!bound) {
        problem = "@media (" + std::string(TrimSpace(feature)) + ") is not supported: min-width, max-width, min-height, "
                  "max-height and prefers-color-scheme are";
        return false;
    }
    if (value.empty() || error != std::errc() || end != value.data() + value.size()) {
        problem = "@media (" + std::string(TrimSpace(feature)) + ") needs a size in pixels";
        return false;
    }
    *bound = number;
    return true;
}

// The condition of `@media <text>`: an optional `screen` or `all`, then features joined by `and`.
std::optional<MediaQuery> ParseMediaQuery(std::string_view text, std::string& problem) {
    MediaQuery query;
    query.text = std::string(TrimSpace(text));
    std::string lowered = ToLowerAscii(text);
    size_t pos = 0;
    while (pos < lowered.size()) {
        if (IsSpace(lowered[pos])) {
            pos++;
            continue;
        }
        if (lowered[pos] == '(') {
            size_t close = lowered.find(')', pos);
            if (close == std::string::npos) {
                problem = "@media " + query.text + " is missing a \")\"";
                return std::nullopt;
            }
            if (!ReadMediaFeature(std::string_view(text).substr(pos + 1, close - pos - 1), query, problem)) return std::nullopt;
            pos = close + 1;
            continue;
        }
        size_t end = pos;
        while (end < lowered.size() && !IsSpace(lowered[end]) && lowered[end] != '(') end++;
        std::string word = lowered.substr(pos, end - pos);
        if (word != "and" && word != "screen" && word != "all" && word != "only") {
            problem = "@media " + query.text + " is not supported: only sizes and the scheme are read, such as (max-width: 1200px)";
            return std::nullopt;
        }
        pos = end;
    }
    return query;
}

// The offsets of a keyframe stop, `from`, `to` and percentages joined by commas, from 0 to 1. Nothing when a part is
// none of those.
std::optional<std::vector<double>> StopOffsets(std::string_view text) {
    std::vector<double> offsets;
    for (std::string_view piece : SplitTopLevel(text, ',')) {
        std::string word = ToLowerAscii(TrimSpace(piece));
        if (word == "from" || word == "to") {
            offsets.push_back(word == "to" ? 1 : 0);
            continue;
        }
        if (word.size() < 2 || word.back() != '%') return std::nullopt;
        double percent = 0;
        const char* last = word.data() + word.size() - 1;
        auto [end, error] = std::from_chars(word.data(), last, percent);
        if (error != std::errc() || end != last || percent < 0 || percent > 100) return std::nullopt;
        offsets.push_back(percent / 100);
    }
    if (offsets.empty()) return std::nullopt;
    return offsets;
}

// The stops of `@keyframes <name> { ... }`, between its braces at `open` and `close`. A stop it cannot read is reported
// and skipped. Nothing without a name.
std::optional<Keyframes> ParseKeyframes(std::string_view name, size_t open, size_t close, int line,
                                        const std::string& clean, std::vector<std::string>& problems) {
    name = Unquoted(TrimSpace(name));
    if (name.empty() || name.find_first_of(" \t\r\n\"'(),;") != std::string_view::npos) {
        problems.push_back("line " + std::to_string(line) + ": @keyframes needs a name, such as @keyframes spin, ignored");
        return std::nullopt;
    }
    Keyframes block;
    block.name = std::string(name);
    block.line = line;
    size_t pos = open + 1;
    while (pos < close) {
        if (IsSpace(clean[pos])) {
            pos++;
            continue;
        }
        const std::string where = "line " + std::to_string(LineOf(clean, pos)) + ": ";
        size_t brace = clean.find('{', pos);
        size_t end = brace < close ? clean.find('}', brace) : std::string::npos;  // a stop holds declarations only
        if (end == std::string::npos || end > close) {
            problems.push_back(where + "a stop of @keyframes " + block.name + " has no { }, ignored");
            break;
        }
        std::string_view selector = TrimSpace(std::string_view(clean).substr(pos, brace - pos));
        std::string_view body = std::string_view(clean).substr(brace + 1, end - brace - 1);
        pos = end + 1;
        std::optional<std::vector<double>> offsets = StopOffsets(selector);
        if (!offsets) {
            problems.push_back(where + "stop \"" + std::string(selector) + "\" of @keyframes " + block.name +
                               " is not from, to or a percentage from 0% to 100%, ignored");
            continue;
        }
        KeyframeStop stop;
        stop.offsets = std::move(*offsets);
        stop.line = LineOf(clean, brace);
        stop.declarations = ParseDeclarations(body, brace + 1, clean, problems);
        block.stops.push_back(std::move(stop));
    }
    return block;
}

// The blocks open around the place being read: one @media at most and any @setting, in the order they opened.
struct Scope {
    std::optional<MediaQuery> media;
    std::vector<SettingCondition> settings;  // innermost last
    std::vector<bool> blocks;  // true for a @setting block, false for a @media block
};

// `lowered` starts with the at-keyword, followed by a space, a bracket or nothing.
bool IsAtKeyword(const std::string& lowered, std::string_view keyword) {
    return lowered.starts_with(keyword) &&
           (lowered.size() == keyword.size() || IsSpace(lowered[keyword.size()]) || lowered[keyword.size()] == '(');
}

// The condition of `@setting <key>`, `@setting <key> = <value>` or `@setting <key> != <value>`, the value quoted or not.
std::optional<SettingCondition> ParseSettingCondition(std::string_view text, std::string& problem) {
    SettingCondition condition;
    size_t equals = text.find("!=");
    size_t operatorSize = 2;
    if (equals != std::string_view::npos) {
        condition.negated = true;
    } else {
        equals = text.find('=');
        operatorSize = 1;
    }
    condition.key = std::string(TrimSpace(text.substr(0, equals)));
    if (!IsSettingKey(condition.key)) {
        problem = "@setting needs a setting key such as hide-radio, written [a-z0-9-]";
        return std::nullopt;
    }
    if (equals == std::string_view::npos) return condition;
    std::string_view value = Unquoted(TrimSpace(text.substr(equals + operatorSize)));
    if (value.empty()) {
        problem = "@setting " + condition.key + (condition.negated ? " != " : " = ") + "needs a value";
        return std::nullopt;
    }
    condition.value = std::string(value);
    return condition;
}

// Opens a @setting block: its rules then carry the condition, with those of the blocks around it, until its brace
// closes. Returns where reading goes on.
size_t OpenSettingBlock(std::string_view head, size_t open, size_t close, const std::string& where, Scope& scope,
                        std::vector<std::string>& problems) {
    std::string problem;
    std::optional<SettingCondition> condition = ParseSettingCondition(head.substr(8), problem);
    if (!condition) {
        problems.push_back(where + problem + ", ignored");
        return close + 1;
    }
    scope.settings.push_back(std::move(*condition));
    scope.blocks.push_back(true);
    return open + 1;
}

// An at-rule with a block, its head read up to `open`: @media and @setting open a block of rules with a condition,
// @keyframes adds to `keyframes`, any other is skipped whole. Returns where reading goes on.
size_t ReadAtRule(std::string_view head, size_t open, int line, const std::string& clean, Scope& scope,
                  std::vector<Keyframes>& keyframes, std::vector<std::string>& problems) {
    const std::string where = "line " + std::to_string(line) + ": ";
    size_t close = MatchingBrace(clean, open);
    if (close == std::string_view::npos) {
        problems.push_back(where + "closing brace missing");
        return clean.size();
    }
    std::string lowered = ToLowerAscii(head);
    for (std::string_view keyword : {std::string_view("@keyframes"), std::string_view("@-webkit-keyframes")}) {
        if (!lowered.starts_with(keyword) || (lowered.size() > keyword.size() && !IsSpace(lowered[keyword.size()]))) {
            continue;
        }
        if (scope.media || !scope.settings.empty()) {
            problems.push_back(where + "@keyframes inside @" + (scope.media ? "media" : "setting") +
                               " is not supported, ignored");
        } else if (std::optional<Keyframes> block =
                       ParseKeyframes(head.substr(keyword.size()), open, close, line, clean, problems)) {
            keyframes.push_back(std::move(*block));
        }
        return close + 1;
    }
    if (IsAtKeyword(lowered, "@setting")) return OpenSettingBlock(head, open, close, where, scope, problems);
    if (!IsAtKeyword(lowered, "@media")) {
        problems.push_back(where + "rule \"" + std::string(head) + "\" is not supported, ignored");
        return close + 1;
    }
    if (scope.media) {
        problems.push_back(where + "@media inside @media is not supported, ignored");
        return close + 1;
    }
    std::string problem;
    std::optional<MediaQuery> query = ParseMediaQuery(head.substr(6), problem);
    if (!query) {
        problems.push_back(where + problem + ", ignored");
        return close + 1;
    }
    scope.media = std::move(query);
    scope.blocks.push_back(false);
    return open + 1;
}

constexpr std::string_view kBuiltinPrefix = "ampify:";

// The layout named by `@import "ampify:<name>"` or `@import url(ampify:<name>)`, or nothing for another statement.
std::optional<std::string> BuiltinImport(std::string_view statement) {
    const std::string_view keyword = "@import";
    if (ToLowerAscii(statement.substr(0, keyword.size())) != keyword) return std::nullopt;

    std::string_view target = TrimSpace(statement.substr(keyword.size()));
    if (ToLowerAscii(target.substr(0, 4)) == "url(" && target.ends_with(')')) {
        target = TrimSpace(target.substr(4, target.size() - 5));
    }
    target = Unquoted(target);
    if (ToLowerAscii(target.substr(0, kBuiltinPrefix.size())) != kBuiltinPrefix) return std::nullopt;
    return ToLowerAscii(target.substr(kBuiltinPrefix.size()));
}

std::string LayoutList(const StylesheetNames& names) {
    std::string list;
    for (const std::string& name : names.LayoutNames()) list += (list.empty() ? "" : ", ") + name;
    return list;
}

// The selectors of a rule head, each class replaced by the path it stands for. Nothing, with `problem` set, when one
// of them is wrong: the whole rule is then skipped, as in CSS.
std::optional<std::vector<Selector>> ParseSelectorList(std::string_view head, const StylesheetNames& names,
                                                     std::string& problem) {
    std::vector<Selector> selectors;
    for (std::string_view piece : SplitTopLevel(head, ',')) {
        std::string_view written = TrimSpace(piece);
        std::optional<std::vector<std::string>> expanded = names.Expand(written, &problem);
        if (!expanded) return std::nullopt;
        for (const std::string& text : *expanded) {
            std::optional<Selector> selector = ParseSelector(text, &problem);
            if (!selector) return std::nullopt;
            selector->text = std::string(written);  // the log shows the class, not the long path
            selectors.push_back(std::move(*selector));
        }
    }
    return selectors;
}

}  // namespace

const char* TriggerName(Trigger trigger) {
    for (const StateClass& state : kStateClasses) {
        if (state.trigger == trigger) return state.name.data();
    }
    return "";
}

std::string StateList(std::string_view last) {
    std::string list;
    for (size_t i = 0; i < std::size(kStateClasses); i++) {
        if (i > 0) list += i + 1 == std::size(kStateClasses) ? last : std::string_view(", ");
        list += kStateClasses[i].name;
    }
    return list;
}

const char* PseudoElementName(PseudoElement pseudo) {
    switch (pseudo) {
        case PseudoElement::Before: return "::before";
        case PseudoElement::After: return "::after";
        case PseudoElement::None: break;
    }
    return "";
}

std::optional<Selector> ParseSelector(std::string_view text, std::string* problem) {
    Selector selector;
    selector.text = std::string(TrimSpace(text));
    std::string_view s = selector.text;

    auto fail = [&](const std::string& reason) -> std::optional<Selector> {
        if (problem) *problem = reason;
        return std::nullopt;
    };

    if (s.empty()) return fail("empty selector");

    Combinator pending = Combinator::Descendant;
    char sawCombinator = 0;  // the combinator read since the last part, waiting for the part after it
    size_t i = 0;
    while (i < s.size()) {
        if (IsSpace(s[i])) {
            i++;
            continue;
        }

        if (selector.pseudo != PseudoElement::None) {
            return fail("\"" + std::string(PseudoElementName(selector.pseudo)) + "\" must end the selector \"" +
                        selector.text + "\"");
        }

        if (std::optional<Combinator> combinator = CombinatorOf(s[i])) {
            if (selector.parts.empty() || sawCombinator) {
                return fail("misplaced \"" + std::string(1, s[i]) + "\" in \"" + selector.text + "\"");
            }
            pending = *combinator;
            sawCombinator = s[i];
            i++;
            continue;
        }

        Compound compound;
        if (std::optional<std::string> reason = ReadCompound(s, i, compound, selector)) return fail(*reason);

        // Nothing was read (for example a stray `@`): the check below reports it, so the loop always advances.
        if (i < s.size() && !IsSpace(s[i]) && !CombinatorOf(s[i])) {
            return fail("character \"" + std::string(1, s[i]) + "\" is not supported in \"" + selector.text + "\"");
        }

        compound.combinator = selector.parts.empty() ? Combinator::Descendant : pending;
        selector.parts.push_back(std::move(compound));
        pending = Combinator::Descendant;
        sawCombinator = 0;
    }

    if (sawCombinator) {
        return fail("\"" + std::string(1, sawCombinator) + "\" has no element after it in \"" + selector.text + "\"");
    }
    return selector;
}

namespace {

// A statement without a block: `@import "ampify:<layout>";` brings the layout's rules in, under the blocks around it.
void ReadStatement(std::string_view statement, int line, const Scope& scope, const StylesheetNames& names,
                   Stylesheet& sheet) {
    std::optional<std::string> layout = BuiltinImport(statement);
    if (!layout) {
        sheet.problems.push_back("line " + std::to_string(line) + ": rule \"" + std::string(statement) +
                                 "\" is not supported, ignored");
        return;
    }
    std::optional<std::string> builtin = names.Layout(*layout);
    if (!builtin) {
        sheet.problems.push_back("line " + std::to_string(line) + ": layout \"ampify:" + *layout + "\" is unknown (" +
                                 LayoutList(names) + "), ignored");
        return;
    }
    // Layouts hold no @import, so this goes one level deep.
    Stylesheet imported = ParseStylesheet(*builtin, names);
    for (Rule& rule : imported.rules) {
        rule.origin = "ampify:" + *layout;
        if (scope.media && !rule.media) rule.media = scope.media;
        rule.settings.insert(rule.settings.begin(), scope.settings.begin(), scope.settings.end());
        sheet.rules.push_back(std::move(rule));
    }
    for (const std::string& problem : imported.problems) sheet.problems.push_back("ampify:" + *layout + " " + problem);
}

}  // namespace

Stylesheet ParseStylesheet(std::string_view css, const StylesheetNames& names) {
    Stylesheet sheet;
    std::string clean = WithoutComments(WithoutBom(css));
    size_t pos = 0;
    Scope scope;  // the @media and @setting blocks open here

    while (pos < clean.size()) {
        if (IsSpace(clean[pos])) {
            pos++;
            continue;
        }
        if (!scope.blocks.empty() && clean[pos] == '}') {  // the end of the innermost @media or @setting block
            if (scope.blocks.back()) scope.settings.pop_back();
            else scope.media.reset();
            scope.blocks.pop_back();
            pos++;
            continue;
        }

        size_t open = clean.find('{', pos);
        int line = LineOf(clean, pos);

        // A statement such as `@import url(a.css);` has no block: skip it, so it cannot swallow the rule after it.
        size_t semicolon = clean.find(';', pos);
        if (semicolon != std::string::npos && (open == std::string::npos || semicolon < open)) {
            std::string_view statement = TrimSpace(std::string_view(clean).substr(pos, semicolon - pos));
            pos = semicolon + 1;
            ReadStatement(statement, line, scope, names, sheet);
            continue;
        }

        if (open == std::string::npos) {
            sheet.problems.push_back("line " + std::to_string(line) + ": opening brace missing");
            break;
        }
        std::string_view head = TrimSpace(std::string_view(clean).substr(pos, open - pos));
        if (!head.empty() && head.front() == '@') {
            pos = ReadAtRule(head, open, line, clean, scope, sheet.keyframes, sheet.problems);
            continue;
        }

        size_t close = clean.find('}', open);
        if (close == std::string::npos) {
            sheet.problems.push_back("line " + std::to_string(line) + ": closing brace missing");
            break;
        }
        std::string_view body = std::string_view(clean).substr(open + 1, close - open - 1);
        pos = close + 1;

        Rule rule;
        rule.line = line;
        rule.media = scope.media;
        rule.settings = scope.settings;
        if (ToLowerAscii(head) == ":root") {
            if (scope.media) {
                sheet.problems.push_back("line " + std::to_string(line) + ": variables inside @media are not supported, "
                                         "ignored");
                continue;
            }
            rule.root = true;
            rule.declarations = ParseDeclarations(body, open + 1, clean, sheet.problems);
            sheet.rules.push_back(std::move(rule));
            continue;
        }
        std::string problem;
        std::optional<std::vector<Selector>> selectors = ParseSelectorList(head, names, problem);
        if (!selectors) {
            sheet.problems.push_back("line " + std::to_string(line) + ": " + problem + ", rule ignored");
            continue;
        }
        if (selectors->empty()) continue;
        rule.selectors = std::move(*selectors);

        rule.declarations = ParseDeclarations(body, open + 1, clean, sheet.problems);
        sheet.rules.push_back(std::move(rule));
    }
    return sheet;
}

}  // namespace ampify::theme
