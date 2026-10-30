// The kernel: a DLL that amk_launcher.exe loads into AppleMusic.exe and AMPLibraryAgent.exe.
// DllMain only starts a thread. The work happens on that thread, outside the loader lock.

#include <windows.h>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#include "../core/apple_music.hpp"
#include "../core/extension.hpp"
#include "../core/extension_loader.hpp"
#include "../core/injection_policy.hpp"
#include "../core/journal.hpp"
#include "../core/safe_mode.hpp"
#include "../core/settings.hpp"
#include "../extensions/atmos-leveler/atmos_windows.hpp"
#include "../win/win_common.hpp"
#include "../win/win_host.hpp"

#if defined(AMK_WITH_XAML)
#include "../win/theme_service.hpp"
#endif

namespace {

constexpr auto kHealthyAfter = std::chrono::seconds(20);
constexpr auto kHealthyRetry = std::chrono::seconds(5);

HINSTANCE g_module = nullptr;  // the kernel DLL itself, needed to install the interface hook

// Themes only touch the interface, so only AppleMusic.exe starts the service.
void StartThemes(amk::Journal& journal, [[maybe_unused]] const std::filesystem::path& kit) {
#if defined(AMK_WITH_XAML)
    static amk::win::ThemeService themes(journal, kit);
    themes.Start(g_module, GetCurrentProcessId());
#else
    journal.Write("Thèmes : ce build n'a pas la liaison XAML (AMK_XAML), aucun thème n'est appliqué");
#endif
}

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
    static amk::Settings settings;
    settings.Load((kit / "settings.txt").string());

    // JavaScript elements (extensions and apps) live in %USERPROFILE%\AppleMusicModKit\extensions\<id>.
    // Both processes load them: an element tells them apart with AMK.process.
    static amk::WinHost winHost(role == amk::ProcessRole::AppleMusic ? "ui" : "engine", kit, settings, journal);
    static amk::ExtensionHost host(journal);
    static amk::ExtensionLoader loader(host, winHost, journal, kit / "extensions");
    amk::LoadReport report = loader.Discover();
    journal.Write("Éléments JavaScript retenus : " + std::to_string(report.loaded.size()) + ", rejetés : " +
                  std::to_string(report.rejected.size()));

    // The Atmos leveler lives where the audio is rendered: AMPLibraryAgent.exe only.
    if (role == amk::ProcessRole::LibraryAgent) {
        host.Add(atmos::hooks::MakeAtmosLevelerExtension(journal, settings));
    }

    // JavaScript runs on this one thread only: QuickJS is not thread-safe, and the shortcuts are delivered to the
    // thread that registered them. Starting the elements here keeps that true.
    std::thread([] {
        host.StartAll(settings);
        journal.Write("Hôte d'extensions prêt");
        while (true) {
            winHost.Service([](const std::string& elementId, const std::string& combo) {
                loader.DeliverKey(elementId, combo);
            });
            loader.Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }).detach();

    if (role == amk::ProcessRole::AppleMusic) StartThemes(journal, kit);

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
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, KernelMain, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
