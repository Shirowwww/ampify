// NOT COMPILED IN THE CURRENT BUILD: see xaml_backend.cpp. Only the file reads and the posts are plain Win32 and C++.

#if defined(AMPIFY_WITH_XAML)

#include "theme_service.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ui_thread.hpp"
#include "win_common.hpp"
#include "xaml_backend.hpp"
#include "../core/settings.hpp"
#include "../core/theme/css.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/theme/layout.hpp"
#include "../core/theme/library.hpp"
#include "../core/theme/scheme_choice.hpp"
#include "../core/theme/session.hpp"
#include "../core/theme/theme_settings.hpp"
#include "../core/theme/worker.hpp"

namespace ampify::win {

namespace {

constexpr auto kTick = std::chrono::seconds(3);
constexpr auto kAttachRetry = std::chrono::seconds(1);
// The first passes after a change write their duration to the log, then only the slow ones.
constexpr int kTimedPasses = 5;
constexpr long long kSlowPassMs = 120;
constexpr int kMaxTimedPasses = 40;

// Windows apps in dark mode, for theme.scheme = auto. Light when the value cannot be read (older Windows).
bool SystemUsesDarkMode() {
    DWORD light = 1;
    DWORD size = sizeof(light);
    LONG status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return status == ERROR_SUCCESS && light == 0;
}

// Local time in minutes from midnight, for theme.schedule.
int LocalMinute() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    return now.wHour * 60 + now.wMinute;
}

// After an inspection, writes sidebar.json when it does not exist yet: every NavigationViewItem of the tree, in the
// current order, none hidden. The user then edits it (docs/themes/format.md, "Sidebar and home").
void WriteSidebarIfMissing(const std::filesystem::path& kit, theme::TreeBackend& tree) {
    std::filesystem::path file = kit / std::string(theme::LayoutFileName(theme::LayoutKind::Sidebar));
    std::error_code error;
    if (std::filesystem::exists(file, error)) return;
    std::optional<theme::Selector> items = theme::ParseSelector("NavigationViewItem");
    if (!items) return;
    std::vector<std::string> names = theme::NamesMatching(tree.Snapshot(), *items);
    if (names.empty()) return;
    std::ofstream(file, std::ios::binary) << theme::WriteLayout(theme::GenerateLayout(std::nullopt, names));
}

// Changes only the theme keys of settings.txt, and keeps the rest of the file as it is.
void UpdateThemeSettings(const std::filesystem::path& kit, const std::function<void(theme::ThemeSettings&)>& change) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    theme::ThemeSettings value = theme::ReadThemeSettings(settings);
    change(value);
    theme::WriteThemeSettings(settings, value);
    settings.Save((kit / "settings.txt").string());
}

}  // namespace

// Owned by the service, touched only by the posted tasks, which run on the interface thread.
struct UiSide {
    UiSide(Journal& journal) : session(journal) {}

    XamlTreeBackend backend;
    theme::ThemeSession session;
    bool attached = false;
    int timedPasses = 0;  // passes after a change whose duration went to the log
    std::size_t heldStates = 0;  // the counts last written to the log
    std::size_t refusedStates = 0;
};

ThemeService::ThemeService(Journal& journal, std::filesystem::path kit)
    : m_journal(journal), m_kit(std::move(kit)), m_ui(std::make_shared<UiSide>(journal)) {}

ThemeService::~ThemeService() {
    Stop();
}

void ThemeService::Start(HINSTANCE module, DWORD processId) {
    if (m_running.exchange(true)) return;
    m_worker = std::thread([this, module, processId] { Run(module, processId); });
}

void ThemeService::Stop() {
    m_running = false;
    if (m_worker.joinable()) m_worker.join();
    DetachUiThread();
}

void ThemeService::Run(HINSTANCE module, DWORD processId) {
    while (m_running && !AttachUiThread(processId, module)) std::this_thread::sleep_for(kAttachRetry);
    if (!m_running) return;
    m_journal.Write("Themes: interface thread ready (DispatcherQueue of the main window)");

    // Reads the settings, the theme, the snippets, sidebar.json and home.json, and reloads only what changed. The
    // Windows mode and the local time choose the scheme for theme.scheme = auto and theme.schedule.
    theme::ThemeWorker worker(m_kit);

    while (m_running) {
        Settings settings;
        settings.Load((m_kit / "settings.txt").string());
        worker.SetSystemDark(SystemUsesDarkMode());
        theme::SessionRequest request = worker.Tick(settings, LocalMinute());
        std::string activeId = request.themeId;

        std::vector<std::string> names{""};  // "" is the choice "None"
        for (const std::string& folder : theme::ListThemeFolders(m_kit / "themes")) names.push_back(folder);

        std::shared_ptr<UiSide> ui = m_ui;
        Journal& journal = m_journal;
        std::filesystem::path kit = m_kit;
        PostToUiThread([ui, request, names, activeId, kit, &journal]() {
            if (!ui->attached) {
                ui->attached = ui->backend.Attach();
                if (!ui->attached) return;
                // New pages and rows get the theme within a fraction of a second instead of at the next tick.
                std::weak_ptr<UiSide> weak = ui;
                ui->backend.WatchChanges([weak, &journal] {
                    std::shared_ptr<UiSide> side = weak.lock();
                    if (!side || side->backend.WindowClosed()) return;
                    auto start = std::chrono::steady_clock::now();
                    side->session.Reapply(side->backend);
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start);
                    bool worthLogging = side->timedPasses < kTimedPasses || ms.count() > kSlowPassMs;
                    if (worthLogging && side->timedPasses < kMaxTimedPasses) {
                        side->timedPasses++;
                        journal.Write("Themes: pass after a change, " + std::to_string(ms.count()) + " ms");
                    }
                });
            }
            if (ui->backend.WindowClosed()) return;

            std::optional<std::string> dump = ui->session.Tick(ui->backend, request);
            std::size_t held = ui->backend.HeldStateCount();
            std::size_t refused = ui->backend.RefusedStateCount();
            if (held != ui->heldStates || refused != ui->refusedStates) {
                ui->heldStates = held;
                ui->refusedStates = refused;
                journal.Write("Themes: " + std::to_string(held) + " value(s) kept under the pointer and while pressed, " +
                              std::to_string(ui->backend.RefusedStateCount()) + " refused");
            }
            // The app's own light or dark mode follows the scheme, so what the theme does not name matches what it does
            // (the midnight theme alone left light text on Apple Music's white background). The scheme's background
            // decides when it gives one (a Spicetify scheme is often named Nord or Mocha), its name otherwise.
            ui->backend.SetAppMode(request.theme ? std::optional<bool>(request.theme->dark.value_or(
                                                       theme::IsDarkScheme(request.theme->scheme)))
                                                 : std::nullopt);
            if (dump) {
                std::ofstream(JournalFile("inspector.txt")) << *dump;
                std::ofstream(JournalFile("inspector.json")) << theme::InspectTreeJson(ui->backend);
                WriteSidebarIfMissing(kit, ui->backend);
                UpdateThemeSettings(kit, [](theme::ThemeSettings& value) { value.inspect = false; });
                journal.Write("Inspector: tree written to logs\\inspector.txt and inspector.json");
            }

            // The section is built once, so the choice is compared with settings.txt as it is now, not with the theme
            // active when the section was built (going back to that theme would be ignored).
            ui->backend.EnsureSettingsSection(names, activeId, [kit, &journal](const std::string& choice) {
                Settings current;
                current.Load((kit / "settings.txt").string());
                if (theme::ReadThemeSettings(current).active == choice) return;  // nothing to change
                UpdateThemeSettings(kit, [&choice](theme::ThemeSettings& value) { value.active = choice; });
                journal.Write("Theme chosen in Settings: \"" + (choice.empty() ? std::string("none") : choice) + "\"");
            });
        });

        std::this_thread::sleep_for(kTick);
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
