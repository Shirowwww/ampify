#pragma once

// What Ampify changes in Apple Music's own behaviour, from the "Apple Music" section of Ampify's settings
// page: its notifications, where its full-screen player goes and what closing its window does. Stored in
// settings.txt; the kernel applies them (src/win/notifications.cpp, src/win/apple_player.cpp,
// src/win/close_to_tray.cpp). Portable, so the choices are tested under Linux.

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "../settings.hpp"

namespace ampify::uirender {

// Apple Music's notifications, such as the one of each new song: as Apple Music shows them, only when Apple Music is
// not the window in front, or never. Apple's own switch (Settings, General, Notifications) still applies first.
enum class NotificationRule { Apple, Background, Never };

// Where Apple's full-screen player goes, from Ampify's Now Playing button or Apple's own: the whole screen (Apple's
// way), the screen above the taskbar, over the Apple Music window, or in place of that window (one window, not two).
enum class PlayerMode { Screen, WorkArea, Window, Replace };

struct AppleTweaks {
    NotificationRule notifications = NotificationRule::Apple;
    PlayerMode player = PlayerMode::Window;
    // Closing the window hides it behind an icon of the notification area; the music goes on (win/close_to_tray.cpp).
    bool closeToTray = false;
    // Shuffle and repeat added around the full-screen player's buttons (win/player_controls.cpp).
    bool playerControls = true;
    // Servers' IPv4 addresses tried before their IPv6 ones (core/address_order.hpp, win/network_fix.cpp): songs stuck
    // at 0:00 where a server's IPv6 does not answer. Read at start: a change applies when Apple Music starts again.
    bool ipv4First = true;
    // The full-screen player's cover replaced by a sharper one when Apple's is shown larger than it is
    // (win/sharp_artwork.cpp).
    bool sharpArtwork = true;
    // A text cut with an ellipsis scrolls under the pointer (win/text_marquee.cpp).
    bool textMarquee = true;
    // The sidebar folded to its icons, opening over the pages under the pointer (win/sidebar_hover.cpp).
    bool sidebarOnHover = false;
    // Freezes of Apple Music's window written to its log, with what Ampify was doing (win/ui_watchdog.cpp). Off by default.
    bool freezeLog = false;
    // A library agent left behind by a killed Apple Music is ended once Apple Music is closed (launcher). Off by default.
    bool orphanAgent = false;
};

inline constexpr char kNotificationsKey[] = "apple.notifications";
inline constexpr char kPlayerModeKey[] = "apple.fullscreen";
inline constexpr char kPlayerControlsKey[] = "apple.playerControls";
inline constexpr char kCloseKey[] = "apple.close";
inline constexpr char kNetworkKey[] = "apple.network";
inline constexpr char kArtworkKey[] = "apple.artwork";
inline constexpr char kMarqueeKey[] = "apple.marquee";
inline constexpr char kSidebarKey[] = "apple.sidebar";
inline constexpr char kFreezeLogKey[] = "apple.watchdog";
inline constexpr char kOrphanAgentKey[] = "apple.orphanAgent";

// An unknown value reads as the default.
AppleTweaks ReadAppleTweaks(const Settings& settings);

// One choice of the card: its settings key, the i18n keys of its label and help, its options (stored value, i18n key
// of its text) and the value stored now.
struct TweakChoice {
    std::string key;
    std::string labelKey;
    std::string helpKey;
    std::vector<std::pair<std::string, std::string>> options;
    std::string value;
};
std::vector<TweakChoice> AppleTweakChoices(const std::filesystem::path& kit);

// New releases of Ampify (kit_update): installed on their own while Apple Music is closed, after a question, or never.
// The background launcher runs `ampify update --auto`, which follows this choice (src/cli/maintenance.cpp).
enum class KitUpdate { Auto, Ask, Off };
inline constexpr char kKitUpdateKey[] = "kit_update";
KitUpdate ReadKitUpdate(const Settings& settings);
TweakChoice KitUpdateChoice(const std::filesystem::path& kit);

// Where Ampify's own entry (the Marketplace's) stands: in the sidebar's list, or in its header next to Apple's "···"
// while the sidebar is open (win/ui_service_sidebar.cpp). Folded, Apple hides the header: the entry stays in the list.
inline constexpr char kEntryKey[] = "ampify.entry";
bool ReadEntryInHeader(const Settings& settings);
TweakChoice EntryChoice(const std::filesystem::path& kit);

// How much glass the themes may draw (theme.glass, read by the theme engine in theme/glass.hpp, which this library
// cannot include): full, balanced or eco.
inline constexpr char kGlassChoiceKey[] = "theme.glass";
TweakChoice GlassChoice(const std::filesystem::path& kit);

// True when the card offers `value` for `key` (stored values, such as "auto" for kit_update).
bool IsAppleTweakValue(const std::string& key, const std::string& value);

// The values the card offers for `key`, joined as "a, b, c"; empty when the card has no such key.
std::string AppleTweakValues(const std::string& key);

// Writes one choice to settings.txt. Refused for a key or a value the card does not offer.
bool SetAppleTweak(const std::filesystem::path& kit, const std::string& key, const std::string& value, std::string& error);

}  // namespace ampify::uirender
