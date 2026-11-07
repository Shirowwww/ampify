#include <filesystem>
#include <string>
#include <string_view>

#include "../src/core/apple_music.hpp"
#include "../src/core/build_signature.hpp"
#include "../src/core/injection_policy.hpp"
#include "../src/core/safe_mode.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

// A player DLL that exports every name the kit needs.
const ampify::BuildSignature kMatching{true, {}};
// A player DLL that lost one of them.
const ampify::BuildSignature kMissingSetVolume{true, {"AVCFPlayerSetVolume"}};
// No readable player DLL at all.
const ampify::BuildSignature kUnreadable{};

}  // namespace

TEST_CASE(Injection_OnlyOnTestedBuildsOutsideSafeMode) {
    ampify::PolicyVerdict tested = ampify::DecideInjection("1.1540.23042.0", false, kMatching);
    CHECK(tested.allowed);
    CHECK(!tested.reason.empty());

    CHECK(!ampify::DecideInjection("1.1540.23042.0", true, kMatching).allowed);  // safe mode wins over a known build
    CHECK(!ampify::DecideInjection("1.1541.0.0", false, kMatching).allowed);     // untested build: app starts untouched
    CHECK(!ampify::DecideInjection("", false, kMatching).allowed);               // version not found
}

TEST_CASE(Injection_ReasonSaysWhyTheAppStartsUntouched) {
    CHECK(ampify::DecideInjection("1.1541.0.0", false, kMatching).reason.find("not tested") != std::string::npos);
    CHECK(ampify::DecideInjection("1.1540.23042.0", true, kMatching).reason.find("safe mode") != std::string::npos);
}

TEST_CASE(Injection_RefusesAKnownVersionWhoseSignatureDoesNotMatch) {
    // Same version string, but the DLL on disk is not the one the kit was tested against.
    ampify::PolicyVerdict missing = ampify::DecideInjection("1.1540.23042.0", false, kMissingSetVolume);
    CHECK(!missing.allowed);
    CHECK(missing.reason.find("AVCFPlayerSetVolume") != std::string::npos);
    CHECK(missing.reason.find("Apple Music starts without the kernel") != std::string::npos);

    CHECK(!ampify::DecideInjection("1.1540.23042.0", false, kUnreadable).allowed);
}

TEST_CASE(Kernel_RunsOnlyInAppProcessesOnTestedBuilds) {
    CHECK(ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "1.1540.23042.0", kMatching).allowed);
    CHECK(ampify::DecideKernel(ampify::ProcessRole::LibraryAgent, "1.1540.23042.0", kMatching).allowed);

    CHECK(!ampify::DecideKernel(ampify::ProcessRole::Unknown, "1.1540.23042.0", kMatching).allowed);
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "1.1541.23042.1", kMatching).allowed);
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::LibraryAgent, "", kMatching).allowed);
}

TEST_CASE(Injection_TakesUntestedBuildsOfATestedFamilyWithTheSignature) {
    for (std::string_view build : {"1.1792.39681.0", "1.1792.39942.0"}) {
        ampify::PolicyVerdict launch = ampify::DecideInjection(build, false, kMatching);
        CHECK(launch.allowed);
        CHECK(launch.reason.find("family 1.1792 tested") != std::string::npos);
        CHECK(ampify::DecideKernel(ampify::ProcessRole::AppleMusic, build, kMatching).allowed);
        CHECK(ampify::DecideKernel(ampify::ProcessRole::LibraryAgent, build, kMatching).allowed);
    }
    ampify::PolicyVerdict other = ampify::DecideInjection("1.1800.1.0", false, kMatching);
    CHECK(!other.allowed);
    CHECK(other.cause == ampify::OffCause::UntestedVersion);
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "1.1800.1.0", kMatching).allowed);

    ampify::PolicyVerdict wrong = ampify::DecideInjection("1.1792.39942.0", false, kMissingSetVolume);
    CHECK(!wrong.allowed);
    CHECK(wrong.cause == ampify::OffCause::WrongExports);
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "1.1792.39942.0", kUnreadable).allowed);
}

TEST_CASE(Kernel_RefusesWhenTheSignatureDoesNotMatch) {
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "1.1540.23042.0", kMissingSetVolume).allowed);
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::LibraryAgent, "1.1540.23042.0", kUnreadable).allowed);
}

TEST_CASE(Kernel_UsesTheListItIsGiven) {
    const std::string_view custom[] = {"2.0.0.0"};
    CHECK(ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "2.0.0.0", kMatching, custom).allowed);
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "1.1540.23042.0", kMatching, custom).allowed);
}

TEST_CASE(SafeMode_IsActiveOnlyAtTheThreshold) {
    ampify::test::ScratchDir dir("safe-mode-active");

    ampify::SafeMode safeMode((dir / "state.txt").string(), 2);
    CHECK(!safeMode.IsActive());

    safeMode.BeginLaunch();
    CHECK(!safeMode.IsActive());

    safeMode.BeginLaunch();
    CHECK(safeMode.IsActive());
    CHECK(safeMode.UnhealthyLaunches() == 2);  // checking does not count as a launch

    safeMode.MarkHealthy();
    CHECK(!safeMode.IsActive());
}
