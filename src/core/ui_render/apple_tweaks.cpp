#include "apple_tweaks.hpp"

#include <algorithm>
#include <span>

#include "../lyrics_window.hpp"
#include "../user_order.hpp"

namespace ampify::uirender {

namespace fs = std::filesystem;

namespace {

// The rows of each section of the page, in the order it shows them.
// The playback bar's place and the sidebar's opening sit on the Look tab instead, under the theme they change
// (PlaybarChoice, SongRoomChoice, SidebarChoice).
constexpr const char* kWindowKeys[] = {kTopbarKey, kEntryKey, kMarqueeKey, kCloseKey, kNotificationsKey};
constexpr const char* kPlayerKeys[] = {kPlayerModeKey, kPlayerControlsKey, kLyricsBeforeKey, kLyricsAfterKey,
                                       kArtworkKey};
constexpr const char* kReliabilityKeys[] = {kNetworkKey, kProxyKey, kOrphanAgentKey, kFreezeLogKey};

std::span<const char* const> KeysOf(TweakGroup group) {
    switch (group) {
        case TweakGroup::Window: return kWindowKeys;
        case TweakGroup::Player: return kPlayerKeys;
        case TweakGroup::Reliability: return kReliabilityKeys;
    }
    return {};
}

TweakChoice ChoiceFrom(const Settings& settings, std::string_view key) {
    const KitSetting& row = *FindKitSetting(key);
    TweakChoice choice{std::string(key), std::string(row.labelKey), std::string(row.helpKey), {},
                       ChoiceOf(settings, key)};
    for (const SettingOption& option : row.options) choice.options.emplace_back(option.value, option.labelKey);
    return choice;
}

TweakChoice ChoiceIn(const fs::path& kit, std::string_view key) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    return ChoiceFrom(settings, key);
}

}  // namespace

AppleTweaks ReadAppleTweaks(const Settings& settings) {
    AppleTweaks tweaks;
    std::string notifications = ChoiceOf(settings, kNotificationsKey);
    tweaks.notifications = notifications == "background" ? NotificationRule::Background
                           : notifications == "never"    ? NotificationRule::Never
                                                         : NotificationRule::Apple;
    std::string player = ChoiceOf(settings, kPlayerModeKey);
    tweaks.player = player == "replace"     ? PlayerMode::Replace
                    : player == "screen"    ? PlayerMode::Screen
                                            : PlayerMode::Window;
    tweaks.closeToTray = ChoiceOf(settings, kCloseKey) == "tray";
    tweaks.playerControls = ChoiceOf(settings, kPlayerControlsKey) == "shown";
    tweaks.ipv4First = ChoiceOf(settings, kNetworkKey) == "ipv4-first";
    tweaks.proxyNoAutodetect = ChoiceOf(settings, kProxyKey) == "no-autodetect";
    tweaks.sharpArtwork = ChoiceOf(settings, kArtworkKey) == "sharp";
    tweaks.textMarquee = ChoiceOf(settings, kMarqueeKey) == "hover";
    tweaks.sidebarOnHover = ChoiceOf(settings, kSidebarKey) == "hover";
    tweaks.freezeLog = ChoiceOf(settings, kFreezeLogKey) == "on";
    tweaks.topbarSearch = ChoiceOf(settings, kTopbarKey) == "search";
    tweaks.lyricsBefore = LyricLinesOf(ChoiceOf(settings, kLyricsBeforeKey));
    tweaks.lyricsAfter = LyricLinesOf(ChoiceOf(settings, kLyricsAfterKey));
    tweaks.playbarOrder = ParseOrder(settings.Get(kPlaybarOrderKey).value_or(""));
    tweaks.sidebarEntriesOrder = ParseOrder(settings.Get(kSidebarEntriesOrderKey).value_or(""));
    return tweaks;
}

std::vector<TweakChoice> AppleTweakChoices(const fs::path& kit, TweakGroup group) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    std::vector<TweakChoice> choices;
    for (const char* key : KeysOf(group)) choices.push_back(ChoiceFrom(settings, key));
    // Apple's own switch (Settings, General) turns its notifications off: the card offers only what it does not, and
    // keeps "never" for someone who chose it before.
    for (TweakChoice& choice : choices) {
        if (choice.key != kNotificationsKey || choice.value == "never") continue;
        std::erase_if(choice.options, [](const auto& option) { return option.first == "never"; });
    }
    return choices;
}

KitUpdate ReadKitUpdate(const Settings& settings) {
    std::string value = ChoiceOf(settings, kKitUpdateKey);
    return value == "ask" ? KitUpdate::Ask : value == "off" ? KitUpdate::Off : KitUpdate::Auto;
}

TweakChoice KitUpdateChoice(const fs::path& kit) { return ChoiceIn(kit, kKitUpdateKey); }

bool ReadEntryInHeader(const Settings& settings) { return ChoiceOf(settings, kEntryKey) == "header"; }

TweakChoice EntryChoice(const fs::path& kit) { return ChoiceIn(kit, kEntryKey); }

TweakChoice GlassChoice(const fs::path& kit) { return ChoiceIn(kit, kGlassChoiceKey); }

TweakChoice PlaybarChoice(const fs::path& kit) { return ChoiceIn(kit, kPlaybarKey); }

TweakChoice SongRoomChoice(const fs::path& kit) { return ChoiceIn(kit, kSongRoomKey); }

TweakChoice SidebarChoice(const fs::path& kit) { return ChoiceIn(kit, kSidebarKey); }

bool IsAppleTweakValue(const std::string& key, const std::string& value) { return IsOfferedValue(key, value); }

std::string AppleTweakValues(const std::string& key) { return OfferedValues(key); }

bool SetAppleTweak(const fs::path& kit, const std::string& key, const std::string& value, std::string& error) {
    if (OfferedValues(key).empty()) {
        error = "not an Apple Music setting of the card: " + key;
        return false;
    }
    if (!IsOfferedValue(key, value)) {
        error = "invalid value for " + key + ": " + value;
        return false;
    }
    return Settings::UpdateFile((kit / "settings.txt").string(), key, value, error);
}

}  // namespace ampify::uirender
