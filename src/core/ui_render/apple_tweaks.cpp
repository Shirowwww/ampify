#include "apple_tweaks.hpp"

#include <algorithm>

#include "../text_util.hpp"

namespace ampify::uirender {

namespace fs = std::filesystem;

namespace {

// The stored values, in the order the card shows them; the first is the default.
const std::vector<std::pair<std::string, std::string>>& NotificationOptions() {
    static const std::vector<std::pair<std::string, std::string>> options{
        {"apple", "tweaks.notifications.apple"},
        {"background", "tweaks.notifications.background"},
        {"never", "tweaks.notifications.never"}};
    return options;
}

const std::vector<std::pair<std::string, std::string>>& PlayerOptions() {
    static const std::vector<std::pair<std::string, std::string>> options{
        {"window", "tweaks.player.window"},
        {"replace", "tweaks.player.replace"},
        {"work-area", "tweaks.player.workArea"},
        {"screen", "tweaks.player.screen"}};
    return options;
}

const std::vector<std::pair<std::string, std::string>>& PlayerControlsOptions() {
    static const std::vector<std::pair<std::string, std::string>> options{
        {"shown", "tweaks.playerControls.shown"},
        {"hidden", "tweaks.playerControls.hidden"}};
    return options;
}

const std::vector<std::pair<std::string, std::string>>& CloseOptions() {
    static const std::vector<std::pair<std::string, std::string>> options{
        {"quit", "tweaks.close.quit"},
        {"tray", "tweaks.close.tray"}};
    return options;
}

const std::vector<std::pair<std::string, std::string>>& NetworkOptions() {
    static const std::vector<std::pair<std::string, std::string>> options{
        {"ipv4-first", "tweaks.network.ipv4"},
        {"system", "tweaks.network.system"}};
    return options;
}

const std::vector<std::pair<std::string, std::string>>& KitUpdateOptions() {
    static const std::vector<std::pair<std::string, std::string>> options{
        {"auto", "update.auto"},
        {"ask", "update.ask"},
        {"off", "update.off"}};
    return options;
}

const std::vector<std::pair<std::string, std::string>>& GlassOptions() {
    static const std::vector<std::pair<std::string, std::string>> options{
        {"full", "glass.full"},
        {"balanced", "glass.balanced"},
        {"eco", "glass.eco"}};
    return options;
}

const std::vector<std::pair<std::string, std::string>>& EntryOptions() {
    static const std::vector<std::pair<std::string, std::string>> options{
        {"sidebar", "entry.sidebar"},
        {"header", "entry.header"}};
    return options;
}

const std::vector<std::pair<std::string, std::string>>* OptionsOf(const std::string& key) {
    if (key == kGlassChoiceKey) return &GlassOptions();
    if (key == kEntryKey) return &EntryOptions();
    if (key == kNotificationsKey) return &NotificationOptions();
    if (key == kPlayerModeKey) return &PlayerOptions();
    if (key == kPlayerControlsKey) return &PlayerControlsOptions();
    if (key == kCloseKey) return &CloseOptions();
    if (key == kNetworkKey) return &NetworkOptions();
    if (key == kKitUpdateKey) return &KitUpdateOptions();
    return nullptr;
}

// The stored value when the card offers it, else its first option.
std::string Known(const Settings& settings, const std::string& key) {
    const auto& options = *OptionsOf(key);
    std::string value = ToLowerAscii(settings.GetString(key, ""));
    bool known = std::any_of(options.begin(), options.end(), [&](const auto& option) { return option.first == value; });
    return known ? value : options.front().first;
}

}  // namespace

AppleTweaks ReadAppleTweaks(const Settings& settings) {
    AppleTweaks tweaks;
    std::string notifications = Known(settings, kNotificationsKey);
    tweaks.notifications = notifications == "background" ? NotificationRule::Background
                           : notifications == "never"    ? NotificationRule::Never
                                                         : NotificationRule::Apple;
    std::string player = Known(settings, kPlayerModeKey);
    tweaks.player = player == "replace"     ? PlayerMode::Replace
                    : player == "work-area" ? PlayerMode::WorkArea
                    : player == "screen"    ? PlayerMode::Screen
                                            : PlayerMode::Window;
    tweaks.closeToTray = Known(settings, kCloseKey) == "tray";
    tweaks.playerControls = Known(settings, kPlayerControlsKey) == "shown";
    tweaks.ipv4First = Known(settings, kNetworkKey) == "ipv4-first";
    return tweaks;
}

std::vector<TweakChoice> AppleTweakChoices(const fs::path& kit) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    // Apple's own switch (Settings, General) turns its notifications off: the card offers only what it does not, and
    // keeps "never" for someone who chose it before.
    std::string notifications = Known(settings, kNotificationsKey);
    std::vector<std::pair<std::string, std::string>> notificationOptions = NotificationOptions();
    if (notifications != "never") notificationOptions.pop_back();
    return {
        {kPlayerModeKey, "tweaks.player", "tweaks.playerHelp", PlayerOptions(), Known(settings, kPlayerModeKey)},
        {kPlayerControlsKey, "tweaks.playerControls", "tweaks.playerControlsHelp", PlayerControlsOptions(),
         Known(settings, kPlayerControlsKey)},
        {kCloseKey, "tweaks.close", "tweaks.closeHelp", CloseOptions(), Known(settings, kCloseKey)},
        {kNotificationsKey, "tweaks.notifications", "tweaks.notificationsHelp", std::move(notificationOptions),
         notifications},
        {kNetworkKey, "tweaks.network", "tweaks.networkHelp", NetworkOptions(), Known(settings, kNetworkKey)},
    };
}

KitUpdate ReadKitUpdate(const Settings& settings) {
    std::string value = Known(settings, kKitUpdateKey);
    return value == "ask" ? KitUpdate::Ask : value == "off" ? KitUpdate::Off : KitUpdate::Auto;
}

TweakChoice KitUpdateChoice(const fs::path& kit) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    return {kKitUpdateKey, "update.title", "update.help", KitUpdateOptions(), Known(settings, kKitUpdateKey)};
}

bool ReadEntryInHeader(const Settings& settings) { return Known(settings, kEntryKey) == "header"; }

TweakChoice EntryChoice(const fs::path& kit) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    return {kEntryKey, "entry.title", "entry.help", EntryOptions(), Known(settings, kEntryKey)};
}

TweakChoice GlassChoice(const fs::path& kit) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    return {kGlassChoiceKey, "glass.title", "glass.help", GlassOptions(), Known(settings, kGlassChoiceKey)};
}

bool IsAppleTweakValue(const std::string& key, const std::string& value) {
    const auto* options = OptionsOf(key);
    return options &&
           std::any_of(options->begin(), options->end(), [&](const auto& option) { return option.first == value; });
}

std::string AppleTweakValues(const std::string& key) {
    const auto* options = OptionsOf(key);
    std::string joined;
    if (!options) return joined;
    for (const auto& option : *options) joined += (joined.empty() ? "" : ", ") + option.first;
    return joined;
}

bool SetAppleTweak(const fs::path& kit, const std::string& key, const std::string& value, std::string& error) {
    if (!OptionsOf(key)) {
        error = "not an Apple Music setting of the card: " + key;
        return false;
    }
    if (!IsAppleTweakValue(key, value)) {
        error = "invalid value for " + key + ": " + value;
        return false;
    }
    return Settings::UpdateFile((kit / "settings.txt").string(), key, value, error);
}

}  // namespace ampify::uirender
