#include "injection_policy.hpp"

namespace ampify {

PolicyVerdict DecideInjection(std::string_view appVersion, bool safeMode, const BuildSignature& signature,
                              std::span<const std::string_view> known) {
    if (safeMode) return {false, "safe mode active: Apple Music starts without the kernel", OffCause::SafeMode};
    if (appVersion.empty()) return {false, "Apple Music version not found: Apple Music starts without the kernel"};
    if (!IsKnownAppVersion(appVersion, known)) {
        return {false, "version " + std::string(appVersion) + " not tested: Apple Music starts without the kernel",
                OffCause::UntestedVersion};
    }
    if (!SignatureMatches(signature)) {
        return {false, DescribeSignature(signature) + ": Apple Music starts without the kernel",
                OffCause::WrongExports};
    }
    return {true, "version " + std::string(appVersion) + " tested: kernel injected"};
}

PolicyVerdict DecideKernel(ProcessRole role, std::string_view appVersion, const BuildSignature& signature,
                           std::span<const std::string_view> known) {
    if (role == ProcessRole::Unknown) return {false, "process not concerned: kernel inactive"};
    if (appVersion.empty()) return {false, "Apple Music version not found: kernel inactive"};
    if (!IsKnownAppVersion(appVersion, known)) {
        return {false, "version " + std::string(appVersion) + " not tested: kernel inactive"};
    }
    if (!SignatureMatches(signature)) return {false, DescribeSignature(signature) + ": kernel inactive"};
    return {true, "version " + std::string(appVersion) + " tested"};
}

}  // namespace ampify
