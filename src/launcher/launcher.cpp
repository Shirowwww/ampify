// ampify_launcher.exe: loads the kernel into Apple Music's processes. The package folder is never written to.
//
//   ampify_launcher.exe                starts Apple Music with the kernel, and stays open while it runs
//   ampify_launcher.exe --register     from Windows sign-in: has Windows start the launcher with each process of Apple
//                                      Music, then ends (src/win/package_debug.hpp); --background does the same, for
//                                      the sign-in entry of Ampify 1.1. Where Windows refuses, it stays in the
//                                      background and loads the kernel each time Apple Music starts
//   ampify_launcher.exe --attach -p <process> -tid <thread>
//                                      run by Windows for a process of Apple Music it holds suspended: loads the kernel
//                                      before the process runs, then resumes it
//
// Started by hand or in the background, it follows the package while Apple Music runs, so every process gets the
// kernel, including ones that appear later (a hand-over to a new process, or AMPLibraryAgent.exe).

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../core/apple_music.hpp"
#include "../core/build_signature.hpp"
#include "../core/injection_policy.hpp"
#include "../core/journal.hpp"
#include "../core/kit_layout.hpp"
#include "../core/launch_flags.hpp"
#include "../core/off_notice.hpp"
#include "../core/orphan_agent.hpp"
#include "../core/package_watch.hpp"
#include "../core/safe_mode.hpp"
#include "../core/settings.hpp"
#include "../core/settings_catalog.hpp"
#include "../core/version_gate.hpp"
#include "../core/version_list.hpp"
#include "../win/background.hpp"
#include "../win/package.hpp"
#include "../win/package_debug.hpp"
#include "../win/win_common.hpp"
#include "injection.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr DWORD kPollIntervalMs = 250;

// Once Apple Music has settled (ampify::PackageWatch), the watch looks this often: four looks a second, each two
// process lists, cost about 85 ms of processor a second for as long as the app stayed open. A process that starts
// later, such as a restarted library agent, gets the kernel within this delay.
constexpr DWORD kSettledPollMs = 2000;

// While Apple Music is closed, the background launcher looks for it this often: soon enough to hold its first
// process early, and a process list read four times a second costs next to nothing.
constexpr DWORD kBackgroundPollMs = 250;

// When the package cannot be read (Apple Music being updated, or not installed), the next look waits longer.
constexpr DWORD kBackgroundRetryMs = 5000;

// The stuck-agent look reads the process list, so it runs every 2 s, not at each 250 ms poll; the 10 s grace still holds.
constexpr ULONGLONG kOrphanLookMs = 2000;

// The first process is held still while the kernel loads, so this wait must stay short: the app would freeze for it.
// A load takes about 60 ms; a first one scanned by an antivirus took 3 s, and finishes after the resume instead.
constexpr DWORD kHeldLoadTimeoutMs = 1000;

// The library agent opens Apple Music's library as it starts; the kernel waits that long before it loads there.
const DWORD kAgentSettleMs = static_cast<DWORD>(ampify::WatchConfig{}.agentSettleAfter * 1000);

std::wstring ToWide(std::string_view text) {
    return std::wstring(text.begin(), text.end());
}

std::string ToNarrow(const std::wstring& text) {
    return std::string(text.begin(), text.end());
}

std::string HResultText(HRESULT hr) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
    return buffer;
}

void Alert(const wchar_t* text) {
    MessageBoxW(nullptr, text, L"Ampify", MB_ICONERROR | MB_OK);
}

bool InjectInto(ampify::Journal& journal, DWORD processId, const std::filesystem::path& kernelDll,
                const std::string& label) {
    HANDLE process = ampify::launcher::OpenProcessForInjection(processId);
    if (!process) {
        journal.Write(label + ": could not open (Windows error " + std::to_string(GetLastError()) + ")");
        return false;
    }

    std::string failure;
    bool loaded = ampify::launcher::InjectKernel(process, kernelDll, failure);
    CloseHandle(process);

    journal.Write(loaded ? label + ": kernel loaded" : label + ": failed, " + failure);
    return loaded;
}

// Holds the first Apple Music process still while the kernel loads, then resumes it, whatever the outcome.
// Windows offers no way to start the app suspended from here (a package debug setting alone does not suspend
// it), so the hold begins when ActivateApp returns. The app may already have run for a moment by then: the
// log says how many threads were held and whether the kernel loaded during the hold.
bool InjectWhileHeld(ampify::Journal& journal, DWORD processId, const std::filesystem::path& kernelDll) {
    std::string label = std::string(ampify::kAppleMusicImage) + " (" + std::to_string(processId) + ")";
    HANDLE process = ampify::launcher::OpenProcessForInjection(processId);
    if (!process) {
        journal.Write(label + ": could not open, no pause (Windows error " +
                      std::to_string(GetLastError()) + ")");
        return false;
    }

    int held = ampify::launcher::SuspendProcessThreads(processId);
    journal.Write(std::to_string(held) + " thread(s) paused");

    std::string failure;
    HANDLE pending = nullptr;
    bool loaded = ampify::launcher::InjectKernel(process, kernelDll, failure, kHeldLoadTimeoutMs, &pending);
    if (loaded) {
        journal.Write(label + ": kernel loaded during the pause");
    } else if (pending) {
        // LoadLibraryW waits for the loader lock, which a held thread may own while the app is still starting.
        journal.Write(label + ": load pending during the pause, resuming threads");
    } else {
        journal.Write(label + ": failed, " + failure);
    }

    // Resumed on every path, so the app is never left frozen, even when the load failed.
    int resumed = ampify::launcher::ResumeProcessThreads(processId);
    journal.Write(std::to_string(resumed) + " thread(s) resumed after injection");

    if (pending) {
        failure.clear();
        loaded = ampify::launcher::FinishRemoteLoad(pending, ampify::launcher::kDefaultRemoteLoadTimeoutMs, failure);
        journal.Write(loaded ? label + ": kernel loaded after the resume" : label + ": failed, " + failure);
    }

    CloseHandle(process);
    return loaded;
}

ampify::WatchConfig MakeWatchConfig(bool inject, bool reportHealth) {
    ampify::WatchConfig config;
    config.inject = inject;
    config.reportHealth = reportHealth;
    return config;
}

std::vector<uint32_t> PackageIds(const std::vector<DWORD>& processIds) {
    return std::vector<uint32_t>(processIds.begin(), processIds.end());
}

// Waits `ms`, or less when `stop` is set. True when it is: the caller stops. Without an event, a plain wait.
bool WaitOrStop(HANDLE stop, DWORD ms) {
    if (!stop) {
        Sleep(ms);
        return false;
    }
    return WaitForSingleObject(stop, ms) == WAIT_OBJECT_0;
}

// True when `image` is the library agent of the package in `packageDirectory`.
bool IsPackageAgent(const std::wstring& image, const std::filesystem::path& packageDirectory) {
    const std::wstring prefix = packageDirectory.wstring();
    return image.size() > prefix.size() && _wcsnicmp(image.c_str(), prefix.c_str(), prefix.size()) == 0 &&
           _wcsicmp(std::filesystem::path(image).filename().c_str(), ampify::kLibraryAgentImageW) == 0;
}

// The full image path of `process`, empty when Windows does not give it.
std::wstring ImagePathOf(HANDLE process) {
    std::wstring image(32768, L'\0');
    DWORD length = static_cast<DWORD>(image.size());
    if (!QueryFullProcessImageNameW(process, 0, image.data(), &length)) return {};
    image.resize(length);
    return image;
}

// Ends one library agent that Apple Music left behind. Only called while no Apple Music runs (ampify::DecideOrphanAgent).
// The image is checked again on the handle that ends it, since the id may have gone to another process since the list,
// and the agent is given a moment to be gone before Apple Music starts. False when Windows refused to end it.
bool EndOrphanAgent(ampify::Journal& journal, DWORD processId, const std::filesystem::path& packageDirectory) {
    constexpr DWORD kExitWaitMs = 2000;
    const std::string label = "Library agent (" + std::to_string(processId) + ")";
    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        journal.Write(label + ": could not be opened to end it (Windows error " + std::to_string(GetLastError()) + ")");
        return false;
    }
    if (!IsPackageAgent(ImagePathOf(process), packageDirectory)) {
        CloseHandle(process);
        journal.Write(label + ": that id is no longer Apple Music's library agent, left alone");
        return true;
    }
    if (!TerminateProcess(process, 0)) {
        DWORD error = GetLastError();
        CloseHandle(process);
        journal.Write(label + ": could not be ended (Windows error " + std::to_string(error) + "), not tried again");
        return false;
    }
    bool gone = WaitForSingleObject(process, kExitWaitMs) == WAIT_OBJECT_0;
    CloseHandle(process);
    journal.Write(label + " ended: Apple Music was closed and it had not left" +
                  (gone ? std::string() : std::string(" (still exiting after 2 s)")));
    return true;
}

bool OrphanAgentSettingOn(const std::filesystem::path& kit) {
    ampify::Settings settings;
    settings.Load((kit / "settings.txt").string());
    return ampify::ChoiceOf(settings, ampify::kOrphanAgentKey) == "on";
}

// One look at a library agent left running, each time the launcher finds Apple Music closed. The agent gets
// kOrphanGraceMs to leave by itself, then is ended; nothing is done while Apple Music runs.
class OrphanAgentWatch {
public:
    explicit OrphanAgentWatch(bool enabled) : m_enabled(enabled) {}

    void SetEnabled(bool enabled) {
        m_enabled = enabled;
        m_since = 0;
    }

    ampify::OrphanAction Step(ampify::Journal& journal, const std::filesystem::path& packageDirectory, bool appRunning) {
        if (!m_enabled || appRunning) {
            m_since = 0;
            return ampify::OrphanAction::None;
        }
        std::vector<uint32_t> agents =
            m_refusals.Untried(PackageIds(ampify::launcher::FindPackageProcesses(ampify::kLibraryAgentImageW, packageDirectory)));
        if (agents.empty()) {
            m_since = 0;
            return ampify::OrphanAction::None;
        }
        ULONGLONG now = GetTickCount64();
        if (m_since == 0) {
            m_since = now;
            journal.Write("A library agent runs with Apple Music closed: given " +
                          std::to_string(ampify::kOrphanGraceMs / 1000) + " s to leave");
        }
        ampify::OrphanAction action = ampify::DecideOrphanAgent(true, false, true, now - m_since);
        if (action == ampify::OrphanAction::End) {
            for (uint32_t processId : agents) {
                if (!EndOrphanAgent(journal, processId, packageDirectory)) m_refusals.Refused(processId);
            }
            m_since = 0;
        }
        return action;
    }

private:
    bool m_enabled;
    ULONGLONG m_since = 0;  // when the agent was first seen alone; 0 when none is
    ampify::OrphanRefusals m_refusals;
};

// A left-over agent would hold the library (error 9039).
void ClearOrphanAgent(ampify::Journal& journal, const std::filesystem::path& packageDirectory, bool enabled) {
    OrphanAgentWatch watch(enabled);
    while (true) {
        bool appRunning = !ampify::launcher::FindPackageProcesses(ampify::kAppleMusicImageW, packageDirectory).empty();
        if (watch.Step(journal, packageDirectory, appRunning) != ampify::OrphanAction::WaitForExit) return;
        Sleep(kPollIntervalMs);
    }
}

// Follows the package until Apple Music has been gone for a while, or until `stop` is set. Each process it sees gets
// the kernel once. When `config.reportHealth` is set, the safe-mode count is cleared once the app has been responsive
// for 20 s. The rules live in ampify::PackageWatch; this function only reads the system and carries out the actions.
void Supervise(ampify::Journal& journal, const std::filesystem::path& packageDirectory,
               const std::filesystem::path& kernelDll, ampify::WatchConfig config, std::optional<DWORD> handledProcess,
               ampify::SafeMode* healthCheck, HANDLE stop = nullptr) {
    ampify::PackageWatch watch(config);
    if (handledProcess) watch.MarkHandled(*handledProcess);

    Clock::time_point started = Clock::now();
    while (true) {
        ampify::WatchSnapshot snapshot;
        snapshot.seconds = std::chrono::duration<double>(Clock::now() - started).count();
        std::vector<std::vector<DWORD>> running =
            ampify::launcher::FindPackageProcesses({ampify::kAppleMusicImageW, ampify::kLibraryAgentImageW}, packageDirectory);
        snapshot.appProcesses = PackageIds(running[0]);
        snapshot.agentProcesses = PackageIds(running[1]);
        for (uint32_t processId : snapshot.appProcesses) {
            if (!watch.NeedsResponsiveness()) break;
            if (ampify::win::WindowsResponsive(processId)) {
                snapshot.appResponsive = true;
                break;
            }
        }

        ampify::WatchActions actions = watch.Step(snapshot);
        for (uint32_t processId : actions.inject) {
            bool isApp = std::find(snapshot.appProcesses.begin(), snapshot.appProcesses.end(), processId) !=
                         snapshot.appProcesses.end();
            std::string image(isApp ? ampify::kAppleMusicImage : ampify::kLibraryAgentImage);
            InjectInto(journal, processId, kernelDll, image + " (" + std::to_string(processId) + ")");
        }

        if (actions.markHealthy) {
            healthCheck->MarkHealthy();
            journal.Write("Healthy after 20 s, reported by the launcher: "
                          "safe mode count reset");
        }

        if (actions.stop) {
            journal.Write(actions.stopReason + ": the launcher stops following Apple Music");
            return;
        }
        if (WaitOrStop(stop, actions.settled ? kSettledPollMs : kPollIntervalMs)) {
            journal.Write("Asked to stop: the launcher stops following Apple Music");
            return;
        }
    }
}

// What one Apple Music session needs to decide on the kernel. Read again for each session: an update of Apple Music
// changes the package folder, and can change the player DLL under the same name.
struct Session {
    ampify::launcher::InstalledPackage package;
    std::string version;
    std::filesystem::path kit;
    ampify::BuildSignature signature;
    std::filesystem::path kernelDll;
    bool kernelPresent = false;
    // Builds tested after this build of Ampify was compiled, from `ampify versions update`. The signature is still checked.
    std::vector<std::string> extraVersions;
    std::filesystem::file_time_type playerWritten{};  // of the player DLL, to see an update under the same folder

    ampify::PolicyVerdict Decide(bool safeMode) const {
        return ampify::DecideInjection(version, safeMode, signature, ampify::KnownAppVersionsWith(extraVersions));
    }

    std::filesystem::path PlayerDll() const { return package.directory / std::string(ampify::kPlayerDllName); }

    // True when Apple Music was updated or removed since this session was read.
    bool Stale() const {
        std::error_code error;
        std::filesystem::file_time_type written = std::filesystem::last_write_time(PlayerDll(), error);
        return error || written != playerWritten;
    }
};

std::optional<Session> OpenSession(ampify::Journal& journal, const std::filesystem::path& kit) {
    std::wstring family = ToWide(ampify::kPackageFamilyName);
    std::optional<ampify::launcher::InstalledPackage> package = ampify::launcher::FindInstalledPackage(family.c_str());
    if (!package) {
        journal.Write("Apple Music for Windows not found");
        return std::nullopt;
    }
    Session session;
    session.package = *package;
    std::string fullName = ToNarrow(package->fullName);
    session.version = ampify::AppleMusicVersion(fullName);
    session.kit = kit;
    journal.Write("Package found: " + fullName + " (folder " + package->directory.string() + ")");

    std::error_code timeError;
    session.playerWritten = std::filesystem::last_write_time(session.PlayerDll(), timeError);
    session.signature = ampify::CheckBuildSignature(ampify::win::ReadFileBytes(session.PlayerDll()));
    journal.Write(ampify::DescribeSignature(session.signature));

    session.kernelDll = ampify::win::ExecutablePath().parent_path() / ampify::kKernelFile;
    session.kernelPresent = std::filesystem::exists(session.kernelDll);
    if (!session.kernelPresent) journal.Write(std::string(ampify::kKernelFile) + " not found next to the launcher");
    session.extraVersions = ampify::LoadVersionList(kit / std::string(ampify::kVersionListFile));
    return session;
}

// Tells the person, once, why Ampify stays off: Apple Music updated itself to a family that was not tested, or safe
// mode. It is `ampify notice` in a process of its own, so neither this launcher nor a held Apple Music waits for it.
void TellIfOff(ampify::Journal& journal, const Session& session, const ampify::PolicyVerdict& verdict) {
    if (!ampify::TakeOffNotice(session.kit / "cache" / "off-notice.txt", verdict, session.version)) return;
    std::filesystem::path folder = ampify::win::ExecutablePath().parent_path();
    std::filesystem::path cli = folder / L"ampify.exe";
    if (!std::filesystem::exists(cli)) return;
    std::wstring what = L"version ";
    if (verdict.cause == ampify::OffCause::SafeMode) what = L"safe-mode";
    else if (verdict.cause == ampify::OffCause::WrongExports) what = L"exports ";
    if (verdict.cause != ampify::OffCause::SafeMode) what += ToWide(session.version);
    std::wstring command = L"\"" + cli.wstring() + L"\" notice " + what;
    bool started = ampify::win::StartProcess(cli.c_str(), command, CREATE_NO_WINDOW, folder.c_str(), true);
    journal.Write(started ? "Ampify is off: the person is told once" : "Ampify is off: the notice could not start");
}

// Apple Music was already open: nothing was started, so this is not a launch to count.
void FollowOpenApp(ampify::Journal& journal, const Session& session, ampify::SafeMode& safeMode, DWORD processId,
                   HANDLE stop) {
    ampify::PolicyVerdict verdict = session.Decide(safeMode.IsActive() || safeMode.ThisLaunchSafe());
    bool inject = verdict.allowed && session.kernelPresent;
    TellIfOff(journal, session, verdict);
    journal.Write("Apple Music already open (process " + std::to_string(processId) + "): " + verdict.reason);

    bool injected = inject && InjectInto(journal, processId, session.kernelDll,
                                         std::string(ampify::kAppleMusicImage) + " (" + std::to_string(processId) + ")");
    Supervise(journal, session.package.directory, session.kernelDll, MakeWatchConfig(inject, !injected), processId,
              &safeMode, stop);
}

// Apple Music has just started (process `processId`): a launch to count for safe mode. Its first process is held
// while the kernel loads. Without a kernel in the app, nothing reports health: the launcher then does it, so a launch
// that does not inject (safe mode, unknown build, failed load) still clears the count and cannot lock the mods out.
void FollowNewApp(ampify::Journal& journal, const Session& session, ampify::SafeMode& safeMode, DWORD processId,
                  HANDLE stop) {
    ampify::PolicyVerdict verdict = session.Decide(safeMode.BeginLaunch());
    journal.Write(verdict.reason);
    TellIfOff(journal, session, verdict);
    bool inject = verdict.allowed && session.kernelPresent;
    bool injected = inject && InjectWhileHeld(journal, processId, session.kernelDll);
    Supervise(journal, session.package.directory, session.kernelDll, MakeWatchConfig(inject, !injected), processId,
              &safeMode, stop);
}

// app_launch_flags: extra arguments for Apple Music. An invalid value is written to the log and not passed.
std::wstring LaunchArguments(ampify::Journal& journal, const std::filesystem::path& kit) {
    ampify::Settings settings;
    settings.Load((kit / "settings.txt").string());
    std::string flagsError;
    std::string flags = ampify::AppLaunchArguments(settings.GetString(std::string(ampify::kLaunchFlagsKey), ""), &flagsError);
    if (!flagsError.empty()) journal.Write(flagsError + ": no argument passed to Apple Music");
    if (!flags.empty()) journal.Write("Arguments passed to Apple Music: " + flags);
    return ToWide(flags);
}

int Run(ampify::Journal& journal, const std::filesystem::path& kit) {
    std::wstring appId = ToWide(ampify::kAppUserModelId);
    std::wstring appFamily = ToWide(ampify::kPackageFamilyName);
    if (ampify::win::BackgroundRunning()) {
        // The background launcher loads the kernel when the app starts: two launchers would count the launch twice.
        journal.Write("The background launcher runs: it loads the kernel, this launcher only starts Apple Music");
        if (std::optional<ampify::launcher::InstalledPackage> package = ampify::launcher::FindInstalledPackage(appFamily.c_str())) {
            ClearOrphanAgent(journal, package->directory, OrphanAgentSettingOn(kit));
        }
        DWORD processId = 0;
        HRESULT hr = ampify::launcher::ActivateApp(appId.c_str(), &processId, LaunchArguments(journal, kit));
        if (FAILED(hr)) Alert(L"Apple Music could not start.");
        return FAILED(hr) ? 1 : 0;
    }

    std::optional<Session> session = OpenSession(journal, kit);
    if (!session) {
        Alert(L"Apple Music for Windows is not installed on this PC.");
        return 1;
    }
    ampify::SafeMode safeMode((kit / "state.txt").string());
    std::vector<DWORD> running = ampify::launcher::FindPackageProcesses(ampify::kAppleMusicImageW, session->package.directory);
    if (!running.empty()) {
        FollowOpenApp(journal, *session, safeMode, running.front(), nullptr);
        return 0;
    }

    ClearOrphanAgent(journal, session->package.directory, OrphanAgentSettingOn(kit));
    DWORD processId = 0;
    HRESULT activationHr = ampify::launcher::ActivateApp(appId.c_str(), &processId, LaunchArguments(journal, kit));
    if (FAILED(activationHr)) {
        journal.Write("Activation refused (" + HResultText(activationHr) + ")");
        Alert(L"Apple Music could not start.");
        return 1;
    }
    journal.Write("Apple Music started (process " + std::to_string(processId) + ")");
    FollowNewApp(journal, *session, safeMode, processId, nullptr);
    return 0;
}

// While Apple Music is closed, the background launcher runs `ampify update --auto` now and then: a few minutes after
// sign-in, a minute after Apple Music quits, then every hour. ampify follows kit_update and looks for a release at most
// once a day, so most runs end at once. An update it installs restarts this launcher (src/cli/marketplace.cpp). Only
// the installed copy does this. Output: logs\update.log, kept to its last runs.
class AutoUpdater {
public:
    ~AutoUpdater() {
        if (m_process) CloseHandle(m_process);
    }

    // Apple Music has just quit: the next run comes soon, for someone who keeps it open in the notification area.
    void SoonAfterSession() { m_next = (std::min)(m_next, GetTickCount64() + kAfterSessionMs); }

    // One run, now, left to end on its own (sign-in, when nothing stays in the background to run it later).
    void StartOnce(ampify::Journal& journal) { Start(journal); }

    void Step(ampify::Journal& journal) {
        if (m_process) {
            if (WaitForSingleObject(m_process, 0) != WAIT_OBJECT_0) return;
            DWORD code = 0;
            GetExitCodeProcess(m_process, &code);
            if (code != 0) journal.Write("ampify update --auto ended with code " + std::to_string(code) + " (logs\\update.log)");
            CloseHandle(m_process);
            m_process = nullptr;
        }
        ULONGLONG now = GetTickCount64();
        if (now < m_next) return;
        m_next = now + kEveryMs;
        Start(journal);
    }

private:
    static constexpr ULONGLONG kFirstDelayMs = 3 * 60 * 1000;
    static constexpr ULONGLONG kAfterSessionMs = 60 * 1000;
    static constexpr ULONGLONG kEveryMs = 60 * 60 * 1000;
    static constexpr LONGLONG kLogLimitBytes = 64 * 1024;

    // logs\update.log, appended to so the run that installed stays readable; started again past kLogLimitBytes.
    static HANDLE OpenLog() {
        std::filesystem::path file = ampify::win::JournalFile("update.log");
        std::error_code ec;
        auto size = std::filesystem::file_size(file, ec);
        DWORD disposition = !ec && size > static_cast<std::uintmax_t>(kLogLimitBytes) ? CREATE_ALWAYS : OPEN_ALWAYS;
        SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
        HANDLE log = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, &inherit, disposition,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log == INVALID_HANDLE_VALUE) return log;
        SYSTEMTIME now{};
        GetLocalTime(&now);
        char line[64];
        int length = std::snprintf(line, sizeof line, "--- %04u-%02u-%02u %02u:%02u ampify update --auto\r\n", now.wYear,
                                   now.wMonth, now.wDay, now.wHour, now.wMinute);
        DWORD written = 0;
        if (length > 0) WriteFile(log, line, static_cast<DWORD>(length), &written, nullptr);
        return log;
    }

    void Start(ampify::Journal& journal) {
        std::filesystem::path folder = ampify::win::ExecutablePath().parent_path();
        std::error_code ec;
        if (!std::filesystem::equivalent(folder, ampify::win::InstallDirectory(), ec)) return;  // a build folder
        std::filesystem::path cli = folder / ampify::kCliFile;
        if (!std::filesystem::exists(cli)) return;
        HANDLE log = OpenLog();
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        if (log != INVALID_HANDLE_VALUE) {
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = log;
            startup.hStdError = log;
        }
        std::wstring command = L"\"" + cli.wstring() + L"\" update --auto";
        PROCESS_INFORMATION started{};
        // A console of its own without a window: curl and tasklist, which ampify runs, inherit it and stay hidden.
        if (CreateProcessW(cli.c_str(), command.data(), nullptr, nullptr, log != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW,
                           nullptr, cli.parent_path().c_str(), &startup, &started)) {
            CloseHandle(started.hThread);
            m_process = started.hProcess;
        } else {
            journal.Write("ampify update --auto could not start (code " + std::to_string(GetLastError()) + ")");
        }
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    }

    ULONGLONG m_next = GetTickCount64() + kFirstDelayMs;
    HANDLE m_process = nullptr;
};

// ampify_launcher.exe --background: waits for Apple Music, follows each session, and waits again. One per Windows
// session; it stops when `stop` is set (ampify restore, the installer, the uninstaller).
int RunBackground(ampify::Journal& journal, const std::filesystem::path& kit, HANDLE stop) {
    journal.Write("Background launcher started: Ampify loads with Apple Music each time it starts");

    // The package and its player DLL are read while Apple Music is closed, so a start is held as soon as it is seen.
    // Read again after an Apple Music update, and after each session. Missing, it is looked for again quietly.
    std::optional<Session> session = OpenSession(journal, kit);
    std::ostringstream quiet;
    ampify::Journal quietJournal(quiet);
    bool firstLook = true;
    AutoUpdater updater;
    ampify::win::ProcessWatch appleMusic(ampify::kAppleMusicImageW);
    // The setting is read at each session, so it applies the next time Apple Music starts.
    OrphanAgentWatch orphanAgent(OrphanAgentSettingOn(kit));
    ULONGLONG lastOrphanLook = 0;
    DWORD wait = 0;
    while (!WaitOrStop(stop, wait)) {
        wait = kBackgroundPollMs;
        // A name check first: it costs next to nothing, four times a second.
        if (!appleMusic.Running()) {
            firstLook = false;
            ULONGLONG now = GetTickCount64();
            if (session && now - lastOrphanLook >= kOrphanLookMs) {
                lastOrphanLook = now;
                orphanAgent.Step(journal, session->package.directory, false);
            }
            updater.Step(journal);
            continue;
        }
        if (!session || session->Stale()) {
            session = OpenSession(session ? journal : quietJournal, kit);
            quiet.str({});
        }
        std::vector<DWORD> running;
        if (session) running = ampify::launcher::FindPackageProcesses(ampify::kAppleMusicImageW, session->package.directory);
        if (running.empty()) {
            wait = kBackgroundRetryMs;  // an AppleMusic.exe from somewhere else, or Apple Music not installed
            continue;
        }
        ampify::SafeMode safeMode((kit / "state.txt").string());
        if (firstLook) {
            FollowOpenApp(journal, *session, safeMode, running.front(), stop);
        } else {
            journal.Write("Apple Music started (process " + std::to_string(running.front()) + ")");
            FollowNewApp(journal, *session, safeMode, running.front(), stop);
        }
        firstLook = false;
        if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) break;
        updater.SoonAfterSession();
        journal.Write("Waiting for Apple Music to start again");
        session = OpenSession(journal, kit);
        orphanAgent.SetEnabled(OrphanAgentSettingOn(kit));
    }
    journal.Write("Background launcher stopped");
    return 0;
}

// Resumes the thread Windows held, on every path out of RunAttach: Apple Music must never stay frozen.
class HeldThread {
public:
    explicit HeldThread(DWORD threadId) : m_thread(OpenThread(THREAD_SUSPEND_RESUME, FALSE, threadId)) {}
    ~HeldThread() { Resume(); }
    HeldThread(const HeldThread&) = delete;
    HeldThread& operator=(const HeldThread&) = delete;

    bool Opened() const { return m_thread != nullptr; }
    void Resume() {
        if (!m_thread) return;
        ResumeThread(m_thread);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }

private:
    HANDLE m_thread;
};

std::wstring ImageName(DWORD processId) {
    wchar_t path[MAX_PATH] = L"";
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)) {
        DWORD length = MAX_PATH;
        if (!QueryFullProcessImageNameW(process, 0, path, &length)) path[0] = L'\0';
        CloseHandle(process);
    }
    return std::filesystem::path(path).filename().wstring();
}

// Windows holds the process suspended until this thread is resumed, so the kernel loads before any of Apple Music's
// code. Only the opening of the app counts for safe mode, not its library agent nor the short AppleMusic.exe that a
// music:// link starts while the app is open.
int RunAttach(ampify::Journal& journal, const std::filesystem::path& kit, DWORD processId, DWORD threadId) {
    HeldThread held(threadId);
    std::string image = ToNarrow(ImageName(processId));
    std::string label = (image.empty() ? std::string("process") : image) + " (" + std::to_string(processId) + ")";
    if (!held.Opened()) {
        journal.Write(label + ": its thread could not be opened (Windows error " + std::to_string(GetLastError()) + ")");
        return 1;
    }
    // Windows also hands over the package's other processes (its RuntimeBroker.exe): the kernel has nothing to do
    // there, so it is not loaded at all. A name Windows would not give proves nothing: such a process is not skipped.
    if (!image.empty() && ampify::RoleOfImageName(image) == ampify::ProcessRole::Unknown) {
        journal.Write(label + " started: not Apple Music's interface nor its library agent, left alone");
        return 0;  // the held thread resumes as `held` goes
    }
    std::optional<Session> session = OpenSession(journal, kit);
    if (!session) return 1;

    bool isApp = ampify::RoleOfImageName(image) == ampify::ProcessRole::AppleMusic;
    bool appOpen = isApp &&
                   ampify::launcher::FindPackageProcesses(ampify::kAppleMusicImageW, session->package.directory).size() > 1;
    bool counted = isApp && !appOpen;
    ampify::SafeMode safeMode((kit / "state.txt").string());
    // Not counted (the library agent, a second AppleMusic.exe): safe when the app's own launch was, whose count is
    // already cleared for the next one.
    ampify::PolicyVerdict verdict = session->Decide(counted ? safeMode.BeginLaunch()
                                                            : safeMode.IsActive() || safeMode.ThisLaunchSafe());
    journal.Write(label + " started, held by Windows: " + verdict.reason);
    TellIfOff(journal, *session, verdict);
    if (!verdict.allowed || !session->kernelPresent) {
        if (counted) safeMode.MarkHealthy();  // no kernel to report health, and nothing of Ampify can crash it
        return 0;
    }

    HANDLE process = ampify::launcher::OpenProcessForInjection(processId);
    if (!process) {
        journal.Write(label + ": could not open (Windows error " + std::to_string(GetLastError()) + ")");
        if (counted) safeMode.MarkHealthy();
        return 1;
    }
    // The library agent runs first and gets the kernel a few seconds later: loaded while the agent opens the library
    // (before it ran, or right after), the kernel left the library closed and nothing played, 13 starts out of 14 on
    // 05/11/2026; none out of 5 with the wait. The app itself gets the kernel before it runs, for its first frame.
    bool early = isApp;
    if (!early) {
        held.Resume();
        Sleep(kAgentSettleMs);
    }
    std::string failure;
    HANDLE pending = nullptr;
    bool loaded = ampify::launcher::InjectKernel(process, session->kernelDll, failure, kHeldLoadTimeoutMs, &pending);
    std::string when = early ? "before the process ran" : "once the agent had settled";
    journal.Write(loaded ? label + ": kernel loaded " + when
                         : pending ? label + ": load still pending, resuming the process" : label + ": failed, " + failure);
    held.Resume();
    if (pending) {
        failure.clear();
        loaded = ampify::launcher::FinishRemoteLoad(pending, ampify::launcher::kDefaultRemoteLoadTimeoutMs, failure);
        journal.Write(loaded ? label + ": kernel loaded after the resume" : label + ": failed, " + failure);
    }
    CloseHandle(process);
    if (!loaded && counted) safeMode.MarkHealthy();
    return loaded ? 0 : 1;
}

// ampify_launcher.exe --register (and --background): has Windows start the launcher with each process of Apple Music,
// checks once for an update of Ampify, and ends. Where Windows refuses the setting, the launcher stays in the
// background instead, as Ampify 1.1 did.
int RunRegister(ampify::Journal& journal, const std::filesystem::path& kit, HANDLE stop) {
    std::wstring fullName = ampify::win::AppleMusicFullName();
    std::filesystem::path self = ampify::win::ExecutablePath();
    if (fullName.empty()) {
        journal.Write("Apple Music for Windows not found: the launcher stays in the background to wait for it");
        return RunBackground(journal, kit, stop);
    }
    HRESULT hr = ampify::win::EnableAttach(fullName, self);
    if (FAILED(hr)) {
        journal.Write("Windows refused to start the launcher with Apple Music (" + HResultText(hr) +
                      "): it stays in the background instead");
        return RunBackground(journal, kit, stop);
    }
    journal.Write("Windows starts the launcher with each process of " + ToNarrow(fullName) +
                  ": nothing stays in the background");
    AutoUpdater updater;
    updater.StartOnce(journal);
    return 0;
}

bool HasArgument(const wchar_t* argument) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; argv && i < argc; i++) found = found || _wcsicmp(argv[i], argument) == 0;
    LocalFree(argv);
    return found;
}

}  // namespace

// logs\attach.log: each process of Apple Music starts a launcher, often two at once, so they append to it; it starts
// again past kAttachLogLimit.
int MainAttach() {
    constexpr std::uintmax_t kAttachLogLimit = 64 * 1024;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    DWORD processId = 0;
    DWORD threadId = 0;
    bool parsed = ampify::win::ParseAttachArguments(argc, argv, processId, threadId);
    LocalFree(argv);
    std::filesystem::path logFile = ampify::win::JournalFile("attach.log");
    std::error_code ec;
    bool restart = std::filesystem::file_size(logFile, ec) > kAttachLogLimit && !ec;
    std::ofstream file(logFile, restart ? std::ios::trunc : std::ios::app);
    ampify::Journal journal(file);
    if (!parsed) {
        journal.Write("--attach without -p <process> -tid <thread>: nothing to do");
        return 1;
    }
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int code = 1;
    try {
        code = RunAttach(journal, ampify::win::KitDirectory(), processId, threadId);
    } catch (const std::exception& error) {
        journal.Write(std::string("Unexpected error: ") + error.what());
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return code;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (HasArgument(ampify::win::kAttachArgument)) return MainAttach();
    bool background = HasArgument(ampify::win::kBackgroundArgument) || HasArgument(ampify::win::kRegisterArgument);
    // One background launcher per session. Checked before the log is opened, which would empty the running one's.
    HANDLE single = background ? CreateMutexW(nullptr, TRUE, ampify::win::kBackgroundMutex) : nullptr;
    if (background && (!single || GetLastError() == ERROR_ALREADY_EXISTS)) {
        if (single) CloseHandle(single);
        return 0;
    }
    // Created right after the mutex, so whoever sees the mutex finds the event. One left by a launcher that is still
    // ending may be set: that stop was not meant for this one.
    HANDLE stop = background ? CreateEventW(nullptr, TRUE, FALSE, ampify::win::kBackgroundStopEvent) : nullptr;
    if (stop && GetLastError() == ERROR_ALREADY_EXISTS) ResetEvent(stop);
    if (background && !stop) {
        CloseHandle(single);
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::ofstream file(ampify::win::JournalFile(background ? "background.log" : "launcher.log"), std::ios::trunc);
    ampify::Journal journal(file);

    int code = 1;
    try {
        code = background ? RunRegister(journal, ampify::win::KitDirectory(), stop)
                          : Run(journal, ampify::win::KitDirectory());
    } catch (const std::exception& error) {
        journal.Write(std::string("Unexpected error: ") + error.what());
        if (!background) Alert(L"The launcher hit an error. See the log in Ampify\\logs.");
    }

    CoUninitialize();
    if (stop) CloseHandle(stop);
    if (single) {
        ReleaseMutex(single);
        CloseHandle(single);
    }
    return code;
}
