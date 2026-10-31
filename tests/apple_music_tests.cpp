#include <string>

#include "../core/apple_music.hpp"
#include "../core/drm_guard.hpp"
#include "test.hpp"

TEST_CASE(AppleMusic_VersionComesFromThePackageFullName) {
    CHECK(ampify::AppleMusicVersion("AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa") == "1.1540.23042.0");

    // Another publisher, or another package, is not Apple Music.
    CHECK(ampify::AppleMusicVersion("AppleInc.AppleMusicWin_1.1540.23042.0_x64__otherpublish1").empty());
    CHECK(ampify::AppleMusicVersion("Other.App_1.0.0.0_x64__nzyj5cx40ttqa").empty());

    // Malformed names give nothing.
    CHECK(ampify::AppleMusicVersion("AppleInc.AppleMusicWin_1.1540.23042.0_x64").empty());
    CHECK(ampify::AppleMusicVersion("").empty());
}

TEST_CASE(AppleMusic_RecognisesItsTwoProcesses) {
    CHECK(ampify::RoleOfImageName("C:\\Program Files\\WindowsApps\\AppleInc.AppleMusicWin_1.1540.23042.0_x64__"
                               "nzyj5cx40ttqa\\AppleMusic.exe") == ampify::ProcessRole::AppleMusic);
    CHECK(ampify::RoleOfImageName("AMPLibraryAgent.exe") == ampify::ProcessRole::LibraryAgent);
    CHECK(ampify::RoleOfImageName("amplibraryagent.EXE") == ampify::ProcessRole::LibraryAgent);

    CHECK(ampify::RoleOfImageName("explorer.exe") == ampify::ProcessRole::Unknown);
    CHECK(ampify::RoleOfImageName("C:\\Tools\\AppleMusic.exe.bak") == ampify::ProcessRole::Unknown);
}

// Self-updates change the package version: the launcher must see the newer one, numerically, not as text.
TEST_CASE(AppleMusic_VersionsCompareAsNumbers) {
    CHECK(ampify::CompareVersions("1.1792.39682.0", "1.1540.23042.0") > 0);
    CHECK(ampify::CompareVersions("1.1540.23042.0", "1.1792.39682.0") < 0);
    CHECK(ampify::CompareVersions("1.9.0.0", "1.10.0.0") < 0);  // not "1.9" > "1.10" as text
    CHECK(ampify::CompareVersions("1.1540.23042.0", "1.1540.23042.0") == 0);
    CHECK(ampify::CompareVersions("1.2", "1.2.0.0") == 0);      // a missing part counts as 0
}

TEST_CASE(DrmGuard_RefusesFairPlayModules) {
    CHECK(ampify::IsProtectedModule("CoreFP.dll"));
    CHECK(ampify::IsProtectedModule("corefp"));
    CHECK(ampify::IsProtectedModule("C:\\Program Files\\WindowsApps\\pkg\\CoreADI64.dll"));
    CHECK(ampify::IsProtectedModule("FairPlayKeyService.dll"));

    CHECK(!ampify::IsProtectedModule("AVFoundationCF.dll"));
    CHECK(!ampify::IsProtectedModule("CoreMedia.dll"));
    CHECK(!ampify::IsProtectedModule("AMPLibraryAgent.exe"));
}
