// amk_launcher.exe: starts Apple Music with the kernel loaded into AppleMusic.exe, and into
// AMPLibraryAgent.exe as soon as that process appears. The package folder is never written to.

#include <windows.h>
#include <objbase.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../core/apple_music.hpp"
#include "../core/injection_policy.hpp"
#include "../core/journal.hpp"
#include "../core/safe_mode.hpp"
#include "../win/win_common.hpp"
#include "injection.hpp"
#include "package.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kHealthyAfter = std::chrono::seconds(20);
constexpr DWORD kPollIntervalMs = 250;

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

// Keeps the launcher running while Apple Music runs. It injects into AMPLibraryAgent.exe as each one appears.
// When `healthCheck` is set (a launch that has no kernel to report health), the safe-mode count is cleared once
// the app has stayed responsive for 20 seconds.
void Supervise(amk::Journal& journal, HANDLE app, DWORD appProcessId, const std::filesystem::path& packageDirectory,
               const std::filesystem::path& kernelDll, bool injectLibraryAgent, amk::SafeMode* healthCheck) {
    Clock::time_point started = Clock::now();
    bool healthChecked = healthCheck == nullptr;
    std::set<DWORD> seen;

    while (WaitForSingleObject(app, 0) == WAIT_TIMEOUT) {
        if (injectLibraryAgent) {
            for (DWORD processId : amk::launcher::FindPackageProcesses(L"AMPLibraryAgent.exe", packageDirectory)) {
                if (seen.insert(processId).second) {
                    InjectInto(journal, processId, kernelDll, "AMPLibraryAgent.exe (" + std::to_string(processId) + ")");
                }
            }
        }

        if (!healthChecked && Clock::now() - started >= kHealthyAfter &&
            amk::win::WindowsResponsive(appProcessId)) {
            healthCheck->MarkHealthy();
            healthChecked = true;
            journal.Write("Application saine sans noyau après 20 s : compteur du mode sans échec remis à zéro");
        }

        Sleep(kPollIntervalMs);
    }
    journal.Write("Apple Music fermé : le lanceur s'arrête");
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

    std::filesystem::path kernelDll = amk::win::ExecutablePath().parent_path() / L"amk_kernel.dll";
    bool kernelPresent = std::filesystem::exists(kernelDll);
    if (!kernelPresent) journal.Write("amk_kernel.dll introuvable à côté du lanceur");

    amk::SafeMode safeMode((kit / "state.txt").string());
    std::vector<DWORD> running = amk::launcher::FindPackageProcesses(L"AppleMusic.exe", package->directory);

    if (!running.empty()) {
        // Apple Music is already open: nothing to start, so this is not a launch to count.
        DWORD processId = running.front();
        amk::PolicyVerdict verdict = amk::DecideInjection(version, safeMode.IsActive());
        bool inject = verdict.allowed && kernelPresent;
        journal.Write("Apple Music déjà ouvert (processus " + std::to_string(processId) + ") : " + verdict.reason);

        HANDLE app = amk::launcher::OpenProcessForInjection(processId);
        if (!app) {
            journal.Write("Ouverture du processus impossible : le lanceur s'arrête");
            return 1;
        }
        if (inject) InjectInto(journal, processId, kernelDll, "AppleMusic.exe");
        Supervise(journal, app, processId, package->directory, kernelDll, inject, nullptr);
        CloseHandle(app);
        return 0;
    }

    bool safe = safeMode.BeginLaunch();
    amk::PolicyVerdict verdict = amk::DecideInjection(version, safe);
    journal.Write(verdict.reason);
    bool inject = verdict.allowed && kernelPresent;

    // Suspended start first, so the kernel is in place before any Apple Music code runs.
    bool suspended = false;
    if (inject) {
        HRESULT debugHr = amk::launcher::EnablePackageDebugging(package->fullName);
        suspended = SUCCEEDED(debugHr);
        journal.Write(suspended
                          ? std::string("Lancement suspendu demandé à Windows")
                          : "Lancement suspendu indisponible (" + HResultText(debugHr) +
                                ") : injection après le démarrage");
    }

    std::wstring appId = ToWide(amk::kAppUserModelId);
    DWORD processId = 0;
    HRESULT activationHr = amk::launcher::ActivateApp(appId.c_str(), &processId);
    if (FAILED(activationHr)) {
        if (suspended) amk::launcher::DisablePackageDebugging(package->fullName);
        journal.Write("Activation refusée (" + HResultText(activationHr) + ")");
        Alert(L"Apple Music n'a pas pu démarrer.");
        return 1;
    }
    journal.Write("Apple Music démarré (processus " + std::to_string(processId) + ")");

    HANDLE app = amk::launcher::OpenProcessForInjection(processId);
    bool injected = inject && app && InjectInto(journal, processId, kernelDll, "AppleMusic.exe");

    // Always resume, kernel or not: the app must never be left frozen.
    if (suspended) {
        int resumed = amk::launcher::ResumeProcessThreads(processId);
        amk::launcher::DisablePackageDebugging(package->fullName);
        journal.Write(std::to_string(resumed) + " thread(s) repris après l'injection");
    }

    if (!app) {
        journal.Write("Ouverture du processus impossible : surveillance arrêtée");
        return 1;
    }

    // Without a kernel in the app, nothing reports health. The launcher then does it for a safe launch,
    // and also for a plain one, so a failed injection cannot lock the mods out for good.
    Supervise(journal, app, processId, package->directory, kernelDll, injected, injected ? nullptr : &safeMode);
    CloseHandle(app);
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
