#include <string>

#include "../core/kit_layout.hpp"
#include "../core/run_report.hpp"
#include "test.hpp"

namespace {

// The lines a healthy run writes, as the launcher and the kernel write them (see docs/development/testing-on-a-pc.md).
const char* const kLauncherLog =
    "[13:36:35.001] Package found: AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa (folder C:\\Program Files\\WindowsApps\\AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa)\n"
    "[13:36:35.002] version 1.1540.23042.0 tested: kernel injected\n"
    "[13:36:36.400] Apple Music started (process 4120)\n"
    "[13:36:36.410] 2 thread(s) paused\n"
    "[13:36:36.900] AppleMusic.exe (4120): kernel loaded during the pause\n"
    "[13:36:36.950] 2 thread(s) resumed after the injection\n"
    "[13:37:10.200] AMPLibraryAgent.exe (5012): kernel loaded\n"
    "[13:40:00.000] Apple Music closed: the launcher is stopping\n";

const char* const kAppleMusicLog =
    "[13:36:37.100] Kernel loaded in AppleMusic.exe (process 4120)\n"
    "[13:36:37.101] version 1.1540.23042.0 tested\n"
    "[13:36:37.150] Extension host ready\n"
    "[13:36:57.200] Healthy after 20 s: safe mode counter reset to zero\n";

const char* const kLibraryAgentLog =
    "[13:37:10.300] Kernel loaded in AMPLibraryAgent.exe (process 5012)\n"
    "[13:37:10.301] version 1.1540.23042.0 tested\n"
    "[13:37:10.350] Extension host ready\n";

// The log text without one line: the test removes the line it wants to be missing.
std::string WithoutLine(const std::string& text, const std::string& part) {
    size_t start = text.find(part);
    if (start == std::string::npos) return text;
    size_t begin = text.rfind('\n', start);
    begin = begin == std::string::npos ? 0 : begin + 1;
    size_t end = text.find('\n', start);
    return text.substr(0, begin) + (end == std::string::npos ? "" : text.substr(end + 1));
}

ampify::RunEnvironment HealthyEnvironment() {
    ampify::RunEnvironment env;
    env.date = "2026-10-30 13:50";
    env.windows = "Windows 11 Pro (build 26100)";
    env.installOk = true;
    env.installDetail = "C:\\Users\\Test\\Ampify\\";
    env.traceStatus = "trace.etl, 2.1 MB";
    env.eventsStatus = "no Apple Music errors";
    return env;
}

}  // namespace

TEST_CASE(KitLayout_InstalledFilesAreTheFourTheInstallerCopies) {
    std::vector<std::string_view> files = ampify::InstalledFiles();
    CHECK(files.size() == 4);
    CHECK(files[0] == "ampify_launcher.exe");
    CHECK(files[1] == "ampify_kernel.dll");
    CHECK(files[2] == "ampify_setup.exe");
    CHECK(files[3] == "ampify.exe");  // beside the kernel: the Marketplace page runs it (Ampify.kit.run)
}

TEST_CASE(KitLayout_OnlyTheDataFolderCanBeRemovedWholesale) {
    CHECK(ampify::IsKitDataFolder("C:\\Users\\Sam\\Ampify"));
    CHECK(ampify::IsKitDataFolder("C:\\Users\\Sam\\Ampify\\"));
    CHECK(ampify::IsKitDataFolder("C:/Users/User/ampify"));

    // The parent folder, a subfolder, or a folder with a similar name must never pass.
    CHECK(!ampify::IsKitDataFolder("C:\\Users\\Sam"));
    CHECK(!ampify::IsKitDataFolder("C:\\Users\\Sam\\Ampify\\logs"));
    CHECK(!ampify::IsKitDataFolder("C:\\Users\\Sam\\AmpifyOld"));
    CHECK(!ampify::IsKitDataFolder("C:\\"));
    CHECK(!ampify::IsKitDataFolder(""));
}

TEST_CASE(RunReport_ReadsEveryFactOfAHealthyRun) {
    ampify::RunFacts facts = ampify::ReadRunFacts(kLauncherLog, kAppleMusicLog, kLibraryAgentLog);

    CHECK(facts.packageName == "AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa");
    CHECK(facts.launched);
    CHECK(facts.versionTested);
    CHECK(facts.suspendedLaunch);
    CHECK(facts.resumedThreads.has_value() && *facts.resumedThreads == 2);
    CHECK(facts.kernelInAppleMusic);
    CHECK(facts.kernelInLibraryAgent);
    CHECK(facts.healthy);
    CHECK(facts.hungWindowWarnings == 0);
    CHECK(!facts.safeModeActive);
    CHECK(facts.closed);
}

TEST_CASE(RunReport_HealthyRunPasses) {
    ampify::RunFacts facts = ampify::ReadRunFacts(kLauncherLog, kAppleMusicLog, kLibraryAgentLog);
    CHECK(ampify::JudgeRun(facts, HealthyEnvironment()) == ampify::RunVerdict::Passed);
}

TEST_CASE(RunReport_NoAppleMusicMeansNotRun) {
    // The launcher could not find the package: nothing was tested, and the report must say so.
    ampify::RunFacts facts = ampify::ReadRunFacts(
        "[13:36:35.001] Apple Music for Windows not found\n", "", "");
    CHECK(facts.packageName.empty());
    CHECK(ampify::JudgeRun(facts, HealthyEnvironment()) == ampify::RunVerdict::NotRun);

    // Same when the kit itself failed to install.
    ampify::RunEnvironment env = HealthyEnvironment();
    env.installOk = false;
    ampify::RunFacts launched = ampify::ReadRunFacts(kLauncherLog, kAppleMusicLog, kLibraryAgentLog);
    CHECK(ampify::JudgeRun(launched, env) == ampify::RunVerdict::NotRun);
}

TEST_CASE(RunReport_ZeroResumedThreadsNeedsReview) {
    // The M3 check: "0 thread(s) resumed" means nothing was suspended, so the start was not really suspended.
    std::string launcher = kLauncherLog;
    size_t cut = launcher.find("2 thread(s) resumed");
    launcher.replace(cut, std::string("2 thread(s) resumed").size(), "0 thread(s) resumed");

    ampify::RunFacts facts = ampify::ReadRunFacts(launcher, kAppleMusicLog, kLibraryAgentLog);
    CHECK(facts.resumedThreads.has_value() && *facts.resumedThreads == 0);
    CHECK(ampify::JudgeRun(facts, HealthyEnvironment()) == ampify::RunVerdict::NeedsReview);
}

TEST_CASE(RunReport_NotSuspendedIsAllowedButReported) {
    // Windows may refuse the suspended start. The kit still injects after the start, so this is a note, not a failure.
    std::string launcher = kLauncherLog;
    launcher = WithoutLine(launcher, "2 thread(s) paused");
    launcher = WithoutLine(launcher, "2 thread(s) resumed after the injection");

    ampify::RunFacts facts = ampify::ReadRunFacts(launcher, kAppleMusicLog, kLibraryAgentLog);
    CHECK(!facts.suspendedLaunch);
    CHECK(ampify::JudgeRun(facts, HealthyEnvironment()) == ampify::RunVerdict::Passed);

    std::string text = ampify::BuildReport(facts, HealthyEnvironment());
    CHECK(text.find("no, the kernel is injected after the start") != std::string::npos);
}

TEST_CASE(RunReport_UntestedVersionKeepsTheKernelOffAndSaysSo) {
    std::string launcher =
        "[13:36:35.001] Package found: AppleInc.AppleMusicWin_1.1600.1.0_x64__nzyj5cx40ttqa (folder C:\\x)\n"
        "[13:36:35.002] version 1.1600.1.0 not tested: kernel inactive\n"
        "[13:36:36.400] Apple Music started (process 4120)\n";
    ampify::RunFacts facts = ampify::ReadRunFacts(launcher, "", "");

    CHECK(!facts.versionTested);
    CHECK(!facts.kernelInAppleMusic);
    CHECK(ampify::JudgeRun(facts, HealthyEnvironment()) == ampify::RunVerdict::NeedsReview);
    CHECK(ampify::BuildReport(facts, HealthyEnvironment()).find("no, the kernel stays inactive") != std::string::npos);
}

TEST_CASE(RunReport_HungWindowAndSafeModeNeedReview) {
    std::string appleMusic =
        std::string(kAppleMusicLog) + "[13:38:00.000] Window not responding after 20 s: retrying every 5 s\n";
    ampify::RunFacts hung = ampify::ReadRunFacts(kLauncherLog, appleMusic, kLibraryAgentLog);
    CHECK(hung.hungWindowWarnings == 1);
    CHECK(ampify::JudgeRun(hung, HealthyEnvironment()) == ampify::RunVerdict::NeedsReview);

    ampify::RunFacts safe = ampify::ReadRunFacts(
        std::string(kLauncherLog) + "[13:36:35.003] safe mode active: Apple Music starts without the kernel\n", "", "");
    CHECK(safe.safeModeActive);
    CHECK(ampify::JudgeRun(safe, HealthyEnvironment()) == ampify::RunVerdict::NeedsReview);
}

TEST_CASE(RunReport_ForeignPackageIsNotAppleMusic) {
    ampify::RunFacts facts = ampify::ReadRunFacts(
        "[13:36:35.001] Package found: Other.App_1.0.0.0_x64__nzyj5cx40ttqa (folder C:\\x)\n", "", "");
    CHECK(facts.packageName.empty());
}

TEST_CASE(RunReport_AgentIsSeenInTheLauncherLogOrItsOwnLog) {
    std::string launcherWithoutAgent = WithoutLine(kLauncherLog, "AMPLibraryAgent.exe (5012): kernel loaded");

    CHECK(!ampify::ReadRunFacts(launcherWithoutAgent, kAppleMusicLog, "").kernelInLibraryAgent);
    CHECK(ampify::ReadRunFacts(launcherWithoutAgent, kAppleMusicLog, kLibraryAgentLog).kernelInLibraryAgent);
    CHECK(ampify::ReadRunFacts(kLauncherLog, kAppleMusicLog, "").kernelInLibraryAgent);
}

TEST_CASE(RunReport_TextNamesTheVerdictAndTheVersion) {
    ampify::RunFacts facts = ampify::ReadRunFacts(kLauncherLog, kAppleMusicLog, kLibraryAgentLog);
    std::string text = ampify::BuildReport(facts, HealthyEnvironment());

    CHECK(text.find("RESULT: PASSED") != std::string::npos);
    CHECK(text.find("Apple Music: 1.1540.23042.0") != std::string::npos);
    CHECK(text.find("[OK]  ") != std::string::npos);
    CHECK(text.find("2 thread(s) resumed") != std::string::npos);
    CHECK(text.find("replaced by \"user\"") != std::string::npos);
}

TEST_CASE(RunReport_HidesTheUserNameInPaths) {
    std::string text =
        "[13:36:35.001] Package found: X (folder C:\\Users\\Sam\\AppData\\Local)\n"
        "folder C:/users/sam/Ampify, and C:\\Users\\Sam\n"
        "Samantha stays, and C:\\Users\\Samantha\\x keeps its name\n";
    std::string hidden = ampify::HideUserName(text, "Sam");

    CHECK(hidden.find("Sam\\") == std::string::npos);
    CHECK(hidden.find("sam/") == std::string::npos);
    CHECK(hidden.find("C:\\Users\\user\\AppData") != std::string::npos);
    CHECK(hidden.find("C:/users/user/Ampify") != std::string::npos);
    CHECK(hidden.find("Samantha") != std::string::npos);  // a longer name is another user
    CHECK(hidden.find("C:\\Users\\user\n") != std::string::npos);
}

TEST_CASE(RunReport_NotRunTextSaysWhy) {
    ampify::RunFacts facts = ampify::ReadRunFacts("[13:36:35.001] Apple Music for Windows not found\n", "", "");
    std::string text = ampify::BuildReport(facts, HealthyEnvironment());

    CHECK(text.find("RESULT: NOT RUN") != std::string::npos);
    CHECK(text.find("Apple Music found: not found") != std::string::npos);
    CHECK(text.find("Suspended launch: not applicable") != std::string::npos);
}
