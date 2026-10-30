#include <filesystem>
#include <string>
#include <string_view>

#include "../core/apple_music.hpp"
#include "../core/injection_policy.hpp"
#include "../core/safe_mode.hpp"
#include "test.hpp"

TEST_CASE(Injection_OnlyOnTestedBuildsOutsideSafeMode) {
    amk::PolicyVerdict tested = amk::DecideInjection("1.1540.23042.0", false);
    CHECK(tested.allowed);
    CHECK(!tested.reason.empty());

    CHECK(!amk::DecideInjection("1.1540.23042.0", true).allowed);  // safe mode wins over a known build
    CHECK(!amk::DecideInjection("1.1541.0.0", false).allowed);      // untested build: app starts untouched
    CHECK(!amk::DecideInjection("", false).allowed);                // version not found
}

TEST_CASE(Injection_ReasonSaysWhyTheAppStartsUntouched) {
    CHECK(amk::DecideInjection("1.1541.0.0", false).reason.find("non testée") != std::string::npos);
    CHECK(amk::DecideInjection("1.1540.23042.0", true).reason.find("mode sans échec") != std::string::npos);
}

TEST_CASE(Kernel_RunsOnlyInAppProcessesOnTestedBuilds) {
    CHECK(amk::DecideKernel(amk::ProcessRole::AppleMusic, "1.1540.23042.0").allowed);
    CHECK(amk::DecideKernel(amk::ProcessRole::LibraryAgent, "1.1540.23042.0").allowed);

    CHECK(!amk::DecideKernel(amk::ProcessRole::Unknown, "1.1540.23042.0").allowed);
    CHECK(!amk::DecideKernel(amk::ProcessRole::AppleMusic, "1.1540.23042.1").allowed);
    CHECK(!amk::DecideKernel(amk::ProcessRole::LibraryAgent, "").allowed);
}

TEST_CASE(Kernel_UsesTheListItIsGiven) {
    const std::string_view custom[] = {"2.0.0.0"};
    CHECK(amk::DecideKernel(amk::ProcessRole::AppleMusic, "2.0.0.0", custom).allowed);
    CHECK(!amk::DecideKernel(amk::ProcessRole::AppleMusic, "1.1540.23042.0", custom).allowed);
}

TEST_CASE(SafeMode_IsActiveOnlyAtTheThreshold) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "amk-tests" / "safe-mode-active";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    amk::SafeMode safeMode((dir / "state.txt").string(), 2);
    CHECK(!safeMode.IsActive());

    safeMode.BeginLaunch();
    CHECK(!safeMode.IsActive());

    safeMode.BeginLaunch();
    CHECK(safeMode.IsActive());
    CHECK(safeMode.UnhealthyLaunches() == 2);  // checking does not count as a launch

    safeMode.MarkHealthy();
    CHECK(!safeMode.IsActive());
}
