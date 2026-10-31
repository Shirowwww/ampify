#include <filesystem>
#include <string>
#include <string_view>

#include "../core/apple_music.hpp"
#include "../core/build_signature.hpp"
#include "../core/injection_policy.hpp"
#include "../core/safe_mode.hpp"
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
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "1.1540.23042.1", kMatching).allowed);
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::LibraryAgent, "", kMatching).allowed);
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
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "ampify-tests" / "safe-mode-active";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

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
