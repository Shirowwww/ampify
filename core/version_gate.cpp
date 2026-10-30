#include "version_gate.hpp"

namespace amk {

bool IsKnownAppVersion(std::string_view version, std::span<const std::string_view> known) {
#ifdef AMK_BENCH_ANY_VERSION
    // Bench build only (CMake -DAMK_BENCH=ON): any named build is accepted. An empty version is still refused.
    if (!version.empty()) return true;
#endif
    for (std::string_view candidate : known) {
        if (version == candidate) return true;
    }
    return false;
}

}  // namespace amk
