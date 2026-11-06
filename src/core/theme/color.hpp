#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ampify::theme {

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

// WCAG relative luminance, 0 (black) to 1 (white); the alpha is not counted.
double RelativeLuminance(Argb color);

// WCAG contrast ratio of two opaque colours, from 1 (the same) to 21 (black on white).
double ContrastRatio(Argb a, Argb b);

// `top` laid over `under` with `alpha` (0 to 1, the top colour's own alpha already counted in), as an opaque colour.
Argb Blend(Argb under, Argb top, double alpha);

// Named colours of a theme, written `$name` in its rules. Names are case-insensitive.
class Palette {
public:
    // Returns false when the name is empty or already defined.
    bool Define(std::string_view name, Argb color);
    // Defines the name, or replaces the colour it had.
    void Set(std::string_view name, Argb color);
    void Remove(std::string_view name);
    std::optional<Argb> Find(std::string_view name) const;
    const std::map<std::string, Argb>& Colors() const { return m_colors; }

private:
    std::map<std::string, Argb> m_colors;  // keys are lowercase
};

}  // namespace ampify::theme
