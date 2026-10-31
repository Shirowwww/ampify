#include "css.hpp"

#include <cctype>

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

}  // namespace

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

        // A pseudo-class. Only :hover, and only on the last part: the engine follows the pointer of the element it styles.
        if (i < s.size() && s[i] == ':') {
            size_t start = ++i;
            while (i < s.size() && IsNameChar(s[i])) i++;
            std::string pseudo = LowerCopy(s.substr(start, i - start));
            if (pseudo != "hover") {
                return fail("pseudo-class \":" + std::string(s.substr(start, i - start)) + "\" is not supported in \"" +
                            selector.text + "\" (only :hover is)");
            }
            if (s.substr(i).find_first_not_of(" \t\r\n") != std::string_view::npos) {
                return fail("\":hover\" is only allowed on the last element of \"" + selector.text + "\"");
            }
            selector.hover = true;
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
    std::string clean = WithoutComments(css);
    size_t pos = 0;

    while (pos < clean.size()) {
        if (IsSpace(clean[pos])) {
            pos++;
            continue;
        }

        size_t open = clean.find('{', pos);
        int line = LineOf(clean, pos);

        // A statement such as `@import url(a.css);` has no block: skip it, so it cannot swallow the rule after it.
        size_t semicolon = clean.find(';', pos);
        if (semicolon != std::string::npos && (open == std::string::npos || semicolon < open)) {
            std::string_view statement = Trimmed(std::string_view(clean).substr(pos, semicolon - pos));
            sheet.problems.push_back("line " + std::to_string(line) + ": rule \"" + std::string(statement) +
                                     "\" is not supported, ignored");
            pos = semicolon + 1;
            continue;
        }

        if (open == std::string::npos) {
            sheet.problems.push_back("line " + std::to_string(line) + ": opening brace missing");
            break;
        }
        size_t close = clean.find('}', open);
        if (close == std::string::npos) {
            sheet.problems.push_back("line " + std::to_string(line) + ": closing brace missing");
            break;
        }

        std::string_view head = Trimmed(std::string_view(clean).substr(pos, open - pos));
        std::string_view body = std::string_view(clean).substr(open + 1, close - open - 1);
        pos = close + 1;

        if (!head.empty() && head.front() == '@') {
            sheet.problems.push_back("line " + std::to_string(line) + ": rule \"" + std::string(head) +
                                     "\" is not supported, ignored");
            continue;
        }

        Rule rule;
        rule.line = line;
        if (LowerCopy(head) == ":root") {
            rule.root = true;
            rule.declarations = ParseDeclarations(body, open + 1, clean, sheet.problems);
            sheet.rules.push_back(std::move(rule));
            continue;
        }
        bool selectorsOk = true;
        for (const Piece& piece : SplitTopLevel(head, ',')) {
            std::string problem;
            std::optional<Selector> selector = ParseSelector(piece.text, &problem);
            if (!selector) {
                sheet.problems.push_back("line " + std::to_string(line) + ": " + problem + ", rule ignored");
                selectorsOk = false;
                break;
            }
            rule.selectors.push_back(std::move(*selector));
        }
        if (!selectorsOk || rule.selectors.empty()) continue;

        rule.declarations = ParseDeclarations(body, open + 1, clean, sheet.problems);
        sheet.rules.push_back(std::move(rule));
    }
    return sheet;
}

}  // namespace ampify::theme
