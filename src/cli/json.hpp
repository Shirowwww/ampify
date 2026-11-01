#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::json {

// A parsed JSON value. Object members keep their file order, and a duplicate key is a parse error:
// a manifest must not mean one thing to one reader and another to the next.
class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    static Value MakeBool(bool value);
    static Value MakeNumber(double value);
    static Value MakeString(std::string value);
    static Value MakeArray(std::vector<Value> items);
    static Value MakeObject(std::vector<std::string> keys, std::vector<Value> values);

    Type type() const { return m_type; }
    bool IsNull() const { return m_type == Type::Null; }
    bool IsBool() const { return m_type == Type::Bool; }
    bool IsNumber() const { return m_type == Type::Number; }
    bool IsString() const { return m_type == Type::String; }
    bool IsArray() const { return m_type == Type::Array; }
    bool IsObject() const { return m_type == Type::Object; }

    bool AsBool() const { return m_bool; }
    double AsNumber() const { return m_number; }
    const std::string& AsString() const { return m_string; }

    // Items of an array, or the values of an object (same order as Keys()).
    const std::vector<Value>& Items() const { return m_items; }
    // Member names of an object, empty for other types.
    const std::vector<std::string>& Keys() const { return m_keys; }

    // The member called `key`, or nullptr when this is not an object or has no such member.
    const Value* Find(std::string_view key) const;

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0;
    std::string m_string;
    std::vector<std::string> m_keys;
    std::vector<Value> m_items;
};

// Parses a whole document. On failure returns nullopt and writes the reason, with its byte offset, to `error`.
std::optional<Value> Parse(std::string_view text, std::string& error);

// A JSON string literal, quotes included.
std::string Quote(std::string_view text);

}  // namespace ampify::json
