// The settings Ampify reads, with the value that applies while one is not set. docs/cli.md lists the same keys.

#include "setting_keys.hpp"

#include <array>

namespace ampify::cli {

namespace {

constexpr std::array<KnownSetting, 34> kFixed = {{
    {"theme.active", "", "no theme"},
    {"theme.scheme", "", "the theme's default scheme"},
    {"theme.schedule", "", "no schedule"},
    {"theme.accent", "", "the theme's own accent"},
    {"theme.glass", "full", ""},
    {"theme.live_reload", "1", ""},
    {"theme.inspect", "0", ""},
    {"xaml.watcher", "0", ""},
    {"catalog.url", kDefaultCatalogUrl, ""},
    {"app_launch_flags", "", "no argument"},
    {"check_kit_update", "0", ""},
    {"kit_update", "auto", ""},
    {"expose_api", "1", ""},
    {"discord.enabled", "0", ""},
    {"tray.enabled", "0", ""},
    {"remote.enabled", "0", ""},
    {"remote.port", "8974", ""},
    {"remote.token", "", "created when the remote is turned on"},
    {"midi.enabled", "0", ""},
    {"midi.device", "0", ""},
    {"midi.map", "", "no key linked"},
    {"player.apple", "1", ""},
    {"player.appleCommands", "0", ""},
    {"audio.maxGainDb", "6", ""},
    {"apple.notifications", "apple", ""},
    {"apple.fullscreen", "window", ""},
    {"apple.playerControls", "shown", ""},
    {"apple.close", "quit", ""},
    {"apple.network", "ipv4-first", ""},
    {"apple.artwork", "sharp", ""},
    {"apple.marquee", "hover", ""},
    {"apple.sidebar", "open", ""},
    {"ampify.entry", "sidebar", ""},
    {"language", "", "the Windows language"},
}};

// Families of keys: element switches and the settings each element or theme declares in its manifest.
constexpr std::array<KnownSetting, 5> kPrefixes = {{
    {"theme.parts.", "1", ""},
    {"extensions.", "", "off, or the extension's own default"},
    {"apps.", "", "off, or the app's own default"},
    {"snippets.", "", "off"},
    {"themes.", "", "the theme's own default"},
}};

}  // namespace

std::optional<KnownSetting> FindSetting(std::string_view key) {
    for (const KnownSetting& setting : kFixed) {
        if (setting.key == key) return setting;
    }
    for (const KnownSetting& family : kPrefixes) {
        if (key.size() > family.key.size() && key.substr(0, family.key.size()) == family.key) {
            return KnownSetting{key, family.fallback, family.note};
        }
    }
    return std::nullopt;
}

std::string KnownSettingNames() {
    std::string names;
    for (const KnownSetting& setting : kFixed) names += (names.empty() ? "" : ", ") + std::string(setting.key);
    for (const KnownSetting& family : kPrefixes) names += ", " + std::string(family.key) + "...";
    return names;
}

}  // namespace ampify::cli
