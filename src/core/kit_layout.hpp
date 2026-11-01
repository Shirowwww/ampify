#pragma once

// Names and places the kit uses on disk and in the registry. Shared by the installer, the command line and the tests.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

// Version shipped in this build. Shown in the uninstall list.
inline constexpr std::string_view kKitVersion = "1.0.2";

// Folder name under %LOCALAPPDATA%\Programs (the installed kit) and under %USERPROFILE% (the user's data).
inline constexpr std::string_view kKitFolderName = "Ampify";

// Files the installer copies into the install folder. The launcher loads the kernel from its own folder,
// and the installer copies itself there as the uninstaller.
inline constexpr std::string_view kLauncherFile = "ampify_launcher.exe";
inline constexpr std::string_view kKernelFile = "ampify_kernel.dll";
inline constexpr std::string_view kSetupFile = "ampify_setup.exe";
inline constexpr std::string_view kCliFile = "ampify.exe";  // beside the kernel, which runs it for the Marketplace page

// The start menu shortcut that starts Apple Music through the launcher.
inline constexpr std::string_view kShortcutFile = "Apple Music with Ampify.lnk";

// Uninstall entry under HKEY_CURRENT_USER, so the kit shows in Settings > Apps.
inline constexpr std::string_view kUninstallKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Ampify";
inline constexpr std::string_view kDisplayName = "Ampify for Apple Music";
inline constexpr std::string_view kPublisher = "Shirowwww";

// The files that make up the installed kit, in the order they are copied.
std::vector<std::string_view> InstalledFiles();

// True for the user's data folder, such as C:\Users\User\Ampify. The uninstaller only deletes a
// folder that passes this check, so a wrong path can never remove something else.
bool IsKitDataFolder(std::string_view path);

// The elements that ship with the kit, as <folder>/<id>: under examples\ in the package, under the user's data folder
// once installed. They are Ampify's own, so they start until the user turns them off, and each install or update
// replaces them, as it replaces the binaries.
std::vector<std::string_view> BundledElements();

// True for one of BundledElements(): `folder` is "apps" or "extensions", `id` the element's folder name.
bool IsBundledElement(std::string_view folder, std::string_view id);

// Copies the bundled elements from <package>\examples into the data folder `kit`, each in place of the copy already
// there. Returns one line per element left as it was, such as one missing from the package.
std::vector<std::string> InstallBundledElements(const std::filesystem::path& package, const std::filesystem::path& kit);

}  // namespace ampify
