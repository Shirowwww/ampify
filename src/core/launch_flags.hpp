#pragma once

// The `app_launch_flags` setting: extra arguments given to Apple Music when the launcher starts it (the equivalent
// of Spicetify's `spotify_launch_flags`). The setting is one line of text, split here into arguments, then joined
// back into the single argument string that Windows passes to a packaged app on activation. Portable, so the
// splitting and the quoting are tested on Linux; the launcher only hands the result to ActivateApplication.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

inline constexpr std::string_view kLaunchFlagsKey = "app_launch_flags";

// Longest setting accepted, and most arguments in it. Beyond that it is a mistake, not flags.
inline constexpr size_t kMaxLaunchFlagsLength = 1024;
inline constexpr size_t kMaxLaunchFlags = 32;

// Splits the setting into arguments: spaces and tabs separate them, double quotes group spaces into one argument
// ("--name=a b" gives one argument). Nullopt, with the reason in English in `error`, for an unclosed quote, a control
// character, or text over the limits above. Empty text gives no argument.
std::optional<std::vector<std::string>> SplitLaunchFlags(std::string_view text, std::string* error = nullptr);

// Joins arguments with the quoting that CommandLineToArgvW reads back on Windows: an argument with a space, a tab
// or a quote is quoted, and backslashes are doubled only before a quote.
std::string JoinWindowsArguments(const std::vector<std::string>& arguments);

// The argument string for Apple Music from the value of `app_launch_flags`. Empty when the value is empty or
// invalid: an invalid setting never reaches the app (the launcher writes `error` to its journal).
std::string AppLaunchArguments(std::string_view setting, std::string* error = nullptr);

}  // namespace ampify
