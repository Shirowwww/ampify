#include "accent.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

namespace ampify::theme {

std::optional<Argb> ParseAccent(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    constexpr size_t kDigits = 6;
    if (text.size() != kDigits) return std::nullopt;
    for (char digit : text) {
        if (!std::isxdigit(static_cast<unsigned char>(digit))) return std::nullopt;
    }
    return ParseColor(text);
}

bool IsArtworkAccent(std::string_view text) {
    return text == kArtworkAccent;
}

Argb LegibleAccent(Argb color) {
    // Lightness limits: an accent darker or lighter than these vanishes on a dark or a light page.
    constexpr double kDarkest = 0.30;
    constexpr double kLightest = 0.70;
    double high = std::max({color.r, color.g, color.b}) / 255.0;
    double low = std::min({color.r, color.g, color.b}) / 255.0;
    double lightness = (high + low) / 2;
    if (lightness >= kDarkest && lightness <= kLightest) return color;
    // Mixing with white or with black keeps the hue, and lands the lightness on the limit.
    bool lift = lightness < kDarkest;
    double amount = lift ? (kDarkest - lightness) / (1 - lightness) : 1 - kLightest / lightness;
    auto mix = [lift, amount](std::uint8_t channel) {
        double value = lift ? channel + amount * (255 - channel) : channel * (1 - amount);
        return static_cast<std::uint8_t>(std::lround(value));
    };
    return Argb{color.a, mix(color.r), mix(color.g), mix(color.b)};
}

std::optional<Argb> ArtworkAccent(const std::map<std::string, std::string>& artwork) {
    auto vibrant = artwork.find("art-2");
    if (vibrant == artwork.end()) return std::nullopt;
    std::optional<Argb> color = ParseAccent(vibrant->second);
    if (!color) return std::nullopt;
    return LegibleAccent(*color);
}

}  // namespace ampify::theme
