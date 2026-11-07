#include "version_gate.hpp"

#include <iterator>

namespace ampify {

bool IsKnownAppVersion(std::string_view version, std::span<const std::string_view> known) {
#ifdef AMPIFY_BENCH_ANY_VERSION
    // Bench build only (CMake -DAMPIFY_BENCH=ON): any named build is accepted. An empty version is still refused.
    if (!version.empty()) return true;
#endif
    std::string_view family = AppVersionFamily(version);
    if (family.empty()) return false;
    for (std::string_view candidate : known) {
        if (AppVersionFamily(candidate) == family) return true;
    }
    return false;
}

std::string_view AppVersionFamily(std::string_view version) {
    int dots = 0;
    size_t familyEnd = 0;
    bool digitBefore = false;
    for (size_t i = 0; i < version.size(); ++i) {
        char c = version[i];
        if (c == '.') {
            if (!digitBefore) return {};
            if (++dots == 2) familyEnd = i;
            digitBefore = false;
        } else if (c >= '0' && c <= '9') {
            digitBefore = true;
        } else {
            return {};
        }
    }
    if (dots != 3 || !digitBefore) return {};
    return version.substr(0, familyEnd);
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
