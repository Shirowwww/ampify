#pragma once

// The keys of settings.txt that Ampify itself reads: what each holds, what applies while it is not set and the values
// it accepts. One row per key in settings_catalog.cpp, read by the kernel, the launcher, Ampify's settings page and the
// command line; docs/cli.md describes the same rows, and a test compares the two.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "settings.hpp"

namespace ampify {

enum class SettingKind { Toggle, Choice, Integer, Number, Text };

// A value a choice offers, with the i18n key of its text on Ampify's settings page.
struct SettingOption {
    std::string_view value;
    std::string_view labelKey;
};

struct KitSetting {
    std::string_view key;  // a whole key, or for a family the prefix its keys start with
    SettingKind kind = SettingKind::Text;
    std::string_view fallback;  // what applies while the key is not set, as `ampify config <key>` prints it
    std::string_view note;      // that default in words, when the value alone would be empty
    std::vector<SettingOption> options;  // a choice's values, in the order the settings page shows them
    double low = 0;
    double high = 0;  // the range of an Integer or a Number
    std::string_view labelKey;
    std::string_view helpKey;  // a choice of the settings page: the i18n keys of its label and its help
    bool secret = false;       // stays on this computer: left out of exports and profiles, hidden in reports
};

inline constexpr char kThemeActiveKey[] = "theme.active";
inline constexpr char kThemeScriptKey[] = "theme.parts.script";
inline constexpr char kThemeColorsKey[] = "theme.parts.colors";
inline constexpr char kThemeResourcesKey[] = "theme.parts.resources";
inline constexpr char kThemeAssetsKey[] = "theme.parts.assets";
inline constexpr char kLiveReloadKey[] = "theme.live_reload";
inline constexpr char kInspectKey[] = "theme.inspect";
inline constexpr char kXamlWatcherKey[] = "xaml.watcher";
inline constexpr char kCheckKitUpdateKey[] = "check_kit_update";
inline constexpr char kExposeApiKey[] = "expose_api";
inline constexpr char kPlayerAppleKey[] = "player.apple";
inline constexpr char kPlayerAppleCommandsKey[] = "player.appleCommands";
inline constexpr char kDiscordKey[] = "discord.enabled";
inline constexpr char kTrayKey[] = "tray.enabled";
inline constexpr char kRemoteKey[] = "remote.enabled";
inline constexpr char kRemotePortKey[] = "remote.port";
inline constexpr char kRemoteTokenKey[] = "remote.token";
inline constexpr char kMidiKey[] = "midi.enabled";
inline constexpr char kMidiDeviceKey[] = "midi.device";
inline constexpr char kGainCeilingSetting[] = "audio.maxGainDb";
inline constexpr char kNotificationsKey[] = "apple.notifications";
inline constexpr char kPlayerModeKey[] = "apple.fullscreen";
inline constexpr char kPlayerControlsKey[] = "apple.playerControls";
inline constexpr char kCloseKey[] = "apple.close";
inline constexpr char kNetworkKey[] = "apple.network";
inline constexpr char kProxyKey[] = "apple.proxy";
inline constexpr char kArtworkKey[] = "apple.artwork";
inline constexpr char kMarqueeKey[] = "apple.marquee";
inline constexpr char kSidebarKey[] = "apple.sidebar";
inline constexpr char kFreezeLogKey[] = "apple.watchdog";
inline constexpr char kOrphanAgentKey[] = "apple.orphanAgent";
inline constexpr char kTopbarKey[] = "apple.topbar";
inline constexpr char kKitUpdateKey[] = "kit_update";
inline constexpr char kEntryKey[] = "ampify.entry";
inline constexpr char kGlassChoiceKey[] = "theme.glass";

// The whole keys, in the order `ampify config` names them, and the families (extensions.<id>.enabled...).
std::span<const KitSetting> FixedKitSettings();
std::span<const KitSetting> KitSettingFamilies();

// The row of `key`, or of its family; null for a key Ampify does not read.
const KitSetting* FindKitSetting(std::string_view key);

// What `key` holds. In the extensions, apps and snippets families, <id>.enabled is the element's switch.
std::optional<SettingKind> KindOfSetting(std::string_view key);

// An Integer's stored value when it is a whole number in its range, else its default.
int IntegerOf(std::string_view key, const std::optional<std::string>& stored);

// A toggle's stored value when it is a switch word (ParseToggle), else the default of its row; false for a key with
// no row.
bool ToggleOf(const Settings& settings, std::string_view key);

// A choice's stored value when it offers it (in any case), else its default.
std::string ChoiceOf(const Settings& settings, std::string_view key);

// True when the choice `key` offers exactly `value`; its values joined as "a, b, c" (empty for a key with no choices).
bool IsOfferedValue(std::string_view key, std::string_view value);
std::string OfferedValues(std::string_view key);

// theme.active, with an empty value and `none` (in any case) both meaning no theme.
std::string ActiveThemeId(const Settings& settings);

bool IsSecretSetting(std::string_view key);

// A new remote.token while the stored one is missing or not valid, else nothing: the command line and the kernel
// both create it through this.
std::optional<std::string> RemoteTokenToCreate(const std::optional<std::string>& stored);

// Fills in what a change of `key` needs: a remote.token once remote.enabled is on. True when a value was created.
bool FillDerivedSettings(Settings& settings, std::string_view key);

}  // namespace ampify
