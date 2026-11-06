#include "css_values.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

#include "../text_util.hpp"

namespace ampify::theme {

namespace {

// One scan for both splits: with `quotes`, a quoted piece stays whole, as the syntax of a stylesheet needs.
std::vector<std::string_view> SplitOutside(std::string_view text, char separator, bool quotes) {
    std::vector<std::string_view> pieces;
    int depth = 0;
    char quote = 0;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (quotes && (c == '"' || c == '\'')) {
            quote = c;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
        } else if (depth == 0 && (separator == ' ' ? std::isspace(static_cast<unsigned char>(c)) != 0 : c == separator)) {
            pieces.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    pieces.push_back(text.substr(start));
    return pieces;
}

}  // namespace

bool IsTypeChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

size_t ClosingBracket(std::string_view text, size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); i++) {
        if (text[i] == '(') depth++;
        if (text[i] == ')' && --depth == 0) return i;
    }
    return std::string_view::npos;
}

std::string_view Unquoted(std::string_view text) {
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front()) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::vector<std::string_view> SplitTopLevel(std::string_view text, char separator) {
    return SplitOutside(text, separator, true);
}

std::vector<std::string_view> SplitArguments(std::string_view text, char separator, bool skipEmpty) {
    std::vector<std::string_view> pieces;
    for (std::string_view piece : SplitOutside(text, separator, false)) {
        piece = TrimSpace(piece);
        if (!piece.empty() || !skipEmpty) pieces.push_back(piece);
    }
    return pieces;
}

std::vector<std::string_view> SplitWords(std::string_view text) {
    std::vector<std::string_view> words;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) i++;
        size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) i++;
        if (i > start) words.push_back(text.substr(start, i - start));
    }
    return words;
}

std::optional<double> ParseNumber(std::string_view text) {
    double value = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    auto [end, error] = std::from_chars(first, last, value);
    if (error != std::errc() || end != last || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<std::string> SubstituteVariables(std::string_view value, const std::map<std::string, std::string>& variables,
                                               std::string* problem) {
    std::string text(value);
    // Each pass replaces the first var(...). A fallback may hold another var(), so a few passes may be needed; the
    // limit stops a value that would grow forever.
    for (int pass = 0; pass < 32; pass++) {
        std::string lowered = ToLowerAscii(text);
        size_t start = lowered.find("var(");
        if (start == std::string::npos) return text;

        size_t close = ClosingBracket(text, start + 3);
        if (close == std::string::npos) {
            if (problem) *problem = "var( without a closing parenthesis";
            return std::nullopt;
        }

        std::string_view inside = std::string_view(text).substr(start + 4, close - start - 4);
        size_t comma = inside.find(',');
        std::string name = ToLowerAscii(TrimSpace(inside.substr(0, comma)));
        if (!name.starts_with("--") || name.size() == 2) {
            if (problem) *problem = "var() needs a --variable name";
            return std::nullopt;
        }

        std::string replacement;
        auto it = variables.find(name.substr(2));
        if (it != variables.end()) {
            replacement = it->second;
        } else if (comma != std::string_view::npos) {
            replacement = std::string(TrimSpace(inside.substr(comma + 1)));
        } else {
            if (problem) *problem = "variable \"" + name + "\" is not defined";
            return std::nullopt;
        }
        text = text.substr(0, start) + replacement + text.substr(close + 1);
    }
    if (problem) *problem = "too many nested var()";
    return std::nullopt;
}

namespace {

struct Quantity {
    double number = 0;
    std::string unit;  // empty for a plain number
};

// Reads the inside of one calc(): sums of products of numbers, brackets and nested calc(), in CSS's order.
class CalcReader {
public:
    explicit CalcReader(std::string_view text) : m_text(text) {}

    std::optional<Quantity> Read() {
        std::optional<Quantity> result = Sum();
        SkipSpaces();
        if (result && m_pos != m_text.size()) return Fail("calc() could not read \"" + std::string(m_text) + "\"");
        return result;
    }

    const std::string& Problem() const { return m_problem; }

private:
    std::optional<Quantity> Fail(std::string problem) {
        if (m_problem.empty()) m_problem = std::move(problem);
        return std::nullopt;
    }

    void SkipSpaces() {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) m_pos++;
    }

    bool Take(char c) {
        SkipSpaces();
        if (m_pos >= m_text.size() || m_text[m_pos] != c) return false;
        m_pos++;
        return true;
    }

    std::optional<Quantity> Sum() {
        std::optional<Quantity> left = Product();
        while (left) {
            bool add = Take('+');
            if (!add && !Take('-')) break;
            std::optional<Quantity> right = Product();
            if (!right) return std::nullopt;
            if (!left->unit.empty() && !right->unit.empty() && left->unit != right->unit) {
                return Fail("calc() adds " + right->unit + " to " + left->unit);
            }
            left->number += add ? right->number : -right->number;
            if (left->unit.empty()) left->unit = right->unit;
        }
        return left;
    }

    std::optional<Quantity> Product() {
        std::optional<Quantity> left = Factor();
        while (left) {
            bool multiply = Take('*');
            if (!multiply && !Take('/')) break;
            std::optional<Quantity> right = Factor();
            if (!right) return std::nullopt;
            if (multiply) {
                if (!left->unit.empty() && !right->unit.empty()) return Fail("calc() multiplies two lengths");
                left->number *= right->number;
                if (left->unit.empty()) left->unit = right->unit;
                continue;
            }
            if (!right->unit.empty()) return Fail("calc() divides by a length");
            if (right->number == 0) return Fail("calc() divides by zero");
            left->number /= right->number;
        }
        return left;
    }

    // min(a, b, ...) and max(...): the smallest or the largest of several lengths of one unit.
    std::optional<Quantity> Extremum(bool smallest) {
        std::string name = smallest ? "min()" : "max()";
        std::optional<Quantity> best = Sum();
        while (best && Take(',')) {
            std::optional<Quantity> next = Sum();
            if (!next) return std::nullopt;
            if (!best->unit.empty() && !next->unit.empty() && best->unit != next->unit) {
                return Fail(name + " compares " + next->unit + " with " + best->unit);
            }
            best->number = smallest ? std::min(best->number, next->number) : std::max(best->number, next->number);
            if (best->unit.empty()) best->unit = next->unit;
        }
        if (!best) return std::nullopt;
        if (!Take(')')) return Fail(name + " is missing a closing parenthesis");
        return best;
    }

    std::optional<Quantity> Factor() {
        // A sign before a bracket or a number: -(1 + 2), +4.
        if (Take('+')) return Factor();
        if (Take('-')) {
            std::optional<Quantity> inner = Factor();
            if (inner) inner->number = -inner->number;
            return inner;
        }
        SkipSpaces();
        std::string_view word = m_text.substr(m_pos, 4);
        if (ToLowerAscii(word) == "min(" || ToLowerAscii(word) == "max(") {
            m_pos += 4;
            return Extremum(ToLowerAscii(word) == "min(");
        }
        if (ToLowerAscii(m_text.substr(m_pos, 5)) == "calc(") m_pos += 4;
        if (Take('(')) {
            std::optional<Quantity> inner = Sum();
            if (!inner) return std::nullopt;
            if (!Take(')')) return Fail("calc() is missing a closing parenthesis");
            return inner;
        }

        Quantity quantity;
        const char* begin = m_text.data() + m_pos;
        auto [end, error] = std::from_chars(begin, m_text.data() + m_text.size(), quantity.number);
        if (error != std::errc() || !std::isfinite(quantity.number)) {
            return Fail("calc() expects a number in \"" + std::string(m_text) + "\"");
        }
        m_pos += end - begin;
        while (m_pos < m_text.size() && (std::isalpha(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '%')) {
            quantity.unit += static_cast<char>(std::tolower(static_cast<unsigned char>(m_text[m_pos++])));
        }
        return quantity;
    }

    std::string_view m_text;
    size_t m_pos = 0;
    std::string m_problem;
};

std::string FormatQuantity(const Quantity& quantity) {
    // Four decimals are plenty for pixels and milliseconds, and keep 0.1 + 0.2 from printing as 0.30000000000000004.
    double rounded = std::round(quantity.number * 10000) / 10000;
    if (rounded == 0) rounded = 0;  // no "-0"
    char buffer[32];
    auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), rounded);
    return std::string(buffer, error == std::errc() ? end : buffer) + quantity.unit;
}

}  // namespace

std::optional<std::string> EvaluateCalc(std::string_view value, std::string* problem) {
    std::string text(value);
    for (int pass = 0; pass < 32; pass++) {
        size_t start = ToLowerAscii(text).find("calc(");
        if (start == std::string::npos) return text;

        size_t close = ClosingBracket(text, start + 4);
        if (close == std::string::npos) {
            if (problem) *problem = "calc( without a closing parenthesis";
            return std::nullopt;
        }

        CalcReader reader(std::string_view(text).substr(start + 5, close - start - 5));
        std::optional<Quantity> result = reader.Read();
        if (!result) {
            if (problem) *problem = reader.Problem();
            return std::nullopt;
        }
        text = text.substr(0, start) + FormatQuantity(*result) + text.substr(close + 1);
    }
    if (problem) *problem = "too many calc()";
    return std::nullopt;
}

std::optional<std::string> ResolveValue(std::string_view value, const std::map<std::string, std::string>& variables,
                                        std::string* problem) {
    std::optional<std::string> substituted = SubstituteVariables(value, variables, problem);
    if (!substituted) return std::nullopt;
    return EvaluateCalc(*substituted, problem);
}

}  // namespace ampify::theme
