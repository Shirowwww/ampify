#include <string>

#include "../core/apple_music.hpp"
#include "../core/drm_guard.hpp"
#include "test.hpp"

TEST_CASE(AppleMusic_VersionComesFromThePackageFullName) {
    CHECK(amk::AppleMusicVersion("AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa") == "1.1540.23042.0");

    // Another publisher, or another package, is not Apple Music.
    CHECK(amk::AppleMusicVersion("AppleInc.AppleMusicWin_1.1540.23042.0_x64__otherpublish1").empty());
    CHECK(amk::AppleMusicVersion("Other.App_1.0.0.0_x64__nzyj5cx40ttqa").empty());

    // Malformed names give nothing.
    CHECK(amk::AppleMusicVersion("AppleInc.AppleMusicWin_1.1540.23042.0_x64").empty());
    CHECK(amk::AppleMusicVersion("").empty());
}

TEST_CASE(AppleMusic_RecognisesItsTwoProcesses) {
    CHECK(amk::RoleOfImageName("C:\\Program Files\\WindowsApps\\AppleInc.AppleMusicWin_1.1540.23042.0_x64__"
                               "nzyj5cx40ttqa\\AppleMusic.exe") == amk::ProcessRole::AppleMusic);
    CHECK(amk::RoleOfImageName("AMPLibraryAgent.exe") == amk::ProcessRole::LibraryAgent);
    CHECK(amk::RoleOfImageName("amplibraryagent.EXE") == amk::ProcessRole::LibraryAgent);

    CHECK(amk::RoleOfImageName("explorer.exe") == amk::ProcessRole::Unknown);
    CHECK(amk::RoleOfImageName("C:\\Tools\\AppleMusic.exe.bak") == amk::ProcessRole::Unknown);
}

// Self-updates change the package version: the launcher must see the newer one, numerically, not as text.
TEST_CASE(AppleMusic_VersionsCompareAsNumbers) {
    CHECK(amk::CompareVersions("1.1792.39682.0", "1.1540.23042.0") > 0);
    CHECK(amk::CompareVersions("1.1540.23042.0", "1.1792.39682.0") < 0);
    CHECK(amk::CompareVersions("1.9.0.0", "1.10.0.0") < 0);  // not "1.9" > "1.10" as text
    CHECK(amk::CompareVersions("1.1540.23042.0", "1.1540.23042.0") == 0);
    CHECK(amk::CompareVersions("1.2", "1.2.0.0") == 0);      // a missing part counts as 0
}

TEST_CASE(DrmGuard_RefusesFairPlayModules) {
    CHECK(amk::IsProtectedModule("CoreFP.dll"));
    CHECK(amk::IsProtectedModule("corefp"));
    CHECK(amk::IsProtectedModule("C:\\Program Files\\WindowsApps\\pkg\\CoreADI64.dll"));
    CHECK(amk::IsProtectedModule("FairPlayKeyService.dll"));

    CHECK(!amk::IsProtectedModule("AVFoundationCF.dll"));
    CHECK(!amk::IsProtectedModule("CoreMedia.dll"));
    CHECK(!amk::IsProtectedModule("AMPLibraryAgent.exe"));
}
