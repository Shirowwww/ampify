#include "color.hpp"

#include <cctype>

namespace amk::theme {

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

bool Palette::Define(std::string_view name, Argb color) {
    if (name.empty()) return false;

    std::string key;
    for (char c : name) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return m_colors.emplace(key, color).second;
}

std::optional<Argb> Palette::Find(std::string_view name) const {
    std::string key;
    for (char c : name) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it = m_colors.find(key);
    if (it == m_colors.end()) return std::nullopt;
    return it->second;
}

}  // namespace amk::theme
