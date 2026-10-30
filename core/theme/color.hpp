#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace amk::theme {

// A colour as XAML stores it: alpha, red, green, blue, one byte each.
struct Argb {
    std::uint8_t a = 0xFF;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    bool operator==(const Argb&) const = default;
};

// Accepts `#RGB`, `#RRGGBB` and `#AARRGGBB` (case-insensitive, `#` optional). Anything else gives nothing.
std::optional<Argb> ParseColor(std::string_view text);

// The text XAML expects for a colour: `#AARRGGBB`.
std::string ToXamlColor(Argb color);

// Named colours of a theme, written `$name` in its rules. Names are case-insensitive.
class Palette {
public:
    // Returns false when the name is empty or already defined.
    bool Define(std::string_view name, Argb color);
    std::optional<Argb> Find(std::string_view name) const;
    const std::map<std::string, Argb>& Colors() const { return m_colors; }

private:
    std::map<std::string, Argb> m_colors;  // keys are lowercase
};

}  // namespace amk::theme
