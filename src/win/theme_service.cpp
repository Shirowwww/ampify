// Built with AMPIFY_WITH_XAML, like xaml_backend.cpp. Only the file reads and the posts are plain Win32 and C++.

#if defined(AMPIFY_WITH_XAML)

#include "theme_service.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "element_picker.hpp"
#include "lcd_links.hpp"
#include "sidebar_hover.hpp"
#include "topbar_search.hpp"
#include "text_marquee.hpp"
#include "ui_thread.hpp"
#include "win_common.hpp"
#include "window_backdrop.hpp"
#include "xaml_backend.hpp"
#include "xaml_watcher.hpp"
#include "../core/settings.hpp"
#include "../core/theme/css.hpp"
#include "../core/theme/fonts.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/theme/layout.hpp"
#include "../core/theme/library.hpp"
#include "../core/theme/scheme_choice.hpp"
#include "../core/theme/session.hpp"
#include "../core/theme/theme_settings.hpp"
#include "../core/theme/worker.hpp"
#include "../core/ui_watchdog.hpp"

namespace ampify::win {

namespace {

constexpr auto kTick = std::chrono::seconds(3);
constexpr auto kChangeCheck = std::chrono::milliseconds(250);
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

// When settings.txt and reload.txt were last written. `ampify theme set`, the Marketplace and `ampify dev` write one of
// them: the next pass then starts at once instead of at the end of the tick.
std::pair<std::filesystem::file_time_type, std::filesystem::file_time_type> ChangeStamp(const std::filesystem::path& kit) {
    std::error_code error;
    std::filesystem::file_time_type settings = std::filesystem::last_write_time(kit / "settings.txt", error);
    std::filesystem::file_time_type reload = std::filesystem::last_write_time(kit / "reload.txt", error);
    return {settings, reload};
}

// The window material of the theme on screen (--window-material in its :root), none without a theme.
std::optional<theme::WindowMaterial> MaterialOf(const theme::Theme* applied) {
    if (!applied) return std::nullopt;
    auto value = applied->variables.find("window-material");
    return value == applied->variables.end() ? std::nullopt : theme::WindowMaterialOf(value->second);
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
void UpdateThemeSettings(const std::filesystem::path& kit, Journal& journal,
                         const std::function<void(theme::ThemeSettings&)>& change) {
    std::string error;
    bool written = Settings::UpdateFile((kit / "settings.txt").string(), [&](Settings& settings) {
        theme::ThemeSettings value = theme::ReadThemeSettings(settings);
        change(value);
        theme::WriteThemeSettings(settings, value);
    }, error);
    if (!written) journal.Write("Settings: theme keys not written, " + error);
}

}  // namespace

// Owned by the service, touched only by the posted tasks, which run on the interface thread.
struct UiSide {
    UiSide(Journal& journal) : session(journal) {}

    XamlTreeBackend backend;
    theme::ThemeSession session;
    bool attached = false;
    bool ticked = false;  // a first tick ran: the theme is on the window, whatever its visibility then
    int timedPasses = 0;  // passes after a change whose duration went to the log
    int timedRefreshes = 0;  // the same for the window's resource refreshes
    std::size_t heldStates = 0;  // the counts last written to the log
    std::size_t refusedStates = 0;
    std::shared_ptr<const theme::Theme> fontsOf;  // what the published font roles come from (null: the defaults)
    // The passes after changes over the current minute: how many, and how long they held the interface thread.
    std::chrono::steady_clock::time_point minuteStart = std::chrono::steady_clock::now();
    int minutePasses = 0;
    std::chrono::steady_clock::duration minuteBusy{};
};

// Once a minute, when the passes after changes held the interface thread long enough to be felt (a scroll that builds
// cards asks for one every few frames), their count and total go to the log.
static void NoteMinute(UiSide& side, std::chrono::steady_clock::time_point now, std::chrono::steady_clock::duration busy,
                       Journal& journal) {
    constexpr auto kMinute = std::chrono::minutes(1);
    constexpr auto kFelt = std::chrono::milliseconds(200);
    side.minutePasses++;
    side.minuteBusy += busy;
    if (now - side.minuteStart < kMinute) return;
    if (side.minuteBusy >= kFelt) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(side.minuteBusy).count();
        journal.Write("Themes: " + std::to_string(side.minutePasses) + " passes after changes in the last minute, " +
                      std::to_string(ms) + " ms in all");
    }
    side.minuteStart = now;
    side.minutePasses = 0;
    side.minuteBusy = {};
}

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
    {
        Settings settings;
        settings.Load((m_kit / "settings.txt").string());
        if (settings.GetBool("xaml.watcher", false)) StartXamlWatcher(m_journal, module);
    }

    // Reads the settings, the theme, the snippets, sidebar.json and home.json, and reloads only what changed. The
    // Windows mode and the local time choose the scheme for theme.scheme = auto and theme.schedule.
    theme::ThemeWorker worker(m_kit);
    // The artwork's colours (var(--art-1)...), read by the backend on the interface thread.
    std::shared_ptr<ArtworkColours> artwork = m_ui->backend.ArtworkSample();
    std::map<std::string, std::string> lastArtwork;
    int artworkReports = 0;

    while (m_running) {
        auto stamp = ChangeStamp(m_kit);
        Settings settings;
        settings.Load((m_kit / "settings.txt").string());
        worker.SetSystemDark(SystemUsesDarkMode());
        {
            std::lock_guard<std::mutex> guard(artwork->lock);
            if (artwork->variables != lastArtwork) {
                lastArtwork = artwork->variables;
                worker.SetArtwork(lastArtwork);
                if (artworkReports++ < kTimedPasses) {
                    std::string list;
                    for (const auto& [name, value] : lastArtwork) list += (list.empty() ? "" : ", ") + name + " " + value;
                    m_journal.Write("Themes: artwork colours " + list);
                }
            }
        }
        theme::SessionRequest request = worker.Tick(settings, LocalMinute());

        std::shared_ptr<UiSide> ui = m_ui;
        Journal& journal = m_journal;
        std::filesystem::path kit = m_kit;
        PostToUiThread([ui, request, kit, &journal]() {
            ampify::ActivityScope activity("theme tick");
            if (!ui->attached) {
                ui->attached = ui->backend.Attach();
                if (!ui->attached) return;
                // New pages and rows get the theme within a fraction of a second instead of at the next tick.
                std::weak_ptr<UiSide> weak = ui;
                ui->backend.WatchChanges([weak, &journal] {
                    std::shared_ptr<UiSide> side = weak.lock();
                    if (!side || side->backend.WindowClosed()) return;
                    ampify::ActivityScope activity("theme pass after a change");
                    auto start = std::chrono::steady_clock::now();
                    side->session.Reapply(side->backend);
                    auto end = std::chrono::steady_clock::now();
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                    NoteMinute(*side, end, end - start, journal);
                    bool worthLogging = side->timedPasses < kTimedPasses || ms.count() > kSlowPassMs;
                    if (worthLogging && side->timedPasses < kMaxTimedPasses) {
                        side->timedPasses++;
                        journal.Write("Themes: pass after a change, " + std::to_string(ms.count()) + " ms");
                    }
                });
                // With xaml.watcher, every element WinUI adds asks for that pass too.
                SetXamlWatcherListener([weak] {
                    if (std::shared_ptr<UiSide> side = weak.lock()) side->backend.ElementAdded();
                });
                SetSidebarChangedListener([weak] {
                    if (std::shared_ptr<UiSide> side = weak.lock()) side->backend.RecheckMedia();
                });
            }
            if (ui->backend.WindowClosed()) return;
            // Hidden in the notification area (music playing, window closed to the tray) or minimized: the periodic
            // pass waits, and the first tick once the window shows again applies what changed meanwhile. Not the very
            // first tick: a window that starts hidden still gets its theme before it first shows.
            if (ui->ticked && !ui->backend.WindowShown()) return;
            ui->ticked = true;
            KeepDisplayLinks(journal);
            KeepTextMarquee(journal);
            KeepSidebarOnHover(journal);
            KeepTopbarSearch(journal);
            ui->backend.RecheckMedia();

            std::optional<std::string> dump = ui->session.Tick(ui->backend, request);
            if (std::shared_ptr<const theme::Theme> applied = ui->session.Applied(); applied != ui->fontsOf) {
                PublishFontRoles(theme::FontRolesJson(applied.get()));
                ui->fontsOf = std::move(applied);
            }
            KeepWindowMaterial(MaterialOf(ui->fontsOf.get()), journal);
            std::size_t held = ui->backend.HeldStateCount();
            std::size_t refused = ui->backend.RefusedStateCount();
            if (held != ui->heldStates || refused != ui->refusedStates) {
                ui->heldStates = held;
                ui->refusedStates = refused;
                const std::string& why = ui->backend.FirstStateRefusal();
                journal.Write("Themes: " + std::to_string(held) + " value(s) kept under the pointer and while pressed, " +
                              std::to_string(refused) + " refused" + (refused && !why.empty() ? " (first: " + why + ")" : ""));
            }
            // The app's own light or dark mode follows the scheme, so what the theme does not name matches what it does
            // (the midnight theme alone left light text on Apple Music's white background). The scheme's background
            // decides when it gives one (a Spicetify scheme is often named Nord or Mocha), its name otherwise.
            auto modeStart = std::chrono::steady_clock::now();
            bool refreshed = ui->backend.SetAppMode(
                request.theme ? theme::AppModeOf(request.theme->dark, request.theme->scheme) : std::nullopt);
            if (std::string note = ui->backend.TakeCaptionNote(); !note.empty()) journal.Write(note);
            if (refreshed) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - modeStart);
                bool worthLogging = ui->timedRefreshes < kTimedPasses || ms.count() > kSlowPassMs;
                if (worthLogging && ui->timedRefreshes < kMaxTimedPasses) {
                    ui->timedRefreshes++;
                    journal.Write("Themes: the window read its theme resources again, " + std::to_string(ms.count()) +
                                  " ms (" + ui->backend.RefreshReason() + ")");
                }
            }
            if (dump) {
                ampify::ActivityScope inspector("inspector tree");
                std::ofstream(JournalFile("inspector.txt")) << *dump;
                std::ofstream(JournalFile("inspector.json")) << theme::InspectTreeJson(ui->backend);
                WriteSidebarIfMissing(kit, ui->backend);
                UpdateThemeSettings(kit, journal, [](theme::ThemeSettings& value) { value.inspect = false; });
                journal.Write("Inspector: tree written to logs\\inspector.txt and inspector.json");
            }
            ampify::ActivityScope picker("element picker");
            AnswerPickRequest(kit, JournalFile("picked.txt"), journal);  // ampify devtools pick
        });

        // A new song's colours end the wait at once: the theme that names them follows the song without a tick's delay.
        for (auto waited = std::chrono::milliseconds(0); m_running && waited < kTick; waited += kChangeCheck) {
            {
                std::unique_lock<std::mutex> lock(artwork->lock);
                if (artwork->changed.wait_for(lock, kChangeCheck, [&] { return artwork->variables != lastArtwork; })) {
                    break;
                }
            }
            if (ChangeStamp(m_kit) != stamp) break;
        }
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
