// The kernel: a DLL that amk_launcher.exe loads into AppleMusic.exe and AMPLibraryAgent.exe.
// DllMain only starts a thread. The work happens on that thread, outside the loader lock.

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../core/apple_music.hpp"
#include "../core/build_signature.hpp"
#include "../core/extension.hpp"
#include "../core/extension_loader.hpp"
#include "../core/injection_policy.hpp"
#include "../core/player_watch.hpp"
#include "../core/journal.hpp"
#include "../core/reload.hpp"
#include "../core/safe_mode.hpp"
#include "../core/settings.hpp"
#include "../core/ui_model.hpp"
#include "../core/version_gate.hpp"
#include "../core/version_list.hpp"
#include "../extensions/atmos-leveler/atmos_windows.hpp"
#include "../win/win_common.hpp"
#include "../win/win_host.hpp"

#if defined(AMK_WITH_XAML)
#include "../core/json.hpp"
#include "../win/theme_service.hpp"
#include "../win/ui_service.hpp"
#include "../win/xaml_host.hpp"
#endif

namespace {

constexpr auto kHealthyAfter = std::chrono::seconds(20);
constexpr auto kHealthyRetry = std::chrono::seconds(5);
constexpr auto kReloadEvery = std::chrono::milliseconds(500);

// Reads reload.txt and runs its new requests: start, stop, restart, rescan (core/reload.hpp). The settings are read
// again first, so an element that `amk` has just enabled starts with the value on disk. A missing file counts as
// empty: the first text is only the baseline, so a file that is created later is not missed.
void ServiceReloadRequests(const std::filesystem::path& kit, amk::ReloadWatcher& watcher, amk::ExtensionLoader& loader,
                           amk::Settings& settings) {
    std::string text;
    std::ifstream in(kit / "reload.txt", std::ios::binary);
    if (in) {
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }

    std::vector<amk::ReloadRequest> requests = watcher.Requests(text);
    if (requests.empty()) return;

    settings.Load((kit / "settings.txt").string());
    loader.Apply(requests, settings);
}

HINSTANCE g_module = nullptr;  // the kernel DLL itself, passed to the interface-thread facade

// Themes and the native interface only touch the interface, so only AppleMusic.exe starts them. Both reach the
// interface thread through the DispatcherQueue of Apple Music's main window (win/xaml_host.hpp). The clicks and
// changes of the rendered trees go back to the JavaScript thread through the elements' queues.
void StartInterface(amk::Journal& journal, [[maybe_unused]] const std::filesystem::path& kit,
                    [[maybe_unused]] amk::UiModel& model, [[maybe_unused]] amk::ExtensionLoader& loader) {
#if defined(AMK_WITH_XAML)
    amk::win::InstallXamlUiThread(journal);
    // Never destroyed, like the interface service below: its theme session holds XAML objects (resource dictionary,
    // brushes, templates), and a static destructor would release them while Apple Music exits, after XAML is gone.
    static auto* themes = new amk::win::ThemeService(journal, kit);
    themes->Start(g_module, GetCurrentProcessId());

    // Never destroyed: it holds XAML objects that may only be released on the interface thread.
    static auto* ui = new amk::win::UiService(journal, kit, model, [target = &loader](const amk::win::UiEvent& event) {
        if (event.click) {
            target->DeliverClickTo(event.elementId, event.callbackId);
            return;
        }
        amk::Json payload(amk::Json::Object{});
        payload.Set("id", amk::Json(static_cast<double>(event.callbackId)));
        std::optional<amk::Json> value = amk::Json::Parse(event.valueJson);
        payload.Set("value", value ? *value : amk::Json());
        target->DeliverEventTo(event.elementId, "change", payload.Dump());
    });
    ui->Start();
#else
    journal.Write("Thèmes et interface native : ce build n'a pas la liaison XAML (AMK_XAML), rien n'est affiché");
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

    // Read on every start: the package may have been updated since the launcher last looked.
    std::string version = amk::AppleMusicVersion(amk::win::PackageFullName());
    amk::BuildSignature signature = amk::CheckBuildSignature(
        amk::win::ReadFileBytes(amk::win::ExecutablePath().parent_path() / std::string(amk::kPlayerDllName)));
    journal.Write(amk::DescribeSignature(signature));
    // Builds tested after this kit was compiled, from `amk versions update` (<kit>\versions.txt). The signature
    // check above still applies, and an unknown build still leaves the kernel idle.
    std::vector<std::string> extraVersions =
        amk::LoadVersionList(amk::win::KitDirectory() / std::string(amk::kVersionListFile));
    amk::PolicyVerdict verdict = amk::DecideKernel(role, version, signature, amk::KnownAppVersionsWith(extraVersions));
    journal.Write(verdict.reason);
    if (!verdict.allowed) return 0;

    std::filesystem::path kit = amk::win::KitDirectory();
    static amk::Settings settings;
    settings.Load((kit / "settings.txt").string());

    // JavaScript elements live in %USERPROFILE%\AppleMusicModKit\extensions\<id> and apps\<id>. Both processes
    // load them: an element tells them apart with AMK.process.
    static amk::WinHost winHost(role == amk::ProcessRole::AppleMusic ? "ui" : "engine", kit, settings, journal);
    static amk::ExtensionHost host(journal);
    static amk::ExtensionLoader loader(host, winHost, journal, kit / "extensions");
    loader.SetAppRoot(kit / "apps");
    loader.SetThemeRoot(kit / "themes");
    winHost.SetEventSink([](const std::string& entry, const std::string& payload) { loader.DeliverEvent(entry, payload); });
    amk::LoadReport report = loader.Discover();
    journal.Write("Éléments JavaScript retenus : " + std::to_string(report.loaded.size()) + ", rejetés : " +
                  std::to_string(report.rejected.size()));

    // The Atmos leveler lives where the audio is rendered: AMPLibraryAgent.exe only.
    if (role == amk::ProcessRole::LibraryAgent) {
        host.Add(atmos::hooks::MakeAtmosLevelerExtension(journal, settings));
    }

    // JavaScript runs on this one thread only: QuickJS is not thread-safe, and the shortcuts are delivered to the
    // thread that registered them. Starting the elements here keeps that true.
    std::thread([kit] {
        host.StartAll(settings);
        journal.Write("Hôte d'extensions prêt");
        amk::PlayerWatcher playerWatcher;
        int ticks = 0;
        amk::ReloadWatcher reload;
        auto nextReload = std::chrono::steady_clock::now();
        while (true) {
            winHost.Service([](const std::string& elementId, const std::string& combo) {
                loader.DeliverKey(elementId, combo);
            });
            loader.Pump();
            // The player is looked at every 250 ms, and its changes go to the elements that subscribed to them.
            if (++ticks % 5 == 0) {
                amk::PlayerSnapshot snapshot = winHost.Player();
                for (const std::string& event : playerWatcher.Poll(snapshot)) loader.DeliverPlayer(event, snapshot);
            }
            if (std::chrono::steady_clock::now() >= nextReload) {
                ServiceReloadRequests(kit, reload, loader, settings);
                nextReload = std::chrono::steady_clock::now() + kReloadEvery;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }).detach();

    if (role == amk::ProcessRole::AppleMusic) StartInterface(journal, kit, winHost.Ui(), loader);

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
