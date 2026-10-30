// The kernel: a DLL that amk_launcher.exe loads into AppleMusic.exe and AMPLibraryAgent.exe.
// DllMain only starts a thread. The work happens on that thread, outside the loader lock.

#include <windows.h>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#include "../core/apple_music.hpp"
#include "../core/extension.hpp"
#include "../core/injection_policy.hpp"
#include "../core/journal.hpp"
#include "../core/safe_mode.hpp"
#include "../core/settings.hpp"
#include "../win/win_common.hpp"

namespace {

constexpr auto kHealthyAfter = std::chrono::seconds(20);
constexpr auto kHealthyRetry = std::chrono::seconds(5);

// Runs once per process. Journal, extension host and safe mode state stay alive until the process exits,
// so nothing is torn down while the app still runs.
DWORD WINAPI KernelMain(LPVOID) {
    amk::ProcessRole role = amk::RoleOfImageName(amk::win::ExecutablePath().string());
    if (role == amk::ProcessRole::Unknown) return 0;

    static std::ofstream file(amk::win::JournalFile(role == amk::ProcessRole::AppleMusic ? "AppleMusic.log"
                                                                                        : "AMPLibraryAgent.log"),
                              std::ios::trunc);
    static amk::Journal journal(file, amk::Journal::LocalTimeStamp);

    journal.Write("Noyau chargé dans " + amk::win::ExecutablePath().filename().string() + " (processus " +
                  std::to_string(GetCurrentProcessId()) + ")");

    std::string version = amk::AppleMusicVersion(amk::win::PackageFullName());
    amk::PolicyVerdict verdict = amk::DecideKernel(role, version);
    journal.Write(verdict.reason);
    if (!verdict.allowed) return 0;

    std::filesystem::path kit = amk::win::KitDirectory();
    amk::Settings settings;
    settings.Load((kit / "settings.txt").string());

    static amk::ExtensionHost host(journal);  // no extension is registered yet: M4 adds the Atmos leveler
    host.StartAll(settings);
    journal.Write("Hôte d'extensions prêt");

    if (role == amk::ProcessRole::AppleMusic) {
        // The interface process is the one that proves the app is healthy. Its window must not be hung.
        std::this_thread::sleep_for(kHealthyAfter);
        bool warned = false;
        while (!amk::win::WindowsResponsive(GetCurrentProcessId())) {
            if (!warned) journal.Write("Fenêtre bloquée après 20 s : nouvel essai toutes les 5 s");
            warned = true;
            std::this_thread::sleep_for(kHealthyRetry);
        }

        amk::SafeMode safeMode((kit / "state.txt").string());
        safeMode.MarkHealthy();
        journal.Write("Application saine après 20 s : compteur du mode sans échec remis à zéro");
    }
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, KernelMain, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
