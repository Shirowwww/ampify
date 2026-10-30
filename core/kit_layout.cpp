#include "kit_layout.hpp"

#include "text_util.hpp"

namespace amk {

std::vector<std::string_view> InstalledFiles() {
    return {kLauncherFile, kKernelFile, kSetupFile};
}

bool IsKitDataFolder(std::string_view path) {
    // Trailing separators do not count: "C:\Users\User\AppleMusicModKit\" is the same folder.
    while (!path.empty() && (path.back() == '\\' || path.back() == '/')) path.remove_suffix(1);
    return ToLowerAscii(FileName(path)) == ToLowerAscii(kKitFolderName);
}

}  // namespace amk
