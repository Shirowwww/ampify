#include "css.hpp"

#include <cctype>
#include <charconv>

#include "../text_util.hpp"
#include "builtins.hpp"

namespace ampify::theme {

namespace {

bool IsSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool IsTypeChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

bool IsNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

std::string_view Trimmed(std::string_view text) {
    size_t first = 0;
    size_t last = text.size();
    while (first < last && IsSpace(text[first])) first++;
    while (last > first && IsSpace(text[last - 1])) last--;
    return text.substr(first, last - first);
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

struct Piece {
    std::string_view text;
    size_t offset;
};

// Splits on `separator` outside quotes and parentheses, so `url(a;b)` and "x;y" stay whole.
std::vector<Piece> SplitTopLevel(std::string_view text, char separator) {
    std::vector<Piece> pieces;
    int depth = 0;
    char quote = 0;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i == text.size()) {
            pieces.push_back({text.substr(start), start});
            break;
        }
        char c = text[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
        } else if (c == separator && depth == 0) {
            pieces.push_back({text.substr(start, i - start), start});
            start = i + 1;
        }
    }
    return pieces;
}

std::string LowerCopy(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

// The state a pseudo-class names. :focus-within is :focus: the backend follows the focus of the element's parts too.
std::optional<Trigger> StateOf(std::string_view pseudo) {
    if (pseudo == "hover") return Trigger::Hover;
    if (pseudo == "focus" || pseudo == "focus-within") return Trigger::Focus;
    if (pseudo == "active") return Trigger::Active;
    return std::nullopt;
}

std::vector<Declaration> ParseDeclarations(std::string_view body, size_t bodyOffset, const std::string& clean,
                                           std::vector<std::string>& problems) {
    std::vector<Declaration> declarations;
    for (const Piece& piece : SplitTopLevel(body, ';')) {
        std::string_view text = Trimmed(piece.text);
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
        declaration.property = std::string(Trimmed(text.substr(0, colon)));
        declaration.line = line;

        std::string_view value = Trimmed(text.substr(colon + 1));
        const std::string_view suffix = "!important";
        if (value.size() >= suffix.size() && LowerCopy(value.substr(value.size() - suffix.size())) == suffix) {
            declaration.important = true;
            value = Trimmed(value.substr(0, value.size() - suffix.size()));
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
    std::string name = LowerCopy(Trimmed(feature.substr(0, colon)));
    std::string_view value = colon == std::string_view::npos ? std::string_view() : Trimmed(feature.substr(colon + 1));
    if (value.size() > 2 && LowerCopy(value.substr(value.size() - 2)) == "px") value = Trimmed(value.substr(0, value.size() - 2));
    double number = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    double* bound = name == "min-width" ? &query.minWidth : name == "max-width" ? &query.maxWidth
                  : name == "min-height" ? &query.minHeight : name == "max-height" ? &query.maxHeight : nullptr;
    if (!bound) {
        problem = "@media (" + std::string(Trimmed(feature)) + ") is not supported: min-width, max-width, min-height "
                  "and max-height are";
        return false;
    }
    if (value.empty() || error != std::errc() || end != value.data() + value.size()) {
        problem = "@media (" + std::string(Trimmed(feature)) + ") needs a size in pixels";
        return false;
    }
    *bound = number;
    return true;
}

// The condition of `@media <text>`: an optional `screen` or `all`, then features joined by `and`.
std::optional<MediaQuery> ParseMediaQuery(std::string_view text, std::string& problem) {
    MediaQuery query;
    query.text = std::string(Trimmed(text));
    std::string lowered = LowerCopy(text);
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
            problem = "@media " + query.text + " is not supported: only sizes are read, such as (max-width: 1200px)";
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
    for (const Piece& piece : SplitTopLevel(text, ',')) {
        std::string word = LowerCopy(Trimmed(piece.text));
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
    name = Trimmed(name);
    if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front()) {
        name = name.substr(1, name.size() - 2);
    }
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
        std::string_view selector = Trimmed(std::string_view(clean).substr(pos, brace - pos));
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

// An at-rule with a block, its head read up to `open`: @media opens a block of rules with a condition, @keyframes
// adds to `keyframes`, any other is skipped whole. Returns where reading goes on.
size_t ReadAtRule(std::string_view head, size_t open, int line, const std::string& clean,
                  std::optional<MediaQuery>& media, std::vector<Keyframes>& keyframes,
                  std::vector<std::string>& problems) {
    const std::string where = "line " + std::to_string(line) + ": ";
    size_t close = MatchingBrace(clean, open);
    if (close == std::string_view::npos) {
        problems.push_back(where + "closing brace missing");
        return clean.size();
    }
    std::string lowered = LowerCopy(head);
    for (std::string_view keyword : {std::string_view("@keyframes"), std::string_view("@-webkit-keyframes")}) {
        if (!lowered.starts_with(keyword) || (lowered.size() > keyword.size() && !IsSpace(lowered[keyword.size()]))) {
            continue;
        }
        if (media) {
            problems.push_back(where + "@keyframes inside @media is not supported, ignored");
        } else if (std::optional<Keyframes> block =
                       ParseKeyframes(head.substr(keyword.size()), open, close, line, clean, problems)) {
            keyframes.push_back(std::move(*block));
        }
        return close + 1;
    }
    bool isMedia = lowered.starts_with("@media") && (lowered.size() == 6 || IsSpace(lowered[6]) || lowered[6] == '(');
    if (!isMedia) {
        problems.push_back(where + "rule \"" + std::string(head) + "\" is not supported, ignored");
        return close + 1;
    }
    if (media) {
        problems.push_back(where + "@media inside @media is not supported, ignored");
        return close + 1;
    }
    std::string problem;
    std::optional<MediaQuery> query = ParseMediaQuery(head.substr(6), problem);
    if (!query) {
        problems.push_back(where + problem + ", ignored");
        return close + 1;
    }
    media = std::move(query);
    return open + 1;
}

constexpr std::string_view kBuiltinPrefix = "ampify:";

// The layout named by `@import "ampify:<name>"` or `@import url(ampify:<name>)`, or nothing for another statement.
std::optional<std::string> BuiltinImport(std::string_view statement) {
    const std::string_view keyword = "@import";
    if (LowerCopy(statement.substr(0, keyword.size())) != keyword) return std::nullopt;

    std::string_view target = Trimmed(statement.substr(keyword.size()));
    if (LowerCopy(target.substr(0, 4)) == "url(" && target.ends_with(')')) {
        target = Trimmed(target.substr(4, target.size() - 5));
    }
    if (target.size() >= 2 && (target.front() == '"' || target.front() == '\'') && target.back() == target.front()) {
        target = target.substr(1, target.size() - 2);
    }
    if (LowerCopy(target.substr(0, kBuiltinPrefix.size())) != kBuiltinPrefix) return std::nullopt;
    return LowerCopy(target.substr(kBuiltinPrefix.size()));
}

std::string LayoutList() {
    std::string list;
    for (const std::string& name : BuiltinStylesheetNames()) list += (list.empty() ? "" : ", ") + name;
    return list;
}

// The selectors of a rule head, each class replaced by the path it stands for. Nothing, with `problem` set, when one
// of them is wrong: the whole rule is then skipped, as in CSS.
std::optional<std::vector<Selector>> ParseSelectorList(std::string_view head, std::string& problem) {
    std::vector<Selector> selectors;
    for (const Piece& piece : SplitTopLevel(head, ',')) {
        std::string_view written = Trimmed(piece.text);
        std::optional<std::vector<std::string>> expanded = ExpandClasses(written, &problem);
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
    switch (trigger) {
        case Trigger::Hover: return ":hover";
        case Trigger::Focus: return ":focus";
        case Trigger::Active: return ":active";
        case Trigger::Always: break;
    }
    return "";
}

std::optional<Selector> ParseSelector(std::string_view text, std::string* problem) {
    Selector selector;
    selector.text = std::string(Trimmed(text));
    std::string_view s = selector.text;

    auto fail = [&](const std::string& reason) -> std::optional<Selector> {
        if (problem) *problem = reason;
        return std::nullopt;
    };

    if (s.empty()) return fail("empty selector");

    Combinator pending = Combinator::Descendant;
    bool sawChild = false;
    size_t i = 0;
    while (i < s.size()) {
        if (IsSpace(s[i])) {
            i++;
            continue;
        }

        // A state belongs to the element that receives the declarations: no part may follow it.
        if (selector.trigger != Trigger::Always) {
            return fail("\"" + std::string(TriggerName(selector.trigger)) + "\" is only allowed on the last element of \"" +
                        selector.text + "\"");
        }

        if (s[i] == '>') {
            if (selector.parts.empty() || sawChild) return fail("misplaced \">\" in \"" + selector.text + "\"");
            pending = Combinator::Child;
            sawChild = true;
            i++;
            continue;
        }

        Compound compound;
        if (s[i] == '*') {
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
            if (compound.name.empty()) return fail("name missing after \"#\" in \"" + selector.text + "\"");
        }

        // Pseudo-classes: the structural ones on any part, one state (:hover, :focus, :active) only on the last part (the
        // backend follows the state of the element it styles).
        while (i < s.size() && s[i] == ':') {
            size_t start = ++i;
            while (i < s.size() && IsNameChar(s[i])) i++;
            std::string pseudo = LowerCopy(s.substr(start, i - start));
            if (pseudo == "first-child") {
                compound.nth = 1;
            } else if (pseudo == "last-child") {
                compound.last = true;
            } else if (pseudo == "nth-child") {
                size_t close = s.find(')', i);
                if (i >= s.size() || s[i] != '(' || close == std::string_view::npos) {
                    return fail(":nth-child needs a number in brackets, like :nth-child(2), in \"" + selector.text + "\"");
                }
                std::string_view digits = s.substr(i + 1, close - i - 1);
                int n = 0;
                for (char c : digits) n = (c >= '0' && c <= '9' && n < 10000) ? n * 10 + (c - '0') : -1;
                if (digits.empty() || n < 1) {
                    return fail(":nth-child counts from 1, like :nth-child(2), in \"" + selector.text + "\"");
                }
                compound.nth = n;
                i = close + 1;
            } else if (std::optional<Trigger> state = StateOf(pseudo)) {
                if (selector.trigger != Trigger::Always) {
                    return fail("only one of :hover, :focus and :active is allowed in \"" + selector.text + "\"");
                }
                selector.trigger = *state;
            } else {
                return fail("pseudo-class \":" + std::string(s.substr(start, i - start)) + "\" is not supported in \"" +
                            selector.text + "\" (:hover, :focus, :active, :nth-child(n), :first-child and :last-child are)");
            }
        }

        // Nothing was read (for example a stray `@`): the check below reports it, so the loop always advances.
        if (i < s.size() && !IsSpace(s[i]) && s[i] != '>') {
            return fail("character \"" + std::string(1, s[i]) + "\" is not supported in \"" + selector.text + "\"");
        }

        compound.combinator = selector.parts.empty() ? Combinator::Descendant : pending;
        selector.parts.push_back(std::move(compound));
        pending = Combinator::Descendant;
        sawChild = false;
    }

    if (sawChild) return fail("\">\" has no element after it in \"" + selector.text + "\"");
    return selector;
}

Stylesheet ParseStylesheet(std::string_view css) {
    Stylesheet sheet;
    std::string clean = WithoutComments(WithoutBom(css));
    size_t pos = 0;
    std::optional<MediaQuery> media;  // set inside an @media block

    while (pos < clean.size()) {
        if (IsSpace(clean[pos])) {
            pos++;
            continue;
        }
        if (media && clean[pos] == '}') {  // the end of the @media block
            media.reset();
            pos++;
            continue;
        }

        size_t open = clean.find('{', pos);
        int line = LineOf(clean, pos);

        // A statement such as `@import url(a.css);` has no block: skip it, so it cannot swallow the rule after it.
        size_t semicolon = clean.find(';', pos);
        if (semicolon != std::string::npos && (open == std::string::npos || semicolon < open)) {
            std::string_view statement = Trimmed(std::string_view(clean).substr(pos, semicolon - pos));
            pos = semicolon + 1;
            std::optional<std::string> layout = BuiltinImport(statement);
            if (!layout) {
                sheet.problems.push_back("line " + std::to_string(line) + ": rule \"" + std::string(statement) +
                                         "\" is not supported, ignored");
                continue;
            }
            std::optional<std::string_view> builtin = BuiltinStylesheet(*layout);
            if (!builtin) {
                sheet.problems.push_back("line " + std::to_string(line) + ": layout \"ampify:" + *layout +
                                         "\" is unknown (" + LayoutList() + "), ignored");
                continue;
            }
            // Layouts hold no @import, so this goes one level deep.
            Stylesheet imported = ParseStylesheet(*builtin);
            for (Rule& rule : imported.rules) {
                rule.origin = "ampify:" + *layout;
                if (media && !rule.media) rule.media = media;
                sheet.rules.push_back(std::move(rule));
            }
            for (const std::string& problem : imported.problems) sheet.problems.push_back("ampify:" + *layout + " " + problem);
            continue;
        }

        if (open == std::string::npos) {
            sheet.problems.push_back("line " + std::to_string(line) + ": opening brace missing");
            break;
        }
        std::string_view head = Trimmed(std::string_view(clean).substr(pos, open - pos));
        if (!head.empty() && head.front() == '@') {
            pos = ReadAtRule(head, open, line, clean, media, sheet.keyframes, sheet.problems);
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
        rule.media = media;
        if (LowerCopy(head) == ":root") {
            if (media) {
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
        std::optional<std::vector<Selector>> selectors = ParseSelectorList(head, problem);
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
