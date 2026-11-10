// Built with AMPIFY_WITH_XAML, like xaml_backend.cpp. Only the file reads and the posts are plain Win32 and C++.

#if defined(AMPIFY_WITH_XAML)

#include "theme_service.hpp"

#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <chrono>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "element_picker.hpp"
#include "mini_player_theme.hpp"
#include "apple_tweak.hpp"
#include "artwork_passthrough.hpp"
#include "sidebar_hover.hpp"
#include "ui_thread.hpp"
#include "win_common.hpp"
#include "window_backdrop.hpp"
#include "xaml_backend.hpp"
#include "xaml_watcher.hpp"
#include "release_facts.hpp"
#include "../core/manifest.hpp"
#include "../core/panel_radius.hpp"
#include "../core/settings.hpp"
#include "../core/settings_catalog.hpp"
#include "../core/text_util.hpp"
#include "../core/theme/retired.hpp"
#include "../core/theme/css.hpp"
#include "../core/theme/fonts.hpp"
#include "../core/theme/glass.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/theme/layout.hpp"
#include "../core/theme/library.hpp"
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
constexpr int kMaxAsideCopies = 100;  // copies of one retired folder kept in kit/retired

// When settings.txt and reload.txt were last written. `ampify theme set`, the Marketplace and `ampify dev` write one of
// them: the next pass then starts at once instead of at the end of the tick.
std::pair<std::filesystem::file_time_type, std::filesystem::file_time_type> ChangeStamp(const std::filesystem::path& kit) {
    std::error_code error;
    std::filesystem::file_time_type settings = std::filesystem::last_write_time(kit / "settings.txt", error);
    std::filesystem::file_time_type reload = std::filesystem::last_write_time(kit / "reload.txt", error);
    return {settings, reload};
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

// Once per start: settings.txt moves the keys of the elements Ampify retired or merged (theme/retired.hpp), and
// Ampify's own copies of those elements move to kit/retired, so that the settings page no longer offers them. A
// folder someone else wrote under the same name stays.
void MoveRetiredElements(const std::filesystem::path& kit, Journal& journal) {
    const std::string path = (kit / "settings.txt").string();
    Settings settings;
    settings.Load(path);
    if (theme::MoveRetiredSettings(settings)) {
        std::string error;
        bool written = Settings::UpdateFile(path, [](Settings& current) { theme::MoveRetiredSettings(current); }, error);
        journal.Write(written ? "Settings: keys of retired and merged elements moved"
                              : "Settings: keys of retired elements not moved, " + error);
        // The folders wait for the keys: a theme.active naming a removed folder would leave no theme at all.
        if (!written) return;
    }
    for (std::string_view retired : theme::RetiredFolders()) {
        const std::filesystem::path folder = kit / std::filesystem::path(retired);
        std::optional<std::string> text = ReadWholeFile(folder / "manifest.json");
        if (!text) continue;
        if (ParseManifest(*text, "", kKitApiMajor, nullptr).partial.author != "Shirowwww") continue;
        // Set aside in kit/retired, never deleted: the copy may hold changes made by hand.
        std::filesystem::path aside = kit / "retired" / std::filesystem::path(retired);
        for (int copy = 2; std::filesystem::exists(aside) && copy < kMaxAsideCopies; ++copy) {
            aside = kit / "retired" / std::filesystem::path(std::string(retired) + "-" + std::to_string(copy));
        }
        std::error_code error;
        std::filesystem::create_directories(aside.parent_path(), error);
        if (!error) std::filesystem::rename(folder, aside, error);
        journal.Write("Themes: retired " + std::string(retired) +
                      (error ? ", not moved: " + error.message() : ", set aside in " + aside.generic_string()));
    }
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
    // What the session applies, theme and snippets together (null: nothing): its font roles are published, its
    // window material kept.
    std::shared_ptr<const theme::Theme> shown;
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

void ThemeService::Start(HINSTANCE module) {
    if (m_running.exchange(true)) return;
    m_worker = std::thread([this, module] { Run(module); });
}

void ThemeService::Stop() {
    m_running = false;
    if (m_worker.joinable()) m_worker.join();
    StopMiniPlayerTheme();
    DetachUiThread();
}

void ThemeService::Run(HINSTANCE module) {
    while (m_running && !AttachUiThread()) std::this_thread::sleep_for(kAttachRetry);
    if (!m_running) return;
    m_journal.Write("Themes: interface thread ready (DispatcherQueue of the main window)");
    {
        Settings settings;
        settings.Load((m_kit / "settings.txt").string());
        if (ToggleOf(settings, kXamlWatcherKey)) StartXamlWatcher(m_journal, module);
    }
    MoveRetiredElements(m_kit, m_journal);

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
        OfferMiniPlayerTheme(request, m_journal);  // Apple's mini player, on its own thread, gets the same

        std::shared_ptr<UiSide> ui = m_ui;
        Journal& journal = m_journal;
        std::filesystem::path kit = m_kit;
        PostToUiThread([ui, request, kit, &journal]() {
            ampify::ActivityScope activity("theme tick");
            theme::SetCurrentGlassLevel(request.glass);
            auto tickStart = std::chrono::steady_clock::now();
            if (!ui->attached) {
                {
                    ampify::ActivityScope step("theme tick: attach to the window");
                    ui->attached = ui->backend.Attach();
                }
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
            bool firstTick = !ui->ticked;
            ui->ticked = true;
            {
                ampify::ActivityScope step("theme tick: Apple Music tweaks");
                KeepAppleTweaks(journal);
                ui->backend.RecheckMedia();
            }

            std::optional<theme::Inspection> inspection;
            {
                ampify::ActivityScope step("theme tick: apply the theme");
                inspection = ui->session.Tick(ui->backend, request);
            }
            // The first tick puts the whole theme on the window: its length is part of the start-up time.
            if (firstTick) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tickStart);
                journal.Write("Themes: first tick, " + std::to_string(ms.count()) + " ms on the interface thread");
            }
            if (std::shared_ptr<const theme::Theme> applied = ui->session.Applied(); applied != ui->shown) {
                PublishFontRoles(theme::FontRolesJson(applied.get()));
                PublishPanelRadius(applied ? PanelRadiusOf(applied->variables) : std::nullopt);
                const std::string* veil = nullptr;
                if (applied) {
                    if (auto found = applied->variables.find("artwork-veil"); found != applied->variables.end()) {
                        veil = &found->second;
                    }
                }
                KeepArtworkVeil(veil);
                ui->shown = std::move(applied);
            }
            KeepWindowMaterial(ui->shown ? ui->shown->Material() : std::nullopt, journal);
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
            bool refreshed = false;
            {
                ampify::ActivityScope step("theme tick: light or dark mode");
                refreshed = ui->backend.SetAppMode(request.theme ? request.theme->dark : std::nullopt);
            }
            if (std::string note = ui->backend.TakeCaptionNote(); !note.empty()) journal.Write(note);
            if (std::string note = ui->backend.TakeArtworkNote(); !note.empty()) journal.Write(note);
            if (refreshed) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - modeStart);
                bool worthLogging = ui->timedRefreshes < kTimedPasses || ms.count() > kSlowPassMs;
                if (worthLogging && ui->timedRefreshes < kMaxTimedPasses) {
                    ui->timedRefreshes++;
                    journal.Write("Themes: the window read its theme resources again, " + std::to_string(ms.count()) +
                                  " ms (" + ui->backend.RefreshReason() + ")");
                }
            }
            if (inspection) {
                ampify::ActivityScope inspector("inspector tree");
                std::ofstream(JournalFile("inspector.txt")) << theme::InspectionText(*inspection);
                theme::InspectOptions recorded;
                recorded.theme = request.themeId;
                recorded.originals = &ui->session.Originals();
                std::ofstream(JournalFile("inspector.json")) << theme::InspectionJson(*inspection, recorded);
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
