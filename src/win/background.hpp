#pragma once

// Ampify loads with Apple Music each time it starts, the way Spicetify stays applied to Spotify. Apple Music's package
// is read-only, so nothing in it is changed: the launcher runs in the background from Windows sign-in
// (ampify_launcher.exe --background, from HKCU\...\Run) and loads the kernel into Apple Music as soon as it starts.
// Shared by the launcher, the installer and the command line. Windows only.

#include <windows.h>

#include <filesystem>

namespace ampify::win {

inline constexpr wchar_t kBackgroundArgument[] = L"--background";

// The value under HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run that starts the background launcher.
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValue[] = L"Ampify";

// One background launcher per session: it holds this mutex, and stops when this event is set.
inline constexpr wchar_t kBackgroundMutex[] = L"Local\\AmpifyBackground";
inline constexpr wchar_t kBackgroundStopEvent[] = L"Local\\AmpifyBackgroundStop";

// True when Windows starts the background launcher at sign-in.
bool BackgroundRegistered();

// Makes Windows start `launcher` --background at each sign-in. False when the registry refuses.
bool RegisterBackground(const std::filesystem::path& launcher);

// Removes the sign-in entry. True when it is gone, including when it was never there.
bool UnregisterBackground();

// True while a background launcher runs in this session.
bool BackgroundRunning();

// Starts `launcher` --background now, without waiting. True when it started or one already runs.
bool StartBackground(const std::filesystem::path& launcher);

// Asks the background launcher to stop and waits for it at most `timeoutMs`. True when none runs any more.
bool StopBackground(DWORD timeoutMs = 5000);

}  // namespace ampify::win
