#include "json.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace ampify::json {

namespace {

// Deep enough for any manifest, shallow enough that a hostile file cannot exhaust the stack.
constexpr int kMaxDepth = 64;

struct SyntaxError {
    std::string message;
};

void AppendUtf8(std::string& out, std::uint32_t code) {
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

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

class Parser {
public:
    explicit Parser(std::string_view text) : m_text(text) {}

    Value ParseDocument() {
        Value value = ParseValue(0);
        SkipSpace();
        if (m_pos != m_text.size()) Fail("text after the end of the document");
        return value;
    }

private:
    [[noreturn]] void Fail(const std::string& message) const {
        throw SyntaxError{message + " (octet " + std::to_string(m_pos) + ")"};
    }

    char Peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }

    void SkipSpace() {
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            m_pos++;
        }
    }

    void ExpectLiteral(std::string_view literal) {
        if (m_text.substr(m_pos, literal.size()) != literal) Fail("invalid value");
        m_pos += literal.size();
    }

    Value ParseValue(int depth) {
        if (depth > kMaxDepth) Fail("nesting too deep");
        SkipSpace();
        if (m_pos >= m_text.size()) Fail("unexpected end of text");

        char c = m_text[m_pos];
        switch (c) {
            case '{':
                return ParseObject(depth);
            case '[':
                return ParseArray(depth);
            case '"':
                return Value::MakeString(ParseString());
            case 't':
                ExpectLiteral("true");
                return Value::MakeBool(true);
            case 'f':
                ExpectLiteral("false");
                return Value::MakeBool(false);
            case 'n':
                ExpectLiteral("null");
                return Value();
            default:
                if (c == '-' || IsDigit(c)) return ParseNumber();
                Fail("unexpected character");
        }
    }

    Value ParseObject(int depth) {
        m_pos++;  // '{'
        std::vector<std::string> keys;
        std::vector<Value> values;

        SkipSpace();
        if (Peek() == '}') {
            m_pos++;
            return Value::MakeObject(std::move(keys), std::move(values));
        }

        while (true) {
            SkipSpace();
            if (Peek() != '"') Fail("member name expected");
            std::string key = ParseString();
            if (std::find(keys.begin(), keys.end(), key) != keys.end()) Fail("duplicate member: " + key);

            SkipSpace();
            if (Peek() != ':') Fail("':' expected");
            m_pos++;

            values.push_back(ParseValue(depth + 1));
            keys.push_back(std::move(key));

            SkipSpace();
            if (Peek() == ',') {
                m_pos++;
            } else if (Peek() == '}') {
                m_pos++;
                return Value::MakeObject(std::move(keys), std::move(values));
            } else {
                Fail("',' or '}' expected");
            }
        }
    }

    Value ParseArray(int depth) {
        m_pos++;  // '['
        std::vector<Value> items;

        SkipSpace();
        if (Peek() == ']') {
            m_pos++;
            return Value::MakeArray(std::move(items));
        }

        while (true) {
            items.push_back(ParseValue(depth + 1));

            SkipSpace();
            if (Peek() == ',') {
                m_pos++;
            } else if (Peek() == ']') {
                m_pos++;
                return Value::MakeArray(std::move(items));
            } else {
                Fail("',' or ']' expected");
            }
        }
    }

    std::uint32_t ParseHex4() {
        if (m_pos + 4 > m_text.size()) Fail("\\u incomplete");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; i++) {
            char c = m_text[m_pos++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                Fail("hexadecimal digit expected");
            }
        }
        return value;
    }

    std::string ParseString() {
        m_pos++;  // opening quote
        std::string out;
        while (true) {
            if (m_pos >= m_text.size()) Fail("unclosed string");
            unsigned char c = static_cast<unsigned char>(m_text[m_pos++]);
            if (c == '"') return out;
            if (c < 0x20) Fail("control character in a string");
            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }

            if (m_pos >= m_text.size()) Fail("incomplete escape");
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
                    std::uint32_t code = ParseHex4();
                    if (code >= 0xDC00 && code <= 0xDFFF) Fail("lone surrogate");
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (m_text.substr(m_pos, 2) != "\\u") Fail("incomplete surrogate pair");
                        m_pos += 2;
                        std::uint32_t low = ParseHex4();
                        if (low < 0xDC00 || low > 0xDFFF) Fail("invalid surrogate pair");
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    Fail("unknown escape");
            }
        }
    }

    Value ParseNumber() {
        size_t start = m_pos;
        if (Peek() == '-') m_pos++;
        if (!IsDigit(Peek())) Fail("invalid number");
        if (Peek() == '0') {
            m_pos++;
        } else {
            while (IsDigit(Peek())) m_pos++;
        }
        if (Peek() == '.') {
            m_pos++;
            if (!IsDigit(Peek())) Fail("invalid number");
            while (IsDigit(Peek())) m_pos++;
        }
        if (Peek() == 'e' || Peek() == 'E') {
            m_pos++;
            if (Peek() == '+' || Peek() == '-') m_pos++;
            if (!IsDigit(Peek())) Fail("invalid number");
            while (IsDigit(Peek())) m_pos++;
        }

        std::string_view token = m_text.substr(start, m_pos - start);
        double number = 0;
        auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), number);
        if (error != std::errc() || end != token.data() + token.size()) Fail("number out of range");
        return Value::MakeNumber(number);
    }

    std::string_view m_text;
    size_t m_pos = 0;
};

}  // namespace

Value Value::MakeBool(bool value) {
    Value result;
    result.m_type = Type::Bool;
    result.m_bool = value;
    return result;
}

Value Value::MakeNumber(double value) {
    Value result;
    result.m_type = Type::Number;
    result.m_number = value;
    return result;
}

Value Value::MakeString(std::string value) {
    Value result;
    result.m_type = Type::String;
    result.m_string = std::move(value);
    return result;
}

Value Value::MakeArray(std::vector<Value> items) {
    Value result;
    result.m_type = Type::Array;
    result.m_items = std::move(items);
    return result;
}

Value Value::MakeObject(std::vector<std::string> keys, std::vector<Value> values) {
    Value result;
    result.m_type = Type::Object;
    result.m_keys = std::move(keys);
    result.m_items = std::move(values);
    return result;
}

const Value* Value::Find(std::string_view key) const {
    if (m_type != Type::Object) return nullptr;
    for (size_t i = 0; i < m_keys.size(); i++) {
        if (m_keys[i] == key) return &m_items[i];
    }
    return nullptr;
}

std::optional<Value> Parse(std::string_view text, std::string& error) {
    try {
        return Parser(text).ParseDocument();
    } catch (const SyntaxError& failure) {
        error = failure.message;
        return std::nullopt;
    }
}

std::string Quote(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        unsigned char byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(byte));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

}  // namespace ampify::json
