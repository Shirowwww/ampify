#include "json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "text_util.hpp"

namespace ampify {

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::string_view text) : m_text(text) {}

    std::optional<Json> ParseDocument(std::string* error) {
        std::optional<Json> value = ParseValue(0);
        if (value) {
            SkipSpace();
            if (m_pos != m_text.size()) {
                Fail("text after the value");
                value.reset();
            }
        }
        if (!value && error) *error = m_error;
        return value;
    }

private:
    std::string_view m_text;
    size_t m_pos = 0;
    std::string m_error;

    std::optional<Json> Fail(const std::string& message) {
        if (m_error.empty()) m_error = message + " at byte " + std::to_string(m_pos);
        return std::nullopt;
    }

    void SkipSpace() {
        while (m_pos < m_text.size() && (m_text[m_pos] == ' ' || m_text[m_pos] == '\t' || m_text[m_pos] == '\n' ||
                                         m_text[m_pos] == '\r')) {
            m_pos++;
        }
    }

    bool Consume(std::string_view word) {
        if (m_text.substr(m_pos, word.size()) != word) return false;
        m_pos += word.size();
        return true;
    }

    std::optional<Json> ParseValue(int depth) {
        if (depth > kMaxDepth) return Fail("nesting too deep");
        SkipSpace();
        if (m_pos >= m_text.size()) return Fail("unexpected end of text");

        char c = m_text[m_pos];
        if (c == '{') return ParseObject(depth);
        if (c == '[') return ParseArray(depth);
        if (c == '"') {
            std::optional<std::string> s = ParseString();
            if (!s) return std::nullopt;
            return Json(std::move(*s));
        }
        if (Consume("true")) return Json(true);
        if (Consume("false")) return Json(false);
        if (Consume("null")) return Json();
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
        return Fail("unexpected character");
    }

    std::optional<Json> ParseObject(int depth) {
        m_pos++;  // '{'
        Json::Object members;
        SkipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            m_pos++;
            return Json(std::move(members));
        }
        while (true) {
            SkipSpace();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') return Fail("key expected");
            std::optional<std::string> key = ParseString();
            if (!key) return std::nullopt;
            SkipSpace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') return Fail("':' expected");
            m_pos++;
            std::optional<Json> value = ParseValue(depth + 1);
            if (!value) return std::nullopt;
            members.emplace_back(std::move(*key), std::move(*value));

            SkipSpace();
            if (m_pos < m_text.size() && m_text[m_pos] == ',') {
                m_pos++;
                continue;
            }
            if (m_pos < m_text.size() && m_text[m_pos] == '}') {
                m_pos++;
                return Json(std::move(members));
            }
            return Fail("',' or '}' expected");
        }
    }

    std::optional<Json> ParseArray(int depth) {
        m_pos++;  // '['
        Json::Array items;
        SkipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            m_pos++;
            return Json(std::move(items));
        }
        while (true) {
            std::optional<Json> value = ParseValue(depth + 1);
            if (!value) return std::nullopt;
            items.push_back(std::move(*value));

            SkipSpace();
            if (m_pos < m_text.size() && m_text[m_pos] == ',') {
                m_pos++;
                continue;
            }
            if (m_pos < m_text.size() && m_text[m_pos] == ']') {
                m_pos++;
                return Json(std::move(items));
            }
            return Fail("',' or ']' expected");
        }
    }

    std::optional<Json> ParseNumber() {
        size_t start = m_pos;
        if (m_text[m_pos] == '-') m_pos++;
        if (m_pos >= m_text.size()) return Fail("incomplete number");
        if (m_text[m_pos] == '0') {
            m_pos++;
        } else if (m_text[m_pos] >= '1' && m_text[m_pos] <= '9') {
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) m_pos++;
        } else {
            return Fail("digit expected");
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            m_pos++;
            if (m_pos >= m_text.size() || !std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) {
                return Fail("digit expected after the point");
            }
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) m_pos++;
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            m_pos++;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) m_pos++;
            if (m_pos >= m_text.size() || !std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) {
                return Fail("digit expected in the exponent");
            }
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) m_pos++;
        }
        std::string token(m_text.substr(start, m_pos - start));
        double value = std::strtod(token.c_str(), nullptr);
        if (!std::isfinite(value)) return Fail("number out of range");
        return Json(value);
    }

    static void AppendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::optional<unsigned> ParseHex4() {
        if (m_pos + 4 > m_text.size()) return std::nullopt;
        unsigned value = 0;
        for (int i = 0; i < 4; i++) {
            char c = m_text[m_pos++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }
        return value;
    }

    std::optional<std::string> ParseString() {
        m_pos++;  // opening quote
        std::string out;
        while (m_pos < m_text.size()) {
            unsigned char c = static_cast<unsigned char>(m_text[m_pos++]);
            if (c == '"') return out;
            if (c < 0x20) {
                Fail("control character in a string");
                return std::nullopt;
            }
            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }
            if (m_pos >= m_text.size()) break;
            char escape = m_text[m_pos++];
            switch (escape) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::optional<unsigned> code = ParseHex4();
                    if (!code) {
                        Fail("invalid \\u escape");
                        return std::nullopt;
                    }
                    unsigned point = *code;
                    if (point >= 0xD800 && point <= 0xDBFF) {
                        // A high surrogate must be followed by a low one.
                        if (!Consume("\\u")) {
                            Fail("incomplete surrogate pair");
                            return std::nullopt;
                        }
                        std::optional<unsigned> low = ParseHex4();
                        if (!low || *low < 0xDC00 || *low > 0xDFFF) {
                            Fail("invalid surrogate pair");
                            return std::nullopt;
                        }
                        point = 0x10000 + ((point - 0xD800) << 10) + (*low - 0xDC00);
                    } else if (point >= 0xDC00 && point <= 0xDFFF) {
                        Fail("lone surrogate");
                        return std::nullopt;
                    }
                    AppendUtf8(out, point);
                    break;
                }
                default:
                    Fail("unknown escape");
                    return std::nullopt;
            }
        }
        Fail("unterminated string");
        return std::nullopt;
    }
};

void DumpString(const std::string& s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned char>(c));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

}  // namespace

std::optional<Json> Json::Parse(std::string_view text, std::string* error) {
    return Parser(WithoutBom(text)).ParseDocument(error);
}

std::string Json::Dump() const {
    std::string out;
    if (IsNull()) {
        out = "null";
    } else if (const bool* b = AsBool()) {
        out = *b ? "true" : "false";
    } else if (const double* n = AsNumber()) {
        char buffer[32];
        if (std::floor(*n) == *n && std::fabs(*n) < 9e15) {
            std::snprintf(buffer, sizeof buffer, "%lld", static_cast<long long>(*n));
        } else {
            std::snprintf(buffer, sizeof buffer, "%.17g", *n);
        }
        out = buffer;
    } else if (const std::string* s = AsString()) {
        DumpString(*s, out);
    } else if (const Array* items = AsArray()) {
        out = "[";
        for (size_t i = 0; i < items->size(); i++) {
            if (i > 0) out += ',';
            out += (*items)[i].Dump();
        }
        out += ']';
    } else if (const Object* members = AsObject()) {
        out = "{";
        for (size_t i = 0; i < members->size(); i++) {
            if (i > 0) out += ',';
            DumpString((*members)[i].first, out);
            out += ':';
            out += (*members)[i].second.Dump();
        }
        out += '}';
    }
    return out;
}

const Json* Json::Find(std::string_view key) const {
    const Object* members = AsObject();
    if (!members) return nullptr;
    for (const auto& [name, value] : *members) {
        if (name == key) return &value;
    }
    return nullptr;
}

void Json::Set(std::string key, Json value) {
    Object* members = std::get_if<Object>(&m_value);
    if (!members) return;
    for (auto& [name, existing] : *members) {
        if (name == key) {
            existing = std::move(value);
            return;
        }
    }
    members->emplace_back(std::move(key), std::move(value));
}

}  // namespace ampify
