#include "theme_settings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

#include "../text_util.hpp"

namespace ampify::theme {

ThemeSettings ReadThemeSettings(const Settings& settings) {
    ThemeSettings value;
    value.active = settings.GetString("theme.active", "");
    std::string lowered = ToLowerAscii(value.active);
    if (lowered == "none") value.active.clear();
    value.scheme = settings.GetString("theme.scheme", "");
    value.schedule = settings.GetString("theme.schedule", "");
    value.accent = settings.GetString("theme.accent", "");
    value.glass = settings.GetString("theme.glass", "");
    value.liveReload = settings.GetBool("theme.live_reload", true);
    value.inspect = settings.GetBool("theme.inspect", false);
    value.parts.colors = settings.GetBool("theme.parts.colors", true);
    value.parts.resources = settings.GetBool("theme.parts.resources", true);
    value.parts.assets = settings.GetBool("theme.parts.assets", true);
    value.parts.script = settings.GetBool("theme.parts.script", true);
    return value;
}

void WriteThemeSettings(Settings& settings, const ThemeSettings& value) {
    settings.Set("theme.active", value.active.empty() ? "none" : value.active);
    settings.Set("theme.scheme", value.scheme);
    // Written only when set, so the settings of someone who never uses it do not grow a key.
    if (value.schedule.empty()) {
        settings.Remove("theme.schedule");
    } else {
        settings.Set("theme.schedule", value.schedule);
    }
    if (value.accent.empty()) {
        settings.Remove("theme.accent");
    } else {
        settings.Set("theme.accent", value.accent);
    }
    if (value.glass.empty()) {
        settings.Remove("theme.glass");
    } else {
        settings.Set("theme.glass", value.glass);
    }
    settings.SetBool("theme.live_reload", value.liveReload);
    settings.SetBool("theme.inspect", value.inspect);
    settings.SetBool("theme.parts.colors", value.parts.colors);
    settings.SetBool("theme.parts.resources", value.parts.resources);
    settings.SetBool("theme.parts.assets", value.parts.assets);
    settings.SetBool("theme.parts.script", value.parts.script);
}

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

namespace {

std::map<std::string, std::string> ReadValues(const Settings& settings, const std::string& prefix) {
    std::map<std::string, std::string> values;
    for (auto it = settings.Values().lower_bound(prefix); it != settings.Values().end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) break;
        values[it->first.substr(prefix.size())] = it->second;
    }
    return values;
}

}  // namespace

std::map<std::string, std::string> ReadThemeValues(const Settings& settings, const std::string& id) {
    if (id.empty()) return {};
    return ReadValues(settings, "themes." + id + ".");
}

std::map<std::string, std::string> ReadSnippetValues(const Settings& settings, const std::string& id) {
    if (id.empty()) return {};
    std::map<std::string, std::string> values = ReadValues(settings, "snippets." + id + ".");
    values.erase("enabled");  // the snippet's switch, not one of its settings
    return values;
}

}  // namespace ampify::theme
