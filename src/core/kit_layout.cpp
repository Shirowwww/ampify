#include "kit_layout.hpp"

#include <algorithm>
#include <system_error>

#include "text_util.hpp"

namespace ampify {

std::vector<std::string_view> InstalledFiles() {
    return {kLauncherFile, kKernelFile, kSetupFile, kCliFile};
}

bool IsKitDataFolder(std::string_view path) {
    // Trailing separators do not count: "C:\Users\User\Ampify\" is the same folder.
    while (!path.empty() && (path.back() == '\\' || path.back() == '/')) path.remove_suffix(1);
    return ToLowerAscii(FileName(path)) == ToLowerAscii(kKitFolderName);
}

std::vector<std::string_view> BundledElements() {
    return {"apps/marketplace", "extensions/now-playing"};
}

bool IsBundledElement(std::string_view folder, std::string_view id) {
    std::string wanted = std::string(folder) + "/" + std::string(id);
    std::vector<std::string_view> bundled = BundledElements();
    return std::find(bundled.begin(), bundled.end(), wanted) != bundled.end();
}

std::vector<std::string> InstallBundledElements(const std::filesystem::path& package, const std::filesystem::path& kit) {
    namespace fs = std::filesystem;
    std::vector<std::string> failures;
    for (std::string_view element : BundledElements()) {
        fs::path source = package / "examples" / fs::path(element);
        fs::path destination = kit / fs::path(element);
        std::error_code error;
        if (!fs::is_regular_file(source / "manifest.json", error)) {
            failures.push_back(std::string(element) + ": not in the package");
            continue;
        }
        // The old copy goes first, so a file the new version dropped does not stay behind.
        fs::remove_all(destination, error);
        if (!error) fs::create_directories(destination.parent_path(), error);
        if (!error) fs::copy(source, destination, fs::copy_options::recursive, error);
        if (error) failures.push_back(std::string(element) + ": " + error.message());
    }
    return failures;
}

}  // namespace ampify
