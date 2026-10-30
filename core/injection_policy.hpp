#pragma once

#include <span>
#include <string>
#include <string_view>

#include "apple_music.hpp"
#include "version_gate.hpp"

namespace amk {

// A yes or no, with the reason in French for the journal.
struct PolicyVerdict {
    bool allowed = false;
    std::string reason;
};

// Launcher side, once per launch: inject the kernel into Apple Music? Safe mode and unknown builds say no,
// and Apple Music then starts untouched.
PolicyVerdict DecideInjection(std::string_view appVersion, bool safeMode,
                              std::span<const std::string_view> known = kKnownAppVersions);

// Kernel side, inside each process: run only in a process of the package and on a build that was tested.
PolicyVerdict DecideKernel(ProcessRole role, std::string_view appVersion,
                           std::span<const std::string_view> known = kKnownAppVersions);

}  // namespace amk
