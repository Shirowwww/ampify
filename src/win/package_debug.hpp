#pragma once

// Ampify loads with Apple Music without a process waiting in the background. Windows can start a "debugger" of the
// user's choice each time a process of a package starts (IPackageDebugSettings, per user, no administrator rights): it
// creates the process suspended, runs `<command> -p <process id> -tid <thread id>`, and the process starts only when
// that thread is resumed. Ampify gives `ampify_launcher.exe --attach` as the command: it loads the kernel before
// Apple Music runs a single line, then resumes it (src/launcher/launcher.cpp). Windows only.
//
// The setting belongs to one version of the package: an update of Apple Music drops it, and the launcher registers
// again at the next sign-in (`--register`, from HKCU\...\Run). While the setting is on, Apple Music does not start at
// all if the command is missing, so ampify restore and the uninstaller turn it off before removing anything.

#include <windows.h>

#include <filesystem>
#include <string>

namespace ampify::win {

inline constexpr wchar_t kAttachArgument[] = L"--attach";

// Makes Windows run `launcher` --attach each time a process of the package `fullName` starts.
HRESULT EnableAttach(const std::wstring& fullName, const std::filesystem::path& launcher);

// Turns the setting off for `fullName`. S_OK when it was not on.
HRESULT DisableAttach(const std::wstring& fullName);

// True when Windows starts a debugger for `fullName` whose command names `launcher` (a path or a file name). Reads
// where Windows keeps the setting (HKCU\Software\Classes\PackagesDebug), which is not documented: false when it
// cannot tell.
bool AttachRegistered(const std::wstring& fullName, const std::filesystem::path& launcher);

// The full name of the installed Apple Music package, empty when it is not installed.
std::wstring AppleMusicFullName();

// The process and thread ids Windows passes to the debugger (`-p <id> -tid <id>`). False when one is missing.
bool ParseAttachArguments(int argc, wchar_t** argv, DWORD& processId, DWORD& threadId);

}  // namespace ampify::win
