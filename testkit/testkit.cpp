// Ampify-Test.exe: the test kit of milestone M5. One double-click installs Ampify, starts Apple Music through the
// launcher, records an ETW trace while the tester uses the app, collects the logs, and writes a report.
//
// It sits next to ampify_setup.exe, ampify_launcher.exe and ampify_kernel.dll, and runs from any folder.
//
//   Ampify-Test.exe                       guided run, with dialogs
//   Ampify-Test.exe /auto /wait:120       no dialog: waits 120 seconds in place of the OK button (tests, CI bench)
//   Ampify-Test.exe /noetl                no ETW trace, so no administrator prompt
//
// The report goes to %USERPROFILE%\Ampify\reports\<date>\report.txt, and the whole folder is zipped
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
        if (_wcsicmp(argv[i], L"/noetl") == 0) options.noTrace = true;
        if (arg.size() > 6 && _wcsnicmp(argv[i], L"/wait:", 6) == 0) options.waitSeconds = _wtoi(argv[i] + 6);
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
        return ampify::win::ToUtf8(wide);
    }
    return bytes;
}

void WriteUtf8File(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "\xEF\xBB\xBF" << text;  // byte order mark, so Notepad reads accents correctly
}

// The Windows edition and build, read from the registry.
std::string WindowsVersion() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return "unknown";
    }

    auto readString = [key](const wchar_t* name) {
        DWORD type = 0;
        DWORD bytes = 0;
        if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_SZ) return std::string();
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
        while (!value.empty() && value.back() == L'\0') value.pop_back();
        return ampify::win::ToUtf8(value);
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
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

bool WaitForAppleMusic(DWORD timeoutMs) {
    for (DWORD waited = 0; waited < timeoutMs; waited += kPollMs) {
        if (!ampify::win::ProcessIdsNamed(L"AppleMusic.exe").empty()) return true;
        Sleep(kPollMs);
    }
    return false;
}

// Starts Apple Music the way the tester does: through the start menu shortcut, which runs the launcher.
bool StartFromShortcut(const fs::path& shortcut) {
    HINSTANCE result = ShellExecuteW(nullptr, L"open", shortcut.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

// Copies the logs of Ampify, and its settings and state, into the report folder.
void CollectKitFiles(const fs::path& destination) {
    fs::path logs = ampify::win::KitDirectory() / L"logs";
    for (const wchar_t* name : {L"launcher.log", L"AppleMusic.log", L"AMPLibraryAgent.log", L"install.log"}) {
        std::error_code error;
        fs::copy_file(logs / name, destination / name, fs::copy_options::overwrite_existing, error);
    }
    for (const wchar_t* name : {L"settings.txt", L"state.txt"}) {
        std::error_code error;
        fs::copy_file(ampify::win::KitDirectory() / name, destination / name, fs::copy_options::overwrite_existing, error);
    }
}

// Every text file of the report folder, with the user's name removed from its paths. trace.etl is binary and is left alone.
void AnonymizeTextFiles(const fs::path& folder) {
    std::string userName = ampify::win::ToUtf8(ampify::win::KitDirectory().parent_path().filename().wstring());
    std::error_code error;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(folder, error)) {
        if (!entry.is_regular_file(error)) continue;
        std::wstring extension = entry.path().extension().wstring();
        if (_wcsicmp(extension.c_str(), L".txt") != 0 && _wcsicmp(extension.c_str(), L".log") != 0) continue;

        std::string text = ReadTextAsUtf8(entry.path());
        std::ofstream out(entry.path(), std::ios::binary | std::ios::trunc);
        out << ampify::HideUserName(text, userName);
    }
}

// Apple Music's own events from the last 50 Application events. The text is checked for its name.
std::string CollectWindowsEvents(const fs::path& destination) {
    fs::path wevtutil = ampify::win::KnownFolder(FOLDERID_System) / L"wevtutil.exe";
    fs::path file = destination / L"application-events.txt";
    int code = ampify::win::RunAndWait(L"\"" + wevtutil.wstring() + L"\" qe Application /c:50 /rd:true /f:text", file);
    if (code != 0) return "wevtutil did not answer (code " + std::to_string(code) + ")";

    std::string text = ReadTextAsUtf8(file);
    int mentions = 0;
    for (size_t at = text.find("AppleMusic"); at != std::string::npos; at = text.find("AppleMusic", at + 1)) mentions++;
    if (mentions == 0) return "no mention of Apple Music in the last 50 Application events";
    return std::to_string(mentions) + " mention(s) of Apple Music in the last 50 Application events (see application-events.txt)";
}

std::string TraceStatus(const fs::path& etl, bool requested, bool started, int lastCode) {
    if (!requested) return "not requested (/noetl)";
    std::error_code error;
    std::uintmax_t size = fs::file_size(etl, error);
    if (started && !error && size > 0) return "trace.etl, " + Megabytes(size);
    if (lastCode == -1) return "not started (administrator rights refused, or logman missing)";
    return "no trace (logman code " + std::to_string(lastCode) + ")";
}

int Run(const Options& options) {
    auto [reportDate, folderName] = Timestamps();
    fs::path reports = ampify::win::KitDirectory() / L"reports";
    std::wstring wideFolder = ampify::win::FromUtf8(folderName);
    fs::path run = reports / wideFolder;
    fs::path kitFolder = ampify::win::ExecutablePath().parent_path();

    std::error_code error;
    fs::create_directories(run / L"logs", error);
    if (error) {
        ampify::win::ShowMessage(L"The report folder could not be created: " + run.wstring(), true);
        return kExitError;
    }

    std::ofstream stepsFile(run / L"steps.txt", std::ios::trunc);
    ampify::Journal log(stepsFile, ampify::Journal::LocalTimeStamp);
    log.Write("M5 test, Ampify " + std::string(ampify::kKitVersion) + ", system: " + WindowsVersion());

    if (!options.automatic) {
        bool go = ampify::win::AskYesNo(
            L"Test of the install and launch of Apple Music with Ampify.\n\n"
            L"The test installs Ampify, starts Apple Music, records while you use it, then writes a report.\n\n"
            L"It takes about 5 minutes. Continue?",
            L"Ampify: M5 test");
        if (!go) {
            log.Write("Test cancelled by the user");
            return kExitPassed;
        }
    }

    // Apple Music must start fresh for the run to count: an app that was already open is not launched by Ampify.
    while (!ampify::win::ProcessIdsNamed(L"AppleMusic.exe").empty()) {
        if (options.automatic) {
            log.Write("Apple Music is already open: the test goes on, but it will not count as a launch");
            break;
        }
        if (MessageBoxW(nullptr, L"Close Apple Music, then click Retry.", L"Ampify",
                        MB_ICONWARNING | MB_RETRYCANCEL) == IDCANCEL) {
            log.Write("Test cancelled: Apple Music was open");
            return kExitPassed;
        }
    }

    // The logs of an earlier run must not count for this one. The launcher and the installer replace theirs anyway.
    fs::remove(ampify::win::KitDirectory() / L"logs" / L"AppleMusic.log", error);
    fs::remove(ampify::win::KitDirectory() / L"logs" / L"AMPLibraryAgent.log", error);

    // 1. Install, with the installer that sits next to this test kit.
    int setupCode = ampify::win::RunAndWait(L"\"" + (kitFolder / ampify::win::FromUtf8(ampify::kSetupFile)).wstring() + L"\" /silent");
    bool installOk = setupCode == 0;
    std::string installDetail = installOk ? "installed in " + ampify::win::ToUtf8(ampify::win::InstallDirectory().wstring())
                                          : "failed (code " + std::to_string(setupCode) + "), see install.log";
    log.Write("Install: " + installDetail);

    // 2. ETW trace. The first attempt uses every provider; a refusal of the list is retried with the first one only.
    fs::path etl = run / L"trace.etl";
    bool traceRequested = !options.noTrace;
    bool traceStarted = false;
    int traceCode = 0;
    if (traceRequested) {
        traceCode = ampify::testkit::StartTrace(etl, false);
        if (traceCode > 0) {
            log.Write("logman: code " + std::to_string(traceCode) + " with the full list, trying again with one provider");
            traceCode = ampify::testkit::StartTrace(etl, true);
        }
        traceStarted = traceCode == 0;
        log.Write(traceStarted ? "ETW trace started" : "ETW trace not started (code " + std::to_string(traceCode) + ")");
    }

    // 3. Launch through the shortcut, once Ampify is installed and Apple Music is present.
    bool appLaunched = false;
    std::optional<ampify::launcher::InstalledPackage> package =
        ampify::launcher::FindInstalledPackage(ampify::win::FromUtf8(ampify::kPackageFamilyName).c_str());
    fs::path shortcut = ampify::win::KnownFolder(FOLDERID_Programs) / ampify::win::FromUtf8(ampify::kShortcutFile);
    if (!installOk) {
        log.Write("Launch skipped: Ampify is not installed");
    } else if (!package) {
        log.Write("Launch skipped: Apple Music for Windows not found");
    } else if (!StartFromShortcut(shortcut)) {
        log.Write("Could not launch: the shortcut does not answer");
    } else {
        appLaunched = WaitForAppleMusic(kAppStartTimeoutMs);
        log.Write(appLaunched ? "Apple Music opened" : "Apple Music did not open within 90 s");
    }

    // 4. Use the app for a while. The tester does this with the dialog open; /auto waits instead.
    if (appLaunched) {
        if (options.automatic) {
            Sleep(static_cast<DWORD>(options.waitSeconds) * 1000);
        } else {
            ampify::win::ShowMessage(
                L"Apple Music is open with Ampify.\n\n"
                L"For about 2 minutes:\n"
                L"  1. search for an album and play a stereo track;\n"
                L"  2. play a Dolby Atmos track if you have one;\n"
                L"  3. pause, skip to the next track, open Settings.\n\n"
                L"When you are done, click OK. Do not close Apple Music before that.");
        }
        log.Write("Use finished");
    }

    // 5. Stop the trace, then collect everything.
    if (traceStarted) {
        int stopCode = ampify::testkit::StopTrace();
        log.Write("ETW trace stopped (code " + std::to_string(stopCode) + ")");
    }
    std::string traceStatus = TraceStatus(etl, traceRequested, traceStarted, traceCode);

    CollectKitFiles(run / L"logs");
    std::string eventsStatus = CollectWindowsEvents(run);
    log.Write("Logs and events collected");

    // 6. The report, from the logs the run left behind.
    fs::path logs = run / L"logs";
    ampify::RunFacts facts = ampify::ReadRunFacts(ReadFileBytes(logs / L"launcher.log"), ReadFileBytes(logs / L"AppleMusic.log"),
                                            ReadFileBytes(logs / L"AMPLibraryAgent.log"));
    ampify::RunEnvironment environment;
    environment.date = reportDate;
    environment.windows = WindowsVersion();
    environment.installOk = installOk;
    environment.installDetail = installDetail;
    environment.traceStatus = traceStatus;
    environment.eventsStatus = eventsStatus;

    ampify::RunVerdict verdict = ampify::JudgeRun(facts, environment);
    WriteUtf8File(run / L"report.txt", ampify::BuildReport(facts, environment));
    log.Write("Report written");
    stepsFile.close();  // the folder is zipped next: no file may still be open in it
    AnonymizeTextFiles(run);

    // 7. Zip the folder, with tar (built into Windows 10 and 11), and point to the latest report.
    fs::path zip = reports / (wideFolder + L".zip");
    fs::path tar = ampify::win::KnownFolder(FOLDERID_System) / L"tar.exe";
    int zipCode = ampify::win::RunAndWait(L"\"" + tar.wstring() + L"\" -a -c -f \"" + zip.wstring() + L"\" -C \"" +
                                       reports.wstring() + L"\" \"" + wideFolder + L"\"");
    WriteUtf8File(reports / L"latest.txt", ampify::win::ToUtf8(run.wstring()) + "\n");
    {
        std::ofstream appended(run / L"steps.txt", std::ios::app);
        ampify::Journal afterZip(appended, ampify::Journal::LocalTimeStamp);
        afterZip.Write(zipCode == 0 ? "Archive created: " + ampify::win::ToUtf8(zip.wstring())
                                    : "Archive not created (tar, code " + std::to_string(zipCode) + ")");
    }

    int code = verdict == ampify::RunVerdict::Passed      ? kExitPassed
               : verdict == ampify::RunVerdict::NeedsReview ? kExitNeedsReview
                                                       : kExitNotRun;
    if (!options.automatic) {
        std::wstring result = verdict == ampify::RunVerdict::Passed        ? L"PASSED"
                              : verdict == ampify::RunVerdict::NeedsReview ? L"NEEDS REVIEW"
                                                                        : L"NOT RUN";
        ampify::win::ShowMessage(L"Test finished: " + result + L".\n\nSend this file:\n" + zip.wstring() +
                              L"\n\nPaths are anonymous, except in trace.etl.");
        ShellExecuteW(nullptr, L"open", reports.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
        ampify::win::ShowMessage(ampify::win::FromUtf8(std::string("Unexpected error: ") + error.what()), true);
    }
    CoUninitialize();
    return code;
}
