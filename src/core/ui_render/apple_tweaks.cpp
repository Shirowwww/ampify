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

const std::vector<std::pair<std::string, std::string>>& CloseOptions() {
    static const std::vector<std::pair<std::string, std::string>> options{
        {"quit", "tweaks.close.quit"},
        {"tray", "tweaks.close.tray"}};
    return options;
}

const std::vector<std::pair<std::string, std::string>>* OptionsOf(const std::string& key) {
    if (key == kNotificationsKey) return &NotificationOptions();
    if (key == kPlayerModeKey) return &PlayerOptions();
    if (key == kCloseKey) return &CloseOptions();
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
        {kCloseKey, "tweaks.close", "tweaks.closeHelp", CloseOptions(), Known(settings, kCloseKey)},
        {kNotificationsKey, "tweaks.notifications", "tweaks.notificationsHelp", std::move(notificationOptions),
         notifications},
    };
}

bool SetAppleTweak(const fs::path& kit, const std::string& key, const std::string& value, std::string& error) {
    const auto* options = OptionsOf(key);
    if (!options) {
        error = "not an Apple Music setting of the card: " + key;
        return false;
    }
    if (std::none_of(options->begin(), options->end(), [&](const auto& option) { return option.first == value; })) {
        error = "invalid value for " + key + ": " + value;
        return false;
    }
    return Settings::UpdateFile((kit / "settings.txt").string(), key, value, error);
}

}  // namespace ampify::uirender
