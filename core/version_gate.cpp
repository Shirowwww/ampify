#include "version_gate.hpp"

#include <iterator>

namespace ampify {

bool IsKnownAppVersion(std::string_view version, std::span<const std::string_view> known) {
#ifdef AMPIFY_BENCH_ANY_VERSION
    // Bench build only (CMake -DAMPIFY_BENCH=ON): any named build is accepted. An empty version is still refused.
    if (!version.empty()) return true;
#endif
    for (std::string_view candidate : known) {
        if (version == candidate) return true;
    }
    return false;
}

std::vector<std::string_view> KnownAppVersionsWith(const std::vector<std::string>& extra) {
    std::vector<std::string_view> known(std::begin(kKnownAppVersions), std::end(kKnownAppVersions));
    for (const std::string& version : extra) {
        if (version.empty()) continue;
        bool present = false;
        for (std::string_view candidate : known) present = present || candidate == version;
        if (!present) known.push_back(version);
    }
    return known;
}

}  // namespace ampify
