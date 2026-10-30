#pragma once

// Names and places the kit uses on disk and in the registry. Shared by the installer, the test kit and the tests.

#include <string>
#include <string_view>
#include <vector>

namespace amk {

// Version shipped in this build. Shown in the uninstall list and in the test report.
inline constexpr std::string_view kKitVersion = "0.5.0";

// Folder name under %LOCALAPPDATA%\Programs (the installed kit) and under %USERPROFILE% (the user's data).
inline constexpr std::string_view kKitFolderName = "AppleMusicModKit";

// Files the installer copies into the install folder. The launcher loads the kernel from its own folder,
// and the installer copies itself there as the uninstaller.
inline constexpr std::string_view kLauncherFile = "amk_launcher.exe";
inline constexpr std::string_view kKernelFile = "amk_kernel.dll";
inline constexpr std::string_view kSetupFile = "amk_setup.exe";

// The start menu shortcut that starts Apple Music through the launcher.
inline constexpr std::string_view kShortcutFile = "Apple Music avec mods.lnk";

// Uninstall entry under HKEY_CURRENT_USER, so the kit shows in Settings > Apps.
inline constexpr std::string_view kUninstallKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AppleMusicModKit";
inline constexpr std::string_view kDisplayName = "Apple Music avec Mod Kit";
inline constexpr std::string_view kPublisher = "Shirowwww";

// The files that make up the installed kit, in the order they are copied.
std::vector<std::string_view> InstalledFiles();

// True for the user's data folder, such as C:\Users\User\AppleMusicModKit. The uninstaller only deletes a
// folder that passes this check, so a wrong path can never remove something else.
bool IsKitDataFolder(std::string_view path);

}  // namespace amk
