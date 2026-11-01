#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

// Builds of Apple Music the kernel has been tested on, as shown by the package version
// (`AppleInc.AppleMusicWin_<version>_x64__...`). Hooks are only installed on these builds.
// On any other build the kernel stays idle: the app runs untouched, nothing is guessed by address.
inline constexpr std::string_view kKnownAppVersions[] = {
    "1.1540.23042.0",
    "1.1792.39682.0",  // Windows bench, 30/10/2026: injection in both processes, export signature recognized
};

bool IsKnownAppVersion(std::string_view version,
                       std::span<const std::string_view> known = kKnownAppVersions);

// The built-in builds, then the builds of `extra` (read from <kit>\versions.txt, see version_list.hpp) that are not
// built in. The views point into kKnownAppVersions and into `extra`, which must outlive the result. Pass the result
// as `known` to IsKnownAppVersion, DecideInjection or DecideKernel: the build signature is still checked there.
std::vector<std::string_view> KnownAppVersionsWith(const std::vector<std::string>& extra);

}  // namespace ampify
