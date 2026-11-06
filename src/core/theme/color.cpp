#include "color.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "../text_util.hpp"

namespace ampify::theme {

namespace {

std::optional<std::uint8_t> HexDigit(char c) {
    if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
    return std::nullopt;
}

std::optional<std::uint8_t> HexByte(char high, char low) {
    auto h = HexDigit(high);
    auto l = HexDigit(low);
    if (!h || !l) return std::nullopt;
    return static_cast<std::uint8_t>((*h << 4) | *l);
}

}  // namespace

std::optional<Argb> ParseColor(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);

    // `#RGB` doubles each digit: `#F80` is `#FF8800`.
    if (text.size() == 3) {
        auto r = HexDigit(text[0]);
        auto g = HexDigit(text[1]);
        auto b = HexDigit(text[2]);
        if (!r || !g || !b) return std::nullopt;
        return Argb{0xFF, static_cast<std::uint8_t>(*r * 17), static_cast<std::uint8_t>(*g * 17),
                    static_cast<std::uint8_t>(*b * 17)};
    }

    if (text.size() != 6 && text.size() != 8) return std::nullopt;

    size_t offset = text.size() == 8 ? 2 : 0;
    Argb color;
    if (offset == 2) {
        auto a = HexByte(text[0], text[1]);
        if (!a) return std::nullopt;
        color.a = *a;
    }

    auto r = HexByte(text[offset], text[offset + 1]);
    auto g = HexByte(text[offset + 2], text[offset + 3]);
    auto b = HexByte(text[offset + 4], text[offset + 5]);
    if (!r || !g || !b) return std::nullopt;
    color.r = *r;
    color.g = *g;
    color.b = *b;
    return color;
}

std::string ToXamlColor(Argb color) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text = "#";
    for (std::uint8_t channel : {color.a, color.r, color.g, color.b}) {
        text += kDigits[channel >> 4];
        text += kDigits[channel & 0x0F];
    }
    return text;
}

double RelativeLuminance(Argb color) {
    auto linear = [](std::uint8_t channel) {
        double value = channel / 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
}

double ContrastRatio(Argb a, Argb b) {
    double first = RelativeLuminance(a);
    double second = RelativeLuminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

Argb Blend(Argb under, Argb top, double alpha) {
    alpha = std::clamp(alpha, 0.0, 1.0);
    auto mix = [&](std::uint8_t low, std::uint8_t high) {
        return static_cast<std::uint8_t>(std::lround(low + (high - low) * alpha));
    };
    return {0xFF, mix(under.r, top.r), mix(under.g, top.g), mix(under.b, top.b)};
}

bool Palette::Define(std::string_view name, Argb color) {
    if (name.empty()) return false;

    std::string key = ToLowerAscii(name);
    return m_colors.emplace(key, color).second;
}

void Palette::Set(std::string_view name, Argb color) {
    if (!name.empty()) m_colors[ToLowerAscii(name)] = color;
}

void Palette::Remove(std::string_view name) {
    m_colors.erase(ToLowerAscii(name));
}

std::optional<Argb> Palette::Find(std::string_view name) const {
    std::string key = ToLowerAscii(name);
    auto it = m_colors.find(key);
    if (it == m_colors.end()) return std::nullopt;
    return it->second;
}

}  // namespace ampify::theme
