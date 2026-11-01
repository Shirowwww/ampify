#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ampify {

// A small JSON value: enough for manifests and storage files. Objects keep their member order, so
// Dump() is deterministic. Parsing is strict RFC 8259 with a nesting limit, so a hostile file cannot
// exhaust the stack.
class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    Json() = default;  // null
    Json(bool value) : m_value(value) {}
    Json(double value) : m_value(value) {}
    Json(std::string value) : m_value(std::move(value)) {}
    Json(const char* value) : m_value(std::string(value)) {}
    Json(Array value) : m_value(std::move(value)) {}
    Json(Object value) : m_value(std::move(value)) {}

    static std::optional<Json> Parse(std::string_view text, std::string* error = nullptr);

    // Compact output, with escapes for control characters and quotes.
    std::string Dump() const;

    bool IsNull() const { return std::holds_alternative<std::nullptr_t>(m_value); }
    bool IsBool() const { return std::holds_alternative<bool>(m_value); }
    bool IsNumber() const { return std::holds_alternative<double>(m_value); }
    bool IsString() const { return std::holds_alternative<std::string>(m_value); }
    bool IsArray() const { return std::holds_alternative<Array>(m_value); }
    bool IsObject() const { return std::holds_alternative<Object>(m_value); }

    // Each accessor returns nullptr or the fallback when the value has another type.
    const Json* Find(std::string_view key) const;
    const std::string* AsString() const { return std::get_if<std::string>(&m_value); }
    const double* AsNumber() const { return std::get_if<double>(&m_value); }
    const bool* AsBool() const { return std::get_if<bool>(&m_value); }
    const Array* AsArray() const { return std::get_if<Array>(&m_value); }
    const Object* AsObject() const { return std::get_if<Object>(&m_value); }

    // Sets a member, replacing an existing one with the same key. The value must be an object.
    void Set(std::string key, Json value);

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> m_value{nullptr};
};

}  // namespace ampify
