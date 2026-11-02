// The kernel: a DLL that ampify_launcher.exe loads into AppleMusic.exe and AMPLibraryAgent.exe.
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
#include "../core/i18n.hpp"
#include "../core/injection_policy.hpp"
#include "../core/player_watch.hpp"
#include "../core/journal.hpp"
#include "../core/reload.hpp"
#include "../core/safe_mode.hpp"
#include "../core/settings.hpp"
#include "../core/ui_model.hpp"
#include "../core/version_gate.hpp"
#include "../core/version_list.hpp"
#include "../../extensions/atmos-leveler/atmos_windows.hpp"
#include "../win/win_common.hpp"
#include "../win/win_host.hpp"

#if defined(AMPIFY_WITH_XAML)
#include "../core/json.hpp"
#include "../win/close_to_tray.hpp"
#include "../win/theme_service.hpp"
#include "../win/ui_service.hpp"
#include "../win/xaml_host.hpp"
#endif

namespace {

constexpr auto kHealthyAfter = std::chrono::seconds(20);
constexpr auto kHealthyRetry = std::chrono::seconds(5);
constexpr auto kReloadEvery = std::chrono::milliseconds(500);

// Reads reload.txt and runs its new requests: start, stop, restart, rescan (src/core/reload.hpp). The settings are read
// again first, so an element that `ampify` has just enabled starts with the value on disk. A missing file counts as
// empty: the first text is only the baseline, so a file that is created later is not missed.
void ServiceReloadRequests(const std::filesystem::path& kit, ampify::ReloadWatcher& watcher, ampify::ExtensionLoader& loader,
                           ampify::Settings& settings) {
    std::string text;
    std::ifstream in(kit / "reload.txt", std::ios::binary);
    if (in) {
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }

    std::vector<ampify::ReloadRequest> requests = watcher.Requests(text);
    if (requests.empty()) return;

    settings.Load((kit / "settings.txt").string());
    loader.Apply(requests, settings);
}

HINSTANCE g_module = nullptr;  // the kernel DLL itself, passed to the interface-thread facade

// Themes and the native interface only touch the interface, so only AppleMusic.exe starts them. Both reach the
// interface thread through the DispatcherQueue of Apple Music's main window (src/win/xaml_host.hpp). The clicks and
// changes of the rendered trees go back to the JavaScript thread through the elements' queues.
void StartInterface(ampify::Journal& journal, [[maybe_unused]] const std::filesystem::path& kit,
                    [[maybe_unused]] ampify::UiModel& model, [[maybe_unused]] ampify::ExtensionLoader& loader) {
#if defined(AMPIFY_WITH_XAML)
    ampify::win::InstallXamlUiThread(journal);
    // Never destroyed, like the interface service below: its theme session holds XAML objects (resource dictionary,
    // brushes, templates), and a static destructor would release them while Apple Music exits, after XAML is gone.
    static auto* themes = new ampify::win::ThemeService(journal, kit);
    themes->Start(g_module, GetCurrentProcessId());

    // Never destroyed: it holds XAML objects that may only be released on the interface thread.
    static auto* ui = new ampify::win::UiService(journal, kit, model, [target = &loader](const ampify::win::UiEvent& event) {
        if (!event.entry.empty()) {
            target->DeliverEventTo(event.elementId, event.entry, event.valueJson);
            return;
        }
        if (event.click) {
            target->DeliverClickTo(event.elementId, event.callbackId);
            return;
        }
        ampify::Json payload(ampify::Json::Object{});
        payload.Set("id", ampify::Json(static_cast<double>(event.callbackId)));
        std::optional<ampify::Json> value = ampify::Json::Parse(event.valueJson);
        payload.Set("value", value ? *value : ampify::Json());
        target->DeliverEventTo(event.elementId, "change", payload.Dump());
    });
    ui->Start();
#else
    journal.Write("Themes and native interface: this build has no XAML binding (AMPIFY_XAML), nothing is shown");
#endif
}

// Runs once per process. Log, extension host and safe mode state stay alive until the process exits,
// so nothing is torn down while the app still runs.
DWORD WINAPI KernelMain(LPVOID) {
    ampify::ProcessRole role = ampify::RoleOfImageName(ampify::win::ExecutablePath().string());
    if (role == ampify::ProcessRole::Unknown) return 0;
    bool apple = role == ampify::ProcessRole::AppleMusic;
    std::filesystem::path logPath = ampify::win::JournalFile(apple ? "AppleMusic.log" : "AMPLibraryAgent.log");
    // Another Ampify kernel, from another folder, already runs here: a second one would hook the audio and the
    // interface again. It leaves one line in the log, which the first kernel has opened.
    if (!ampify::win::ClaimKernelOfProcess(GetCurrentProcessId())) {
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(g_module, self, MAX_PATH);
        std::ofstream log(logPath, std::ios::app);
        ampify::Journal(log, ampify::Journal::LocalTimeStamp)
            .Write("Another Ampify kernel already runs in this process: this one (" + ampify::win::ToUtf8(self) +
                   ") stays idle");
        return 0;
    }

    // The first process of a role starts its log afresh. A later one, such as the short AppleMusic.exe Windows starts
    // to pass on a music:// link, adds to it instead: the mutex, never closed, marks the log as taken until the first
    // process exits. Both append, so neither writes over the other's lines.
    HANDLE logTaken = CreateMutexW(nullptr, FALSE, apple ? L"Local\\Ampify.AppleMusic.log" : L"Local\\Ampify.AMPLibraryAgent.log");
    bool laterProcess = logTaken && GetLastError() == ERROR_ALREADY_EXISTS;
    if (!laterProcess) std::ofstream(logPath, std::ios::trunc).close();
    static std::ofstream file(logPath, std::ios::app);
    static ampify::Journal journal(file, ampify::Journal::LocalTimeStamp);

    journal.Write("Kernel loaded in " + ampify::win::ExecutablePath().filename().string() + " (process " +
                  std::to_string(GetCurrentProcessId()) + ")");
#if defined(AMPIFY_WITH_XAML)
    // Apple Music started again while its window hides in the notification area: Apple would leave it hidden.
    if (apple && laterProcess && ampify::win::WakeHiddenWindow()) {
        journal.Write("Close to tray: asked the running Apple Music to show its window if it hides");
    }
#endif

    // Read on every start: the package may have been updated since the launcher last looked.
    std::string version = ampify::AppleMusicVersion(ampify::win::PackageFullName());
    ampify::BuildSignature signature = ampify::CheckBuildSignature(
        ampify::win::ReadFileBytes(ampify::win::ExecutablePath().parent_path() / std::string(ampify::kPlayerDllName)));
    journal.Write(ampify::DescribeSignature(signature));
    // Builds tested after this build of Ampify was compiled, from `ampify versions update` (<data folder>\versions.txt). The signature
    // check above still applies, and an unknown build still leaves the kernel idle.
    std::vector<std::string> extraVersions =
        ampify::LoadVersionList(ampify::win::KitDirectory() / std::string(ampify::kVersionListFile));
    ampify::PolicyVerdict verdict = ampify::DecideKernel(role, version, signature, ampify::KnownAppVersionsWith(extraVersions));
    journal.Write(verdict.reason);
    if (!verdict.allowed) return 0;

    std::filesystem::path kit = ampify::win::KitDirectory();
    static ampify::Settings settings;
    settings.Load((kit / "settings.txt").string());

    // JavaScript elements live in %USERPROFILE%\Ampify\extensions\<id> and apps\<id>. Both processes
    // load them: an element tells them apart with Ampify.process.
    static ampify::WinHost winHost(role == ampify::ProcessRole::AppleMusic ? "ui" : "engine", kit, settings, journal);
    ampify::i18n::SetLanguage(ampify::i18n::LanguageCode(winHost.Locale()));
    static ampify::ExtensionHost host(journal);
    static ampify::ExtensionLoader loader(host, winHost, journal, kit / "extensions");
    loader.SetAppRoot(kit / "apps");
    loader.SetThemeRoot(kit / "themes");
    winHost.SetEventSink([](const std::string& entry, const std::string& payload) { loader.DeliverEvent(entry, payload); });
    ampify::LoadReport report = loader.Discover();
    journal.Write("JavaScript elements accepted: " + std::to_string(report.loaded.size()) + ", rejected: " +
                  std::to_string(report.rejected.size()));

    // The Atmos leveler lives where the audio is rendered: AMPLibraryAgent.exe only.
    if (role == ampify::ProcessRole::LibraryAgent) {
        host.Add(atmos::hooks::MakeAtmosLevelerExtension(journal, settings));
    }

    // JavaScript runs on this one thread only: QuickJS is not thread-safe, and the shortcuts are delivered to the
    // thread that registered them. Starting the elements here keeps that true.
    std::thread([kit] {
        host.StartAll(settings);
        journal.Write("Extension host ready");
        ampify::PlayerWatcher playerWatcher;
        int ticks = 0;
        ampify::ReloadWatcher reload;
        auto nextReload = std::chrono::steady_clock::now();
        while (true) {
            winHost.Service([](const std::string& elementId, const std::string& combo) {
                loader.DeliverKey(elementId, combo);
            });
            loader.Pump();
            // The player is looked at every 250 ms, and its changes go to the elements that subscribed to them.
            if (++ticks % 5 == 0) {
                ampify::PlayerSnapshot snapshot = winHost.Player();
                int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
                for (const std::string& event : playerWatcher.Poll(snapshot, nowMs)) {
                    loader.DeliverPlayer(event, snapshot);
                }
            }
            if (std::chrono::steady_clock::now() >= nextReload) {
                ServiceReloadRequests(kit, reload, loader, settings);
                nextReload = std::chrono::steady_clock::now() + kReloadEvery;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }).detach();

    if (role == ampify::ProcessRole::AppleMusic) StartInterface(journal, kit, winHost.Ui(), loader);

    if (role == ampify::ProcessRole::AppleMusic) {
        // The interface process is the one that proves the app is healthy. Its window must not be hung.
        std::this_thread::sleep_for(kHealthyAfter);
        bool warned = false;
        while (!ampify::win::WindowsResponsive(GetCurrentProcessId())) {
            if (!warned) journal.Write("Window not responding after 20 s: retrying every 5 s");
            warned = true;
            std::this_thread::sleep_for(kHealthyRetry);
        }

        ampify::SafeMode safeMode((kit / "state.txt").string());
        safeMode.MarkHealthy();
        journal.Write("Healthy after 20 s: safe mode count reset");
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
