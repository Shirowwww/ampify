// amk_launcher.exe: starts Apple Music with the kernel loaded into its processes. The launcher stays open while
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
#include "../core/package_watch.hpp"
#include "../core/safe_mode.hpp"
#include "../win/win_common.hpp"
#include "injection.hpp"
#include "package.hpp"

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
    MessageBoxW(nullptr, text, L"Apple Music Mod Kit", MB_ICONERROR | MB_OK);
}

// Loads the kernel into one process and writes the outcome to the journal. Returns true on success.
bool InjectInto(amk::Journal& journal, DWORD processId, const std::filesystem::path& kernelDll,
                const std::string& label) {
    HANDLE process = amk::launcher::OpenProcessForInjection(processId);
    if (!process) {
        journal.Write(label + " : ouverture impossible (erreur Windows " + std::to_string(GetLastError()) + ")");
        return false;
    }

    std::string failure;
    bool loaded = amk::launcher::InjectKernel(process, kernelDll, failure);
    CloseHandle(process);

    journal.Write(loaded ? label + " : noyau chargé" : label + " : échec, " + failure);
    return loaded;
}

// Holds the first Apple Music process still while the kernel loads, then resumes it, whatever the outcome.
// Windows offers no way to start the app suspended from here (a package debug setting alone does not suspend
// it), so the hold begins when ActivateApp returns. The app may already have run for a moment by then: the
// journal says how many threads were held and whether the kernel loaded during the hold.
bool InjectWhileHeld(amk::Journal& journal, DWORD processId, const std::filesystem::path& kernelDll) {
    std::string label = "AppleMusic.exe (" + std::to_string(processId) + ")";
    HANDLE process = amk::launcher::OpenProcessForInjection(processId);
    if (!process) {
        journal.Write(label + " : ouverture impossible, pas de pause (erreur Windows " +
                      std::to_string(GetLastError()) + ")");
        return false;
    }

    int held = amk::launcher::SuspendProcessThreads(processId);
    journal.Write(std::to_string(held) + " thread(s) mis en pause");

    std::string failure;
    bool loaded = amk::launcher::InjectKernel(process, kernelDll, failure, kHeldLoadTimeoutMs);
    journal.Write(loaded ? label + " : noyau chargé pendant la pause" : label + " : échec, " + failure);

    // Resumed on every path, so the app is never left frozen, even when the load failed.
    int resumed = amk::launcher::ResumeProcessThreads(processId);
    journal.Write(std::to_string(resumed) + " thread(s) repris après l'injection");

    CloseHandle(process);
    return loaded;
}

amk::WatchConfig MakeWatchConfig(bool inject, bool reportHealth) {
    amk::WatchConfig config;
    config.inject = inject;
    config.reportHealth = reportHealth;
    return config;
}

// Ids of the running processes with this image name in the package, as the watch counts them.
std::vector<uint32_t> PackageIds(const wchar_t* imageName, const std::filesystem::path& directory) {
    std::vector<uint32_t> ids;
    for (DWORD processId : amk::launcher::FindPackageProcesses(imageName, directory)) ids.push_back(processId);
    return ids;
}

// Follows the package until Apple Music has been gone for a while. Each process it sees gets the kernel once.
// When `config.reportHealth` is set, the safe-mode count is cleared once the app has been responsive for 20 s.
// The rules live in amk::PackageWatch; this function only reads the system and carries out the actions.
void Supervise(amk::Journal& journal, const std::filesystem::path& packageDirectory,
               const std::filesystem::path& kernelDll, amk::WatchConfig config, std::optional<DWORD> handledProcess,
               amk::SafeMode* healthCheck) {
    amk::PackageWatch watch(config);
    if (handledProcess) watch.MarkHandled(*handledProcess);

    Clock::time_point started = Clock::now();
    while (true) {
        amk::WatchSnapshot snapshot;
        snapshot.seconds = std::chrono::duration<double>(Clock::now() - started).count();
        snapshot.appProcesses = PackageIds(L"AppleMusic.exe", packageDirectory);
        snapshot.agentProcesses = PackageIds(L"AMPLibraryAgent.exe", packageDirectory);
        for (uint32_t processId : snapshot.appProcesses) {
            if (amk::win::WindowsResponsive(processId)) {
                snapshot.appResponsive = true;
                break;
            }
        }

        amk::WatchActions actions = watch.Step(snapshot);
        for (uint32_t processId : actions.inject) {
            bool isApp = std::find(snapshot.appProcesses.begin(), snapshot.appProcesses.end(), processId) !=
                         snapshot.appProcesses.end();
            std::string image = isApp ? "AppleMusic.exe" : "AMPLibraryAgent.exe";
            InjectInto(journal, processId, kernelDll, image + " (" + std::to_string(processId) + ")");
        }

        if (actions.markHealthy) {
            healthCheck->MarkHealthy();
            journal.Write("Application saine après 20 s, rapportée par le lanceur : "
                          "compteur du mode sans échec remis à zéro");
        }

        if (actions.stop) {
            journal.Write(actions.stopReason + " : le lanceur s'arrête");
            return;
        }
        Sleep(kPollIntervalMs);
    }
}

int Run(amk::Journal& journal, const std::filesystem::path& kit) {
    std::wstring family = ToWide(amk::kPackageFamilyName);
    std::optional<amk::launcher::InstalledPackage> package = amk::launcher::FindInstalledPackage(family.c_str());
    if (!package) {
        journal.Write("Apple Music pour Windows introuvable");
        Alert(L"Apple Music pour Windows n'est pas installé sur ce PC.");
        return 1;
    }

    std::string fullName = ToNarrow(package->fullName);
    std::string version = amk::AppleMusicVersion(fullName);
    journal.Write("Paquet trouvé : " + fullName + " (dossier " + package->directory.string() + ")");

    // Read on every launch, never kept from an earlier one: an update can change the player DLL under the same name.
    amk::BuildSignature signature = amk::CheckBuildSignature(
        amk::win::ReadFileBytes(package->directory / std::string(amk::kPlayerDllName)));
    journal.Write(amk::DescribeSignature(signature));

    std::filesystem::path kernelDll = amk::win::ExecutablePath().parent_path() / L"amk_kernel.dll";
    bool kernelPresent = std::filesystem::exists(kernelDll);
    if (!kernelPresent) journal.Write("amk_kernel.dll introuvable à côté du lanceur");

    amk::SafeMode safeMode((kit / "state.txt").string());
    std::vector<DWORD> running = amk::launcher::FindPackageProcesses(L"AppleMusic.exe", package->directory);

    if (!running.empty()) {
        // Apple Music is already open: nothing is started, so this is not a launch to count.
        DWORD processId = running.front();
        amk::PolicyVerdict verdict = amk::DecideInjection(version, safeMode.IsActive(), signature);
        bool inject = verdict.allowed && kernelPresent;
        journal.Write("Apple Music déjà ouvert (processus " + std::to_string(processId) + ") : " + verdict.reason);

        bool injected = inject && InjectInto(journal, processId, kernelDll,
                                             "AppleMusic.exe (" + std::to_string(processId) + ")");
        Supervise(journal, package->directory, kernelDll, MakeWatchConfig(inject, !injected), processId, &safeMode);
        return 0;
    }

    bool safe = safeMode.BeginLaunch();
    amk::PolicyVerdict verdict = amk::DecideInjection(version, safe, signature);
    journal.Write(verdict.reason);
    bool inject = verdict.allowed && kernelPresent;

    std::wstring appId = ToWide(amk::kAppUserModelId);
    DWORD processId = 0;
    HRESULT activationHr = amk::launcher::ActivateApp(appId.c_str(), &processId);
    if (FAILED(activationHr)) {
        journal.Write("Activation refusée (" + HResultText(activationHr) + ")");
        Alert(L"Apple Music n'a pas pu démarrer.");
        return 1;
    }
    journal.Write("Apple Music démarré (processus " + std::to_string(processId) + ")");

    // Without a kernel in the app, nothing reports health. The launcher then does it, so a launch that does not
    // inject (safe mode, unknown build, failed load) still clears the count and cannot lock the mods out for good.
    bool injected = inject && InjectWhileHeld(journal, processId, kernelDll);
    Supervise(journal, package->directory, kernelDll, MakeWatchConfig(inject, !injected), processId, &safeMode);
    return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::ofstream file(amk::win::JournalFile("launcher.log"), std::ios::trunc);
    amk::Journal journal(file, amk::Journal::LocalTimeStamp);

    int code = 1;
    try {
        code = Run(journal, amk::win::KitDirectory());
    } catch (const std::exception& error) {
        journal.Write(std::string("Erreur inattendue : ") + error.what());
        Alert(L"Le lanceur a rencontré une erreur. Voir le journal dans AppleMusicModKit\\journal.");
    }

    CoUninitialize();
    return code;
}
