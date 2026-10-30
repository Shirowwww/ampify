#pragma once

#include <span>
#include <string_view>

namespace amk {

// Builds of Apple Music the kernel has been tested on, as shown by the package version
// (`AppleInc.AppleMusicWin_<version>_x64__...`). Hooks are only installed on these builds.
// On any other build the kernel stays idle: the app runs untouched, nothing is guessed by address.
inline constexpr std::string_view kKnownAppVersions[] = {
    "1.1540.23042.0",
};

bool IsKnownAppVersion(std::string_view version,
                       std::span<const std::string_view> known = kKnownAppVersions);

}  // namespace amk
