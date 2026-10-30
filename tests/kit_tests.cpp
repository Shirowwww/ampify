#include <string>

#include "../core/kit_layout.hpp"
#include "../core/run_report.hpp"
#include "test.hpp"

namespace {

// The lines a healthy run writes, as the launcher and the kernel write them (see docs/tests/M3-injecteur.md).
const char* const kLauncherLog =
    "[13:36:35.001] Paquet trouvé : AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa (dossier C:\\Program Files\\WindowsApps\\AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa)\n"
    "[13:36:35.002] version 1.1540.23042.0 testée : noyau injecté\n"
    "[13:36:36.400] Apple Music démarré (processus 4120)\n"
    "[13:36:36.410] 2 thread(s) mis en pause\n"
    "[13:36:36.900] AppleMusic.exe (4120) : noyau chargé pendant la pause\n"
    "[13:36:36.950] 2 thread(s) repris après l'injection\n"
    "[13:37:10.200] AMPLibraryAgent.exe (5012) : noyau chargé\n"
    "[13:40:00.000] Apple Music fermé : le lanceur s'arrête\n";

const char* const kAppleMusicLog =
    "[13:36:37.100] Noyau chargé dans AppleMusic.exe (processus 4120)\n"
    "[13:36:37.101] version 1.1540.23042.0 testée\n"
    "[13:36:37.150] Hôte d'extensions prêt\n"
    "[13:36:57.200] Application saine après 20 s : compteur du mode sans échec remis à zéro\n";

const char* const kLibraryAgentLog =
    "[13:37:10.300] Noyau chargé dans AMPLibraryAgent.exe (processus 5012)\n"
    "[13:37:10.301] version 1.1540.23042.0 testée\n"
    "[13:37:10.350] Hôte d'extensions prêt\n";

// The log text without one line: the test removes the line it wants to be missing.
std::string WithoutLine(const std::string& text, const std::string& part) {
    size_t start = text.find(part);
    if (start == std::string::npos) return text;
    size_t begin = text.rfind('\n', start);
    begin = begin == std::string::npos ? 0 : begin + 1;
    size_t end = text.find('\n', start);
    return text.substr(0, begin) + (end == std::string::npos ? "" : text.substr(end + 1));
}

amk::RunEnvironment HealthyEnvironment() {
    amk::RunEnvironment env;
    env.date = "2026-10-30 13:50";
    env.windows = "Windows 11 Pro (build 26100)";
    env.installOk = true;
    env.installDetail = "C:\\Users\\Test\\AppleMusicModKit\\";
    env.traceStatus = "trace.etl, 2,1 Mo";
    env.eventsStatus = "aucune erreur d'Apple Music";
    return env;
}

}  // namespace

TEST_CASE(KitLayout_InstalledFilesAreTheThreeTheInstallerCopies) {
    std::vector<std::string_view> files = amk::InstalledFiles();
    CHECK(files.size() == 3);
    CHECK(files[0] == "amk_launcher.exe");
    CHECK(files[1] == "amk_kernel.dll");
    CHECK(files[2] == "amk_setup.exe");
}

TEST_CASE(KitLayout_OnlyTheDataFolderCanBeRemovedWholesale) {
    CHECK(amk::IsKitDataFolder("C:\\Users\\User\\AppleMusicModKit"));
    CHECK(amk::IsKitDataFolder("C:\\Users\\User\\AppleMusicModKit\\"));
    CHECK(amk::IsKitDataFolder("C:/Users/User/appleMusicModKit"));

    // The parent folder, a subfolder, or a folder with a similar name must never pass.
    CHECK(!amk::IsKitDataFolder("C:\\Users\\User"));
    CHECK(!amk::IsKitDataFolder("C:\\Users\\User\\AppleMusicModKit\\journal"));
    CHECK(!amk::IsKitDataFolder("C:\\Users\\User\\AppleMusicModKitOld"));
    CHECK(!amk::IsKitDataFolder("C:\\"));
    CHECK(!amk::IsKitDataFolder(""));
}

TEST_CASE(RunReport_ReadsEveryFactOfAHealthyRun) {
    amk::RunFacts facts = amk::ReadRunFacts(kLauncherLog, kAppleMusicLog, kLibraryAgentLog);

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
    amk::RunFacts facts = amk::ReadRunFacts(kLauncherLog, kAppleMusicLog, kLibraryAgentLog);
    CHECK(amk::JudgeRun(facts, HealthyEnvironment()) == amk::RunVerdict::Passed);
}

TEST_CASE(RunReport_NoAppleMusicMeansNotRun) {
    // The launcher could not find the package: nothing was tested, and the report must say so.
    amk::RunFacts facts = amk::ReadRunFacts(
        "[13:36:35.001] Apple Music pour Windows introuvable\n", "", "");
    CHECK(facts.packageName.empty());
    CHECK(amk::JudgeRun(facts, HealthyEnvironment()) == amk::RunVerdict::NotRun);

    // Same when the kit itself failed to install.
    amk::RunEnvironment env = HealthyEnvironment();
    env.installOk = false;
    amk::RunFacts launched = amk::ReadRunFacts(kLauncherLog, kAppleMusicLog, kLibraryAgentLog);
    CHECK(amk::JudgeRun(launched, env) == amk::RunVerdict::NotRun);
}

TEST_CASE(RunReport_ZeroResumedThreadsNeedsReview) {
    // The M3 check: "0 thread(s) repris" means nothing was suspended, so the start was not really suspended.
    std::string launcher = kLauncherLog;
    size_t cut = launcher.find("2 thread(s) repris");
    launcher.replace(cut, std::string("2 thread(s) repris").size(), "0 thread(s) repris");

    amk::RunFacts facts = amk::ReadRunFacts(launcher, kAppleMusicLog, kLibraryAgentLog);
    CHECK(facts.resumedThreads.has_value() && *facts.resumedThreads == 0);
    CHECK(amk::JudgeRun(facts, HealthyEnvironment()) == amk::RunVerdict::NeedsReview);
}

TEST_CASE(RunReport_NotSuspendedIsAllowedButReported) {
    // Windows may refuse the suspended start. The kit still injects after the start, so this is a note, not a failure.
    std::string launcher = kLauncherLog;
    launcher = WithoutLine(launcher, "2 thread(s) mis en pause");
    launcher = WithoutLine(launcher, "2 thread(s) repris après l'injection");

    amk::RunFacts facts = amk::ReadRunFacts(launcher, kAppleMusicLog, kLibraryAgentLog);
    CHECK(!facts.suspendedLaunch);
    CHECK(amk::JudgeRun(facts, HealthyEnvironment()) == amk::RunVerdict::Passed);

    std::string text = amk::BuildReport(facts, HealthyEnvironment());
    CHECK(text.find("non, le noyau est injecté après le démarrage") != std::string::npos);
}

TEST_CASE(RunReport_UntestedVersionKeepsTheKernelOffAndSaysSo) {
    std::string launcher =
        "[13:36:35.001] Paquet trouvé : AppleInc.AppleMusicWin_1.1600.1.0_x64__nzyj5cx40ttqa (dossier C:\\x)\n"
        "[13:36:35.002] version 1.1600.1.0 non testée : noyau inactif\n"
        "[13:36:36.400] Apple Music démarré (processus 4120)\n";
    amk::RunFacts facts = amk::ReadRunFacts(launcher, "", "");

    CHECK(!facts.versionTested);
    CHECK(!facts.kernelInAppleMusic);
    CHECK(amk::JudgeRun(facts, HealthyEnvironment()) == amk::RunVerdict::NeedsReview);
    CHECK(amk::BuildReport(facts, HealthyEnvironment()).find("non, le noyau reste inactif") != std::string::npos);
}

TEST_CASE(RunReport_HungWindowAndSafeModeNeedReview) {
    std::string appleMusic =
        std::string(kAppleMusicLog) + "[13:38:00.000] Fenêtre bloquée après 20 s : nouvel essai toutes les 5 s\n";
    amk::RunFacts hung = amk::ReadRunFacts(kLauncherLog, appleMusic, kLibraryAgentLog);
    CHECK(hung.hungWindowWarnings == 1);
    CHECK(amk::JudgeRun(hung, HealthyEnvironment()) == amk::RunVerdict::NeedsReview);

    amk::RunFacts safe = amk::ReadRunFacts(
        std::string(kLauncherLog) + "[13:36:35.003] mode sans échec actif : Apple Music démarre sans noyau\n", "", "");
    CHECK(safe.safeModeActive);
    CHECK(amk::JudgeRun(safe, HealthyEnvironment()) == amk::RunVerdict::NeedsReview);
}

TEST_CASE(RunReport_ForeignPackageIsNotAppleMusic) {
    amk::RunFacts facts = amk::ReadRunFacts(
        "[13:36:35.001] Paquet trouvé : Other.App_1.0.0.0_x64__nzyj5cx40ttqa (dossier C:\\x)\n", "", "");
    CHECK(facts.packageName.empty());
}

TEST_CASE(RunReport_AgentIsSeenInTheLauncherLogOrItsOwnLog) {
    std::string launcherWithoutAgent = WithoutLine(kLauncherLog, "AMPLibraryAgent.exe (5012) : noyau chargé");

    CHECK(!amk::ReadRunFacts(launcherWithoutAgent, kAppleMusicLog, "").kernelInLibraryAgent);
    CHECK(amk::ReadRunFacts(launcherWithoutAgent, kAppleMusicLog, kLibraryAgentLog).kernelInLibraryAgent);
    CHECK(amk::ReadRunFacts(kLauncherLog, kAppleMusicLog, "").kernelInLibraryAgent);
}

TEST_CASE(RunReport_TextNamesTheVerdictAndTheVersion) {
    amk::RunFacts facts = amk::ReadRunFacts(kLauncherLog, kAppleMusicLog, kLibraryAgentLog);
    std::string text = amk::BuildReport(facts, HealthyEnvironment());

    CHECK(text.find("RÉSULTAT : RÉUSSI") != std::string::npos);
    CHECK(text.find("Apple Music : 1.1540.23042.0") != std::string::npos);
    CHECK(text.find("[OK]  ") != std::string::npos);
    CHECK(text.find("2 thread(s) repris") != std::string::npos);
    CHECK(text.find("remplacé par « utilisateur »") != std::string::npos);
}

TEST_CASE(RunReport_HidesTheUserNameInPaths) {
    std::string text =
        "[13:36:35.001] Paquet trouvé : X (dossier C:\\Users\\User\\AppData\\Local)\n"
        "dossier C:/users/alex/AppleMusicModKit, et C:\\Users\\User\n"
        "Alexine reste, et C:\\Users\\Userine\\x garde son nom\n";
    std::string hidden = amk::HideUserName(text, "l'utilisateur");

    CHECK(hidden.find("l'utilisateur\\") == std::string::npos);
    CHECK(hidden.find("alex/") == std::string::npos);
    CHECK(hidden.find("C:\\Users\\utilisateur\\AppData") != std::string::npos);
    CHECK(hidden.find("C:/users/utilisateur/AppleMusicModKit") != std::string::npos);
    CHECK(hidden.find("Alexine") != std::string::npos);  // a longer name is another user
    CHECK(hidden.find("C:\\Users\\utilisateur\n") != std::string::npos);
}

TEST_CASE(RunReport_NotRunTextSaysWhy) {
    amk::RunFacts facts = amk::ReadRunFacts("[13:36:35.001] Apple Music pour Windows introuvable\n", "", "");
    std::string text = amk::BuildReport(facts, HealthyEnvironment());

    CHECK(text.find("RÉSULTAT : NON EXÉCUTÉ") != std::string::npos);
    CHECK(text.find("Apple Music trouvé : introuvable") != std::string::npos);
    CHECK(text.find("Lancement suspendu : sans objet") != std::string::npos);
}
