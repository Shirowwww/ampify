#pragma once

// What Ampify changes in Apple Music's own behaviour, from the "Apple Music" part of the Ampify card (Settings,
// Playback): its notifications, and where its full-screen player goes. Stored in settings.txt; the kernel applies them
// (src/win/notifications.cpp, src/win/apple_player.cpp). Portable, so the choices are tested under Linux.

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
};

inline constexpr char kNotificationsKey[] = "apple.notifications";
inline constexpr char kPlayerModeKey[] = "apple.fullscreen";

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

// Writes one choice to settings.txt. Refused for a key or a value the card does not offer.
bool SetAppleTweak(const std::filesystem::path& kit, const std::string& key, const std::string& value, std::string& error);

}  // namespace ampify::uirender
