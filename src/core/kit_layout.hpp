#pragma once

// Names and places the kit uses on disk and in the registry. Shared by the installer, the command line and the tests.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "release_facts.hpp"

namespace ampify {

// The Start menu shortcut of versions up to 1.0.2, which started Apple Music through the launcher. Removed on install.
inline constexpr std::string_view kShortcutFile = "Apple Music with Ampify.lnk";

// Uninstall entry under HKEY_CURRENT_USER, so the kit shows in Settings > Apps.
inline constexpr std::string_view kUninstallKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Ampify";
inline constexpr std::string_view kDisplayName = "Ampify for Apple Music";

// The files that make up the installed kit, in the order they are copied.
std::vector<std::string_view> InstalledFiles();

// True for a file that `ampify update --apply` leaves in the install folder besides InstalledFiles(): the old copy of
// a program it replaced (name.old, name.1.old, name.last.old) and the text files of the package (BUILD_INFO.txt,
// README.txt, SHA256SUMS.txt). The uninstaller removes them, and nothing else it did not put there.
bool IsUpdateLeftover(std::string_view fileName);

// True for the user's data folder, such as C:\Users\User\Ampify. The uninstaller only deletes a
// folder that passes this check, so a wrong path can never remove something else.
bool IsKitDataFolder(std::string_view path);

// The elements of kKitElements that ship as a folder, as <folder>/<id>: under examples\ in the package, under the
// user's data folder once installed. Each install or update replaces them, as it replaces the binaries.
std::vector<std::string> BundledElements();

// True for one of BundledElements(): `folder` is "apps" or "extensions", `id` the element's folder name.
bool IsBundledElement(std::string_view folder, std::string_view id);

// True for an element of kKitElements that is on until the user turns it off. Any other element, a community one
// included, waits to be turned on.
bool IsOnByDefault(std::string_view folder, std::string_view id);

// True for the id of a first-party element of kKitElements, whatever its kind.
bool IsFirstParty(std::string_view id);

// True for an extension built into the kernel, such as atmos-leveler: it has no folder and no manifest.
constexpr bool IsBuiltInExtension(std::string_view id) {
    for (const KitElement& element : kKitElements) {
        if (element.builtIn && element.kind == "extension" && element.id == id) return true;
    }
    return false;
}

// Copies the bundled elements from <package>\examples into the data folder `kit`, each in place of the copy already
// there. Returns one line per element left as it was, such as one missing from the package.
std::vector<std::string> InstallBundledElements(const std::filesystem::path& package, const std::filesystem::path& kit);

// The user's Path (entries separated by ';') with `folder` added at the end, so `ampify` works in any terminal. A Path
// that already holds it, whatever the case or a trailing separator, comes back unchanged. Empty entries are dropped.
std::string PathWithFolder(std::string_view path, std::string_view folder);

// The user's Path without `folder`, the other entries kept in their order. Used by the uninstaller.
std::string PathWithoutFolder(std::string_view path, std::string_view folder);

}  // namespace ampify
