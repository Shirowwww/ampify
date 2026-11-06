// Apple Music reliability helpers (src/core/reliability.*): slow network waits written to the freeze log, the memory
// of a process followed in steps, and the programs known to make Apple Music crash at start. All portable: the kernel
// times the calls and reads the processes, these decide what is written.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/core/reliability.hpp"
#include "test.hpp"

using ampify::FindKnownConflicts;
using ampify::IgnoredAppsListsAppleMusic;
using ampify::KnownConflict;
using ampify::MemoryWatch;
using ampify::SlowCallLog;

TEST_CASE(Reliability_AQuickCallIsNotLogged) {
    SlowCallLog log;
    CHECK(log.Record("resolving api.music.apple.com", ampify::kSlowCallMs - 1, false).empty());
}

TEST_CASE(Reliability_ASlowCallSaysWhatWaitedHowLongAndOnWhichThread) {
    SlowCallLog log;
    CHECK(log.Record("proxy detection", 1250, false) == "Network: proxy detection took 1250 ms");
    CHECK(log.Record("resolving x.apple.com", 400, true) ==
          "Network: resolving x.apple.com took 400 ms, on the interface thread");
}

TEST_CASE(Reliability_SlowCallLinesStopAfterTheirShareAndSaySo) {
    SlowCallLog log;
    std::string last;
    for (int i = 0; i < ampify::kSlowCallLines; ++i) last = log.Record("proxy detection", 500, false);
    CHECK(last.find("further slow network calls are not logged") != std::string::npos);
    CHECK(log.Record("proxy detection", 9000, false).empty());
}

TEST_CASE(Reliability_MemoryIsLoggedFirstThenAtEachNewStep) {
    constexpr uint64_t mb = 1024 * 1024;
    MemoryWatch watch("AMPLibraryAgent.exe");
    CHECK(watch.Check(300 * mb) == "Memory: AMPLibraryAgent.exe uses 300 MB");
    CHECK(watch.Check(400 * mb).empty());
    CHECK(watch.Check(300 * mb).empty());
    CHECK(watch.Check(800 * mb) == "Memory: AMPLibraryAgent.exe grew to 800 MB");
    CHECK(watch.Check(900 * mb).empty());  // below the next step, 1024 MB
    CHECK(watch.Check(1300 * mb) == "Memory: AMPLibraryAgent.exe grew to 1300 MB");
    CHECK(watch.Check(5000 * mb) == "Memory: AMPLibraryAgent.exe grew to 5000 MB");
    CHECK(watch.Check(5100 * mb).empty());
}

TEST_CASE(Reliability_AnUnreadMemoryIsNotLogged) {
    MemoryWatch watch("AppleMusic.exe");
    CHECK(watch.Check(0).empty());
}

TEST_CASE(Reliability_NoKnownProgramMeansNoConflict) {
    CHECK(FindKnownConflicts({"explorer.exe", "AppleMusic.exe", "Discord.exe"}, false).empty());
    CHECK(FindKnownConflicts({}, false).empty());
}

TEST_CASE(Reliability_3DconnexionIsFoundByItsProgramsOnceAndUntilAppleMusicIsIgnored) {
    std::vector<KnownConflict> found = FindKnownConflicts({"3DxService.exe", "3dxsmartui.exe"}, false);
    CHECK(found.size() == 1);
    if (found.size() == 1) {
        CHECK(found[0].id == "3dconnexion");
        CHECK(found[0].advice.find("IgnoredApps.xml") != std::string::npos);
    }
    CHECK(FindKnownConflicts({"3DxService.exe"}, true).empty());  // already told to leave Apple Music alone
}

TEST_CASE(Reliability_OnlyRivaTunersOverlayCountsNotAfterburnerAlone) {
    CHECK(FindKnownConflicts({"MSIAfterburner.exe"}, false).empty());  // draws nothing in Apple Music by itself
    std::vector<KnownConflict> found = FindKnownConflicts({"MSIAfterburner.exe", "RTSS.exe", "3DxService.exe"}, false);
    CHECK(found.size() == 2);
    if (found.size() == 2) {
        CHECK(found[0].id == "3dconnexion");  // the fixed order, not the order of the processes
        CHECK(found[1].id == "rivatuner");
        CHECK(found[1].advice.find("AppleMusic.exe") != std::string::npos);
    }
}

TEST_CASE(Reliability_IgnoredAppsListsAppleMusicInAnyCase) {
    CHECK(IgnoredAppsListsAppleMusic("<AppInfo><ExecutableName>applemusic.EXE</ExecutableName></AppInfo>"));
    CHECK(IgnoredAppsListsAppleMusic("<ExecutableName> AppleMusic.exe </ExecutableName>"));
    CHECK(!IgnoredAppsListsAppleMusic("<ExecutableName>iTunes.exe</ExecutableName>"));
    CHECK(!IgnoredAppsListsAppleMusic("<!-- AppleMusic.exe --><ExecutableName>x.exe</ExecutableName>"));
    CHECK(!IgnoredAppsListsAppleMusic(""));
}

namespace {

namespace fs = std::filesystem;

fs::path FreshFolder(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("reliability-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void Touch(const fs::path& file, const std::string& text = "x") {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

}  // namespace

TEST_CASE(Reliability_3DconnexionsIgnoredAppsIsReadUnderProgramFiles) {
    fs::path programFiles = FreshFolder("program-files");
    CHECK(!ampify::ThreeDxIgnoresAppleMusic(programFiles));  // no driver
    fs::path file = programFiles / "3Dconnexion" / "3DxWare" / "3DxWinCore" / "Cfg" / "IgnoredApps.xml";
    Touch(file, "<AppInfo><ExecutableName>Photoshop.exe</ExecutableName></AppInfo>");
    CHECK(!ampify::ThreeDxIgnoresAppleMusic(programFiles));
    Touch(file, "<AppInfo><ExecutableName>applemusic.EXE</ExecutableName></AppInfo>");
    CHECK(ampify::ThreeDxIgnoresAppleMusic(programFiles));
}

TEST_CASE(Reliability_OnlyRecentDumpsOfAppleMusicsProcessesAreCounted) {
    fs::path dumps = FreshFolder("dumps");
    Touch(dumps / "AppleMusic.exe.4120.dmp");
    Touch(dumps / "AMPLibraryAgent.exe.77.dmp");
    Touch(dumps / "Discord.exe.12.dmp");
    Touch(dumps / "AppleMusic.exe.notes.txt");
    Touch(dumps / "AppleMusic.exe.9.dmp");
    fs::last_write_time(dumps / "AppleMusic.exe.9.dmp", fs::file_time_type::clock::now() - std::chrono::hours(24 * 30));
    std::vector<std::string> recent = ampify::RecentAppleCrashDumps(dumps, 7);
    CHECK(recent.size() == 2);
    CHECK(ampify::RecentAppleCrashDumps(dumps / "missing", 7).empty());
}
