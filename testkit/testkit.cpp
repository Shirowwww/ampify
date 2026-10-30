// AMK-Test.exe: the test kit of milestone M5. One double-click installs the kit, starts Apple Music through the
// launcher, records an ETW trace while the tester uses the app, collects the logs, and writes a report.
//
// It sits next to amk_setup.exe, amk_launcher.exe and amk_kernel.dll, and runs from any folder.
//
//   AMK-Test.exe                       guided run, with dialogs
//   AMK-Test.exe /auto /attente:120    no dialog: waits 120 seconds in place of the OK button (tests, CI bench)
//   AMK-Test.exe /sansetl              no ETW trace, so no administrator prompt
//
// The report goes to %USERPROFILE%\AppleMusicModKit\rapports\<date>\rapport.txt, and the whole folder is zipped
// next to it. Exit codes: 0 the report says the run passed, 2 needs review, 4 Apple Music did not run, 1 no report.

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "../core/apple_music.hpp"
#include "../core/journal.hpp"
#include "../core/kit_layout.hpp"
#include "../core/run_report.hpp"
#include "../win/package.hpp"
#include "../win/win_common.hpp"
#include "etl.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int kExitPassed = 0;
constexpr int kExitError = 1;
constexpr int kExitNeedsReview = 2;
constexpr int kExitNotRun = 4;

constexpr DWORD kAppStartTimeoutMs = 90 * 1000;
constexpr DWORD kPollMs = 500;

struct Options {
    bool automatic = false;
    int waitSeconds = 120;
    bool noTrace = false;
};

Options ParseOptions() {
    Options options;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        std::wstring_view arg = argv[i];
        if (_wcsicmp(argv[i], L"/auto") == 0) options.automatic = true;
        if (_wcsicmp(argv[i], L"/sansetl") == 0) options.noTrace = true;
        if (arg.size() > 9 && _wcsnicmp(argv[i], L"/attente:", 9) == 0) options.waitSeconds = _wtoi(argv[i] + 9);
    }
    LocalFree(argv);
    return options;
}

// The text of a file as bytes. Empty when the file is missing.
std::string ReadFileBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Windows writes some redirected output as UTF-16 with a byte order mark. Returns UTF-8 in both cases.
std::string ReadTextAsUtf8(const fs::path& file) {
    std::string bytes = ReadFileBytes(file);
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring wide(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / sizeof(wchar_t));
        return amk::win::ToUtf8(wide);
    }
    return bytes;
}

void WriteUtf8File(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "\xEF\xBB\xBF" << text;  // byte order mark, so Notepad reads the accents correctly
}

// The Windows edition and build, read from the registry.
std::string WindowsVersion() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return "inconnu";
    }

    auto readString = [key](const wchar_t* name) {
        DWORD type = 0;
        DWORD bytes = 0;
        if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_SZ) return std::string();
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
        while (!value.empty() && value.back() == L'\0') value.pop_back();
        return amk::win::ToUtf8(value);
    };

    std::string product = readString(L"ProductName");
    std::string display = readString(L"DisplayVersion");
    if (display.empty()) display = readString(L"ReleaseId");
    std::string build = readString(L"CurrentBuild");
    RegCloseKey(key);

    return product + (display.empty() ? "" : " " + display) + (build.empty() ? "" : " (build " + build + ")");
}

// "2026-10-30 13:50" for the report, "2026-10-30_13-50-00" for the folder name.
std::pair<std::string, std::string> Timestamps() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char reportDate[32];
    char folderName[32];
    std::snprintf(reportDate, sizeof(reportDate), "%04d-%02d-%02d %02d:%02d", now.wYear, now.wMonth, now.wDay, now.wHour,
                  now.wMinute);
    std::snprintf(folderName, sizeof(folderName), "%04d-%02d-%02d_%02d-%02d-%02d", now.wYear, now.wMonth, now.wDay,
                  now.wHour, now.wMinute, now.wSecond);
    return {reportDate, folderName};
}

std::string Megabytes(std::uintmax_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    for (char& c : text) {
        if (c == '.') c = ',';  // French decimal comma
    }
    return text;
}

bool WaitForAppleMusic(DWORD timeoutMs) {
    for (DWORD waited = 0; waited < timeoutMs; waited += kPollMs) {
        if (!amk::win::ProcessIdsNamed(L"AppleMusic.exe").empty()) return true;
        Sleep(kPollMs);
    }
    return false;
}

// Starts Apple Music the way the tester does: through the start menu shortcut, which runs the launcher.
bool StartFromShortcut(const fs::path& shortcut) {
    HINSTANCE result = ShellExecuteW(nullptr, L"open", shortcut.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

// Copies the logs of the kit, and the Apple Music settings of the kit, into the report folder.
void CollectKitFiles(const fs::path& destination) {
    fs::path journal = amk::win::KitDirectory() / L"journal";
    for (const wchar_t* name : {L"launcher.log", L"AppleMusic.log", L"AMPLibraryAgent.log", L"installation.log"}) {
        std::error_code error;
        fs::copy_file(journal / name, destination / name, fs::copy_options::overwrite_existing, error);
    }
    for (const wchar_t* name : {L"settings.txt", L"state.txt"}) {
        std::error_code error;
        fs::copy_file(amk::win::KitDirectory() / name, destination / name, fs::copy_options::overwrite_existing, error);
    }
}

// Every text file of the report folder, with the user's name removed from its paths. trace.etl is binary and is left alone.
void AnonymizeTextFiles(const fs::path& folder) {
    std::string userName = amk::win::ToUtf8(amk::win::KitDirectory().parent_path().filename().wstring());
    std::error_code error;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(folder, error)) {
        if (!entry.is_regular_file(error)) continue;
        std::wstring extension = entry.path().extension().wstring();
        if (_wcsicmp(extension.c_str(), L".txt") != 0 && _wcsicmp(extension.c_str(), L".log") != 0) continue;

        std::string text = ReadTextAsUtf8(entry.path());
        std::ofstream out(entry.path(), std::ios::binary | std::ios::trunc);
        out << amk::HideUserName(text, userName);
    }
}

// Apple Music's own events from the last 50 Application events. The text is checked for its name.
std::string CollectWindowsEvents(const fs::path& destination) {
    fs::path wevtutil = amk::win::KnownFolder(FOLDERID_System) / L"wevtutil.exe";
    fs::path file = destination / L"evenements-application.txt";
    int code = amk::win::RunAndWait(L"\"" + wevtutil.wstring() + L"\" qe Application /c:50 /rd:true /f:text", file);
    if (code != 0) return "wevtutil n'a pas répondu (code " + std::to_string(code) + ")";

    std::string text = ReadTextAsUtf8(file);
    int mentions = 0;
    for (size_t at = text.find("AppleMusic"); at != std::string::npos; at = text.find("AppleMusic", at + 1)) mentions++;
    if (mentions == 0) return "aucune mention d'Apple Music dans les 50 derniers événements Application";
    return std::to_string(mentions) + " mention(s) d'Apple Music dans les 50 derniers événements Application (voir evenements-application.txt)";
}

std::string TraceStatus(const fs::path& etl, bool requested, bool started, int lastCode) {
    if (!requested) return "non demandée (/sansetl)";
    std::error_code error;
    std::uintmax_t size = fs::file_size(etl, error);
    if (started && !error && size > 0) return "trace.etl, " + Megabytes(size);
    if (lastCode == -1) return "pas démarrée (droits administrateur refusés, ou logman absent)";
    return "pas de trace (logman code " + std::to_string(lastCode) + ")";
}

int Run(const Options& options) {
    auto [reportDate, folderName] = Timestamps();
    fs::path rapports = amk::win::KitDirectory() / L"rapports";
    std::wstring wideFolder = amk::win::FromUtf8(folderName);
    fs::path run = rapports / wideFolder;
    fs::path kitFolder = amk::win::ExecutablePath().parent_path();

    std::error_code error;
    fs::create_directories(run / L"journal", error);
    if (error) {
        amk::win::ShowMessage(L"Le dossier du rapport ne peut pas être créé : " + run.wstring(), true);
        return kExitError;
    }

    std::ofstream stepsFile(run / L"deroulement.txt", std::ios::trunc);
    amk::Journal log(stepsFile, amk::Journal::LocalTimeStamp);
    log.Write("Test M5, kit " + std::string(amk::kKitVersion) + ", système : " + WindowsVersion());

    if (!options.automatic) {
        bool go = amk::win::AskYesNo(
            L"Test de l'installation et du lancement d'Apple Music avec mods.\n\n"
            L"Le test installe le kit, lance Apple Music, enregistre pendant que vous l'utilisez, puis écrit un rapport.\n\n"
            L"Il faut environ 5 minutes. Continuer ?",
            L"Apple Music Mod Kit : test M5");
        if (!go) {
            log.Write("Test annulé par l'utilisateur");
            return kExitPassed;
        }
    }

    // Apple Music must start fresh for the run to count: an app that was already open is not launched by the kit.
    while (!amk::win::ProcessIdsNamed(L"AppleMusic.exe").empty()) {
        if (options.automatic) {
            log.Write("Apple Music est déjà ouvert : le test continue, mais il ne comptera pas comme un lancement");
            break;
        }
        if (MessageBoxW(nullptr, L"Fermez Apple Music, puis cliquez Réessayer.", L"Apple Music Mod Kit",
                        MB_ICONWARNING | MB_RETRYCANCEL) == IDCANCEL) {
            log.Write("Test annulé : Apple Music était ouvert");
            return kExitPassed;
        }
    }

    // The logs of an earlier run must not count for this one. The launcher and the installer replace theirs anyway.
    fs::remove(amk::win::KitDirectory() / L"journal" / L"AppleMusic.log", error);
    fs::remove(amk::win::KitDirectory() / L"journal" / L"AMPLibraryAgent.log", error);

    // 1. Install, with the installer that sits next to this test kit.
    int setupCode = amk::win::RunAndWait(L"\"" + (kitFolder / amk::win::FromUtf8(amk::kSetupFile)).wstring() + L"\" /silent");
    bool installOk = setupCode == 0;
    std::string installDetail = installOk ? "installé dans " + amk::win::ToUtf8(amk::win::InstallDirectory().wstring())
                                          : "échec (code " + std::to_string(setupCode) + "), voir installation.log";
    log.Write("Installation : " + installDetail);

    // 2. ETW trace. The first attempt uses every provider; a refusal of the list is retried with the first one only.
    fs::path etl = run / L"trace.etl";
    bool traceRequested = !options.noTrace;
    bool traceStarted = false;
    int traceCode = 0;
    if (traceRequested) {
        traceCode = amk::testkit::StartTrace(etl, false);
        if (traceCode > 0) {
            log.Write("logman : code " + std::to_string(traceCode) + " avec la liste complète, nouvel essai avec un fournisseur");
            traceCode = amk::testkit::StartTrace(etl, true);
        }
        traceStarted = traceCode == 0;
        log.Write(traceStarted ? "Trace ETW démarrée" : "Trace ETW non démarrée (code " + std::to_string(traceCode) + ")");
    }

    // 3. Launch through the shortcut, once the kit is installed and Apple Music is present.
    bool appLaunched = false;
    std::optional<amk::launcher::InstalledPackage> package =
        amk::launcher::FindInstalledPackage(amk::win::FromUtf8(amk::kPackageFamilyName).c_str());
    fs::path shortcut = amk::win::KnownFolder(FOLDERID_Programs) / amk::win::FromUtf8(amk::kShortcutFile);
    if (!installOk) {
        log.Write("Lancement ignoré : le kit n'est pas installé");
    } else if (!package) {
        log.Write("Lancement ignoré : Apple Music pour Windows introuvable");
    } else if (!StartFromShortcut(shortcut)) {
        log.Write("Lancement impossible : le raccourci ne répond pas");
    } else {
        appLaunched = WaitForAppleMusic(kAppStartTimeoutMs);
        log.Write(appLaunched ? "Apple Music ouvert" : "Apple Music ne s'est pas ouvert dans les 90 s");
    }

    // 4. Use the app for a while. The tester does this with the dialog open; /auto waits instead.
    if (appLaunched) {
        if (options.automatic) {
            Sleep(static_cast<DWORD>(options.waitSeconds) * 1000);
        } else {
            amk::win::ShowMessage(
                L"Apple Music est ouvert avec le kit.\n\n"
                L"Pendant environ 2 minutes :\n"
                L"  1. cherchez un album et lancez un titre stéréo ;\n"
                L"  2. lancez un titre Dolby Atmos si vous en avez un ;\n"
                L"  3. mettez en pause, passez au titre suivant, ouvrez les Réglages.\n\n"
                L"Quand c'est fait, cliquez OK. Ne fermez pas Apple Music avant.");
        }
        log.Write("Utilisation terminée");
    }

    // 5. Stop the trace, then collect everything.
    if (traceStarted) {
        int stopCode = amk::testkit::StopTrace();
        log.Write("Trace ETW arrêtée (code " + std::to_string(stopCode) + ")");
    }
    std::string traceStatus = TraceStatus(etl, traceRequested, traceStarted, traceCode);

    CollectKitFiles(run / L"journal");
    std::string eventsStatus = CollectWindowsEvents(run);
    log.Write("Journaux et événements collectés");

    // 6. The report, from the logs the run left behind.
    fs::path journal = run / L"journal";
    amk::RunFacts facts = amk::ReadRunFacts(ReadFileBytes(journal / L"launcher.log"), ReadFileBytes(journal / L"AppleMusic.log"),
                                            ReadFileBytes(journal / L"AMPLibraryAgent.log"));
    amk::RunEnvironment environment;
    environment.date = reportDate;
    environment.windows = WindowsVersion();
    environment.installOk = installOk;
    environment.installDetail = installDetail;
    environment.traceStatus = traceStatus;
    environment.eventsStatus = eventsStatus;

    amk::RunVerdict verdict = amk::JudgeRun(facts, environment);
    WriteUtf8File(run / L"rapport.txt", amk::BuildReport(facts, environment));
    log.Write("Rapport écrit");
    stepsFile.close();  // the folder is zipped next: no file may still be open in it
    AnonymizeTextFiles(run);

    // 7. Zip the folder, with tar (built into Windows 10 and 11), and point to the latest report.
    fs::path zip = rapports / (wideFolder + L".zip");
    fs::path tar = amk::win::KnownFolder(FOLDERID_System) / L"tar.exe";
    int zipCode = amk::win::RunAndWait(L"\"" + tar.wstring() + L"\" -a -c -f \"" + zip.wstring() + L"\" -C \"" +
                                       rapports.wstring() + L"\" \"" + wideFolder + L"\"");
    WriteUtf8File(rapports / L"dernier.txt", amk::win::ToUtf8(run.wstring()) + "\n");
    {
        std::ofstream appended(run / L"deroulement.txt", std::ios::app);
        amk::Journal afterZip(appended, amk::Journal::LocalTimeStamp);
        afterZip.Write(zipCode == 0 ? "Archive créée : " + amk::win::ToUtf8(zip.wstring())
                                    : "Archive non créée (tar, code " + std::to_string(zipCode) + ")");
    }

    int code = verdict == amk::RunVerdict::Passed      ? kExitPassed
               : verdict == amk::RunVerdict::NeedsReview ? kExitNeedsReview
                                                       : kExitNotRun;
    if (!options.automatic) {
        std::wstring result = verdict == amk::RunVerdict::Passed        ? L"RÉUSSI"
                              : verdict == amk::RunVerdict::NeedsReview ? L"À REVOIR"
                                                                        : L"NON EXÉCUTÉ";
        amk::win::ShowMessage(L"Test terminé : " + result + L".\n\nEnvoyez le fichier :\n" + zip.wstring() +
                              L"\n\nLes chemins sont anonymes, sauf dans trace.etl.");
        ShellExecuteW(nullptr, L"open", rapports.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return code;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int code = kExitError;
    try {
        code = Run(ParseOptions());
    } catch (const std::exception& error) {
        amk::win::ShowMessage(amk::win::FromUtf8(std::string("Erreur inattendue : ") + error.what()), true);
    }
    CoUninitialize();
    return code;
}
