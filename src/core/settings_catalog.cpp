#include "settings_catalog.hpp"

#include <algorithm>
#include <cstdlib>

#include "release_facts.hpp"
#include "remote_http.hpp"
#include "text_util.hpp"

namespace ampify {

namespace {

using K = SettingKind;

KitSetting Toggle(std::string_view key, std::string_view fallback) {
    return {.key = key, .kind = K::Toggle, .fallback = fallback};
}

KitSetting Text(std::string_view key, std::string_view note) { return {.key = key, .note = note}; }

KitSetting Choice(std::string_view key, std::string_view label, std::string_view help,
                  std::vector<SettingOption> options) {
    std::string_view fallback = options.front().value;  // the first value the page shows is the default
    return {.key = key,
            .kind = K::Choice,
            .fallback = fallback,
            .options = std::move(options),
            .labelKey = label,
            .helpKey = help};
}

// All the lines (the default, as Apple shows them), or 0 to 5 of them.
std::vector<SettingOption> LyricLineOptions() {
    return {{"all", "tweaks.lyricsLines.all"}, {"0", "tweaks.lyricsLines.0"}, {"1", "tweaks.lyricsLines.1"},
            {"2", "tweaks.lyricsLines.2"},     {"3", "tweaks.lyricsLines.3"}, {"4", "tweaks.lyricsLines.4"},
            {"5", "tweaks.lyricsLines.5"}};
}

const std::vector<KitSetting>& Fixed() {
    static const std::vector<KitSetting> rows{
        Text(kThemeActiveKey, "no theme"),
        Text("theme.scheme", "the theme's default scheme"),
        Text("theme.schedule", "no schedule"),
        Text("theme.accent", "the theme's own accent"),
        Choice(kGlassChoiceKey, "glass.title", "glass.help",
                    {{"full", "glass.full"}, {"balanced", "glass.balanced"}, {"eco", "glass.eco"}}),
        Toggle(kLiveReloadKey, "1"),
        Toggle(kInspectKey, "0"),
        Toggle(kXamlWatcherKey, "0"),
        {.key = "catalog.url", .fallback = kDefaultCatalogUrl},
        Text("app_launch_flags", "no argument"),
        Toggle(kCheckKitUpdateKey, "0"),
        Choice(kKitUpdateKey, "update.title", "update.help",
                    {{"auto", "update.auto"}, {"ask", "update.ask"}, {"off", "update.off"}}),
        Toggle(kExposeApiKey, "1"),
        Toggle(kDiscordKey, "0"),
        Toggle(kTrayKey, "0"),
        Toggle(kRemoteKey, "0"),
        {.key = kRemotePortKey, .kind = K::Integer, .fallback = "8974", .low = 1024, .high = 65535},
        {.key = kRemoteTokenKey, .note = "created when the remote is turned on", .secret = true},
        Toggle(kMidiKey, "0"),
        {.key = kMidiDeviceKey, .kind = K::Integer, .fallback = "0", .low = 0, .high = 255},
        Text("midi.map", "no key linked"),
        Text(kPlaybarOrderKey, "Apple's order"),
        Text(kSidebarEntriesOrderKey, "the order the entries were added in"),
        Toggle(kPlayerAppleKey, "1"),
        Toggle(kPlayerAppleCommandsKey, "0"),
        {.key = kGainCeilingSetting, .kind = K::Number, .fallback = "6", .low = 0, .high = 12},
        Choice(kNotificationsKey, "tweaks.notifications", "tweaks.notificationsHelp",
                    {{"apple", "tweaks.notifications.apple"},
                     {"background", "tweaks.notifications.background"},
                     {"never", "tweaks.notifications.never"}}),
        Choice(kPlayerModeKey, "tweaks.player", "tweaks.playerHelp",
                    {{"window", "tweaks.player.window"},
                     {"replace", "tweaks.player.replace"},
                     {"screen", "tweaks.player.screen"}}),
        Choice(kPlayerControlsKey, "tweaks.playerControls", "tweaks.playerControlsHelp",
                    {{"shown", "tweaks.playerControls.shown"}, {"hidden", "tweaks.playerControls.hidden"}}),
        Choice(kCloseKey, "tweaks.close", "tweaks.closeHelp",
                    {{"quit", "tweaks.close.quit"}, {"tray", "tweaks.close.tray"}}),
        Choice(kNetworkKey, "tweaks.network", "tweaks.networkHelp",
                    {{"ipv4-first", "tweaks.network.ipv4"}, {"system", "tweaks.network.system"}}),
        Choice(kProxyKey, "tweaks.proxy", "tweaks.proxyHelp",
                    {{"system", "tweaks.proxy.system"}, {"no-autodetect", "tweaks.proxy.noAutodetect"}}),
        Choice(kArtworkKey, "tweaks.artwork", "tweaks.artworkHelp",
                    {{"sharp", "tweaks.artwork.sharp"}, {"apple", "tweaks.artwork.apple"}}),
        Choice(kMarqueeKey, "tweaks.marquee", "tweaks.marqueeHelp",
                    {{"hover", "tweaks.marquee.hover"}, {"off", "tweaks.marquee.off"}}),
        Choice(kSidebarKey, "tweaks.sidebar", "tweaks.sidebarHelp",
                    {{"open", "tweaks.sidebar.open"}, {"hover", "tweaks.sidebar.hover"}}),
        Choice(kFreezeLogKey, "tweaks.watchdog", "tweaks.watchdogHelp", {{"on", "ui.on"}, {"off", "ui.off"}}),
        Choice(kOrphanAgentKey, "tweaks.orphan", "tweaks.orphanHelp",
                    {{"on", "tweaks.orphan.on"}, {"off", "ui.off"}}),
        Choice(kSongRoomKey, "tweaks.songRoom", "tweaks.songRoomHelp",
                    {{"theme", "tweaks.songRoom.theme"},
                     {"wider", "tweaks.songRoom.wider"},
                     {"widest", "tweaks.songRoom.widest"}}),
        Choice(kPlaybarKey, "tweaks.playbar", "tweaks.playbarHelp",
                    {{"theme", "tweaks.playbar.theme"},
                     {"top", "tweaks.playbar.top"},
                     {"bottom", "tweaks.playbar.bottom"},
                     {"centred", "tweaks.playbar.centred"}}),
        Choice(kTopbarKey, "tweaks.topbar", "tweaks.topbarHelp",
                    {{"off", "tweaks.topbar.off"}, {"search", "tweaks.topbar.search"}}),
        Choice(kLyricsBeforeKey, "tweaks.lyricsBefore", "tweaks.lyricsBeforeHelp", LyricLineOptions()),
        Choice(kLyricsAfterKey, "tweaks.lyricsAfter", "tweaks.lyricsAfterHelp", LyricLineOptions()),
        Choice(kEntryKey, "entry.title", "entry.help", {{"sidebar", "entry.sidebar"}, {"header", "entry.header"}}),
        Text("language", "the Windows language"),
    };
    return rows;
}

// The element switches and the settings each element or theme declares in its manifest.
const std::vector<KitSetting>& Families() {
    static const std::vector<KitSetting> rows{
        Toggle("theme.parts.", "1"),
        Text("extensions.", "off, or the extension's own default"),
        Text("apps.", "off, or the app's own default"),
        Text("snippets.", "off"),
        Text("themes.", "the theme's own default"),
    };
    return rows;
}

bool HasSwitchSuffix(std::string_view family, std::string_view key) {
    constexpr std::string_view kSwitch = ".enabled";
    bool switches = family == "extensions." || family == "apps." || family == "snippets.";
    return switches && key.size() > kSwitch.size() && key.substr(key.size() - kSwitch.size()) == kSwitch;
}

const SettingOption* OptionOf(const KitSetting* row, std::string_view value) {
    if (!row) return nullptr;
    auto found = std::find_if(row->options.begin(), row->options.end(),
                              [&](const SettingOption& option) { return option.value == value; });
    return found == row->options.end() ? nullptr : &*found;
}

}  // namespace

std::span<const KitSetting> FixedKitSettings() { return Fixed(); }

std::span<const KitSetting> KitSettingFamilies() { return Families(); }

const KitSetting* FindKitSetting(std::string_view key) {
    for (const KitSetting& row : Fixed()) {
        if (row.key == key) return &row;
    }
    for (const KitSetting& family : Families()) {
        if (key.size() > family.key.size() && key.substr(0, family.key.size()) == family.key) return &family;
    }
    return nullptr;
}

std::optional<SettingKind> KindOfSetting(std::string_view key) {
    const KitSetting* row = FindKitSetting(key);
    if (!row) return std::nullopt;
    return HasSwitchSuffix(row->key, key) ? SettingKind::Toggle : row->kind;
}

int IntegerOf(std::string_view key, const std::optional<std::string>& stored) {
    const KitSetting* row = FindKitSetting(key);
    int fallback = row ? std::atoi(std::string(row->fallback).c_str()) : 0;
    if (!row || !stored || stored->empty()) return fallback;
    char* end = nullptr;
    long value = std::strtol(stored->c_str(), &end, 10);
    if (!end || *end != '\0' || value < row->low || value > row->high) return fallback;
    return static_cast<int>(value);
}

bool ToggleOf(const Settings& settings, std::string_view key) {
    const KitSetting* row = FindKitSetting(key);
    return settings.GetBool(std::string(key), row && row->fallback == "1");
}

std::string ChoiceOf(const Settings& settings, std::string_view key) {
    const KitSetting* row = FindKitSetting(key);
    if (!row) return {};
    std::string value = ToLowerAscii(settings.GetString(std::string(key), ""));
    return OptionOf(row, value) ? value : std::string(row->fallback);
}

bool IsOfferedValue(std::string_view key, std::string_view value) {
    const KitSetting* row = FindKitSetting(key);
    return row && row->kind == SettingKind::Choice && OptionOf(row, value);
}

std::string OfferedValues(std::string_view key) {
    const KitSetting* row = FindKitSetting(key);
    std::string joined;
    if (!row || row->kind != SettingKind::Choice) return joined;
    for (const SettingOption& option : row->options) joined += (joined.empty() ? "" : ", ") + std::string(option.value);
    return joined;
}

std::string ActiveThemeId(const Settings& settings) {
    std::string id = settings.GetString(kThemeActiveKey, "");
    return ToLowerAscii(id) == "none" ? std::string() : id;
}

bool IsSecretSetting(std::string_view key) {
    const KitSetting* row = FindKitSetting(key);
    return row && row->secret;
}

bool FillDerivedSettings(Settings& settings, std::string_view key) {
    if (key != kRemoteKey || !ToggleOf(settings, kRemoteKey)) return false;
    std::optional<std::string> token = RemoteTokenToCreate(settings.Get(kRemoteTokenKey));
    return token && settings.Set(kRemoteTokenKey, *token);
}

std::optional<std::string> RemoteTokenToCreate(const std::optional<std::string>& stored) {
    if (stored && remote::IsValidToken(*stored)) return std::nullopt;
    return remote::NewToken();
}

}  // namespace ampify
