#pragma once

#include <span>
#include <string>
#include <string_view>

#include "apple_music.hpp"
#include "build_signature.hpp"
#include "version_gate.hpp"

namespace ampify {

// Why Ampify stays off when it does, for the person to be told (src/core/off_notice.hpp). None when the reason is
// not one they can act on or wait out: a missing version, or the verdict of a process that is not Apple Music's.
enum class OffCause { None, SafeMode, UntestedVersion, WrongExports };

// A yes or no, with the reason in English for the log.
struct PolicyVerdict {
    bool allowed = false;
    std::string reason;
    OffCause cause = OffCause::None;
};

// Launcher side, once per launch: inject the kernel into Apple Music? Safe mode, an unknown build, and a build
// whose player DLL does not export what the kit needs all say no, and Apple Music then starts untouched.
PolicyVerdict DecideInjection(std::string_view appVersion, bool safeMode, const BuildSignature& signature,
                              std::span<const std::string_view> known = kKnownAppVersions);

// Kernel side, inside each process: run only in a process of the package, on a build that was tested, whose
// player DLL matches the signature.
PolicyVerdict DecideKernel(ProcessRole role, std::string_view appVersion, const BuildSignature& signature,
                           std::span<const std::string_view> known = kKnownAppVersions);

}  // namespace ampify
