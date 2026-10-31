#include "kit_layout.hpp"

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

}  // namespace ampify
