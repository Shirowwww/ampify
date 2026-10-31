// ampify_launcher.exe: starts Apple Music with the kernel loaded into its processes. The launcher stays open while
// Apple Music runs, so it can load the kernel into every process of the package, including ones that appear
// later (a hand-over to a new process, or AMPLibraryAgent.exe). The package folder is never written to.

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../core/apple_music.hpp"
#include "../core/build_signature.hpp"
#include "../core/injection_policy.hpp"
#include "../core/journal.hpp"
#include "../core/launch_flags.hpp"
#include "../core/package_watch.hpp"
#include "../core/safe_mode.hpp"
#include "../core/settings.hpp"
#include "../core/version_gate.hpp"
#include "../core/version_list.hpp"
#include "../win/win_common.hpp"
#include "injection.hpp"
#include "../win/package.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr DWORD kPollIntervalMs = 250;

// The first process is held still while the kernel loads, so this wait must stay short: the app would freeze for it.
constexpr DWORD kHeldLoadTimeoutMs = 3000;

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

// Loads the kernel into one process and writes the outcome to the log. Returns true on success.
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
    std::string label = "AppleMusic.exe (" + std::to_string(processId) + ")";
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

// Ids of the running processes with this image name in the package, as the watch counts them.
std::vector<uint32_t> PackageIds(const wchar_t* imageName, const std::filesystem::path& directory) {
    std::vector<uint32_t> ids;
    for (DWORD processId : ampify::launcher::FindPackageProcesses(imageName, directory)) ids.push_back(processId);
    return ids;
}

// Follows the package until Apple Music has been gone for a while. Each process it sees gets the kernel once.
// When `config.reportHealth` is set, the safe-mode count is cleared once the app has been responsive for 20 s.
// The rules live in ampify::PackageWatch; this function only reads the system and carries out the actions.
void Supervise(ampify::Journal& journal, const std::filesystem::path& packageDirectory,
               const std::filesystem::path& kernelDll, ampify::WatchConfig config, std::optional<DWORD> handledProcess,
               ampify::SafeMode* healthCheck) {
    ampify::PackageWatch watch(config);
    if (handledProcess) watch.MarkHandled(*handledProcess);

    Clock::time_point started = Clock::now();
    while (true) {
        ampify::WatchSnapshot snapshot;
        snapshot.seconds = std::chrono::duration<double>(Clock::now() - started).count();
        snapshot.appProcesses = PackageIds(L"AppleMusic.exe", packageDirectory);
        snapshot.agentProcesses = PackageIds(L"AMPLibraryAgent.exe", packageDirectory);
        for (uint32_t processId : snapshot.appProcesses) {
            if (ampify::win::WindowsResponsive(processId)) {
                snapshot.appResponsive = true;
                break;
            }
        }

        ampify::WatchActions actions = watch.Step(snapshot);
        for (uint32_t processId : actions.inject) {
            bool isApp = std::find(snapshot.appProcesses.begin(), snapshot.appProcesses.end(), processId) !=
                         snapshot.appProcesses.end();
            std::string image = isApp ? "AppleMusic.exe" : "AMPLibraryAgent.exe";
            InjectInto(journal, processId, kernelDll, image + " (" + std::to_string(processId) + ")");
        }

        if (actions.markHealthy) {
            healthCheck->MarkHealthy();
            journal.Write("Healthy after 20 s, reported by the launcher: "
                          "safe mode count reset");
        }

        if (actions.stop) {
            journal.Write(actions.stopReason + ": the launcher stops");
            return;
        }
        Sleep(kPollIntervalMs);
    }
}

int Run(ampify::Journal& journal, const std::filesystem::path& kit) {
    std::wstring family = ToWide(ampify::kPackageFamilyName);
    std::optional<ampify::launcher::InstalledPackage> package = ampify::launcher::FindInstalledPackage(family.c_str());
    if (!package) {
        journal.Write("Apple Music for Windows not found");
        Alert(L"Apple Music for Windows is not installed on this PC.");
        return 1;
    }

    std::string fullName = ToNarrow(package->fullName);
    std::string version = ampify::AppleMusicVersion(fullName);
    journal.Write("Package found: " + fullName + " (folder " + package->directory.string() + ")");

    // Read on every launch, never kept from an earlier one: an update can change the player DLL under the same name.
    ampify::BuildSignature signature = ampify::CheckBuildSignature(
        ampify::win::ReadFileBytes(package->directory / std::string(ampify::kPlayerDllName)));
    journal.Write(ampify::DescribeSignature(signature));

    std::filesystem::path kernelDll = ampify::win::ExecutablePath().parent_path() / L"ampify_kernel.dll";
    bool kernelPresent = std::filesystem::exists(kernelDll);
    if (!kernelPresent) journal.Write("ampify_kernel.dll not found next to the launcher");

    ampify::SafeMode safeMode((kit / "state.txt").string());
    // Builds tested after this build of Ampify was compiled, from `ampify versions update`. The signature is still checked.
    std::vector<std::string> extraVersions = ampify::LoadVersionList(kit / std::string(ampify::kVersionListFile));
    std::vector<std::string_view> knownVersions = ampify::KnownAppVersionsWith(extraVersions);
    std::vector<DWORD> running = ampify::launcher::FindPackageProcesses(L"AppleMusic.exe", package->directory);

    if (!running.empty()) {
        // Apple Music is already open: nothing is started, so this is not a launch to count.
        DWORD processId = running.front();
        ampify::PolicyVerdict verdict = ampify::DecideInjection(version, safeMode.IsActive(), signature, knownVersions);
        bool inject = verdict.allowed && kernelPresent;
        journal.Write("Apple Music already open (process " + std::to_string(processId) + "): " + verdict.reason);

        bool injected = inject && InjectInto(journal, processId, kernelDll,
                                             "AppleMusic.exe (" + std::to_string(processId) + ")");
        Supervise(journal, package->directory, kernelDll, MakeWatchConfig(inject, !injected), processId, &safeMode);
        return 0;
    }

    bool safe = safeMode.BeginLaunch();
    ampify::PolicyVerdict verdict = ampify::DecideInjection(version, safe, signature, knownVersions);
    journal.Write(verdict.reason);
    bool inject = verdict.allowed && kernelPresent;

    std::wstring appId = ToWide(ampify::kAppUserModelId);
    DWORD processId = 0;
    // app_launch_flags: extra arguments for Apple Music. An invalid value is written to the log and not passed.
    ampify::Settings settings;
    settings.Load((kit / "settings.txt").string());
    std::string flagsError;
    std::string flags = ampify::AppLaunchArguments(settings.GetString(std::string(ampify::kLaunchFlagsKey), ""), &flagsError);
    if (!flagsError.empty()) journal.Write(flagsError + ": no argument passed to Apple Music");
    if (!flags.empty()) journal.Write("Arguments passed to Apple Music: " + flags);
    HRESULT activationHr = ampify::launcher::ActivateApp(appId.c_str(), &processId, ToWide(flags));
    if (FAILED(activationHr)) {
        journal.Write("Activation refused (" + HResultText(activationHr) + ")");
        Alert(L"Apple Music could not start.");
        return 1;
    }
    journal.Write("Apple Music started (process " + std::to_string(processId) + ")");

    // Without a kernel in the app, nothing reports health. The launcher then does it, so a launch that does not
    // inject (safe mode, unknown build, failed load) still clears the count and cannot lock the mods out for good.
    bool injected = inject && InjectWhileHeld(journal, processId, kernelDll);
    Supervise(journal, package->directory, kernelDll, MakeWatchConfig(inject, !injected), processId, &safeMode);
    return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::ofstream file(ampify::win::JournalFile("launcher.log"), std::ios::trunc);
    ampify::Journal journal(file, ampify::Journal::LocalTimeStamp);

    int code = 1;
    try {
        code = Run(journal, ampify::win::KitDirectory());
    } catch (const std::exception& error) {
        journal.Write(std::string("Unexpected error: ") + error.what());
        Alert(L"The launcher hit an error. See the log in Ampify\\logs.");
    }

    CoUninitialize();
    return code;
}
