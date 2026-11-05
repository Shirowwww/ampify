#pragma once

// Ampify loads with Apple Music each time it starts, the way Spicetify stays applied to Spotify. Apple Music's package
// is read-only, so nothing in it is changed. Windows starts the launcher with each process of the package
// (src/win/package_debug.hpp), so nothing waits in the background; at sign-in (HKCU\...\Run) the launcher only
// registers again, for an Apple Music updated since (--register), and ends. Where Windows refuses that setting, the
// launcher stays in the background instead and loads the kernel as soon as Apple Music starts (--background, the
// behaviour of Ampify 1.1 and before). Shared by the launcher, the installer and the command line. Windows only.

#include <windows.h>

#include <filesystem>

namespace ampify::win {

inline constexpr wchar_t kBackgroundArgument[] = L"--background";
inline constexpr wchar_t kRegisterArgument[] = L"--register";

// The value under HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run that starts the background launcher.
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValue[] = L"Ampify";

// One background launcher per session: it holds this mutex, and stops when this event is set.
inline constexpr wchar_t kBackgroundMutex[] = L"Local\\AmpifyBackground";
inline constexpr wchar_t kBackgroundStopEvent[] = L"Local\\AmpifyBackgroundStop";

// True when Windows starts the launcher at sign-in.
bool BackgroundRegistered();

// Makes Windows start `launcher` --register at each sign-in, and with each process of Apple Music. False when the
// sign-in entry is refused; a refused package setting is caught up by StartBackground.
bool RegisterBackground(const std::filesystem::path& launcher);

// Removes the sign-in entry and the package setting. True when both are gone, including when they were never there.
bool UnregisterBackground();

// True when Ampify loads with Apple Music: Windows starts the launcher with it, or a background launcher runs.
bool BackgroundRunning();

// True while a launcher stays in the background in this session (--background).
bool ResidentRunning();

// Has Ampify load with Apple Music from now on: runs `launcher` --register, without waiting, unless that is already
// so. True when it started or nothing was needed.
bool StartBackground(const std::filesystem::path& launcher);

// Asks a launcher left in the background to stop and waits for it at most `timeoutMs`. True when none runs any more.
// The package setting stays: UnregisterBackground removes it.
bool StopBackground(DWORD timeoutMs = 5000);

}  // namespace ampify::win
