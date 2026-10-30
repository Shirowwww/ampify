#include "version_gate.hpp"

namespace amk {

bool IsKnownAppVersion(std::string_view version, std::span<const std::string_view> known) {
    for (std::string_view candidate : known) {
        if (version == candidate) return true;
    }
    return false;
}

}  // namespace amk
