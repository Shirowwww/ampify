// Built with AMPIFY_WITH_XAML, like xaml_backend.cpp. Only the file reads and the posts are plain Win32 and C++.

#if defined(AMPIFY_WITH_XAML)

#include "theme_service.hpp"

#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "element_picker.hpp"
#include "mini_player_theme.hpp"
#include "presence_watch.hpp"
#include "apple_tweak.hpp"
#include "artwork_passthrough.hpp"
#include "sidebar_hover.hpp"
#include "kit_command.hpp"
#include "kit_watch.hpp"
#include "ui_thread.hpp"
#include "win_common.hpp"
#include "window_backdrop.hpp"
#include "xaml_backend.hpp"
#include "xaml_watcher.hpp"
#include "release_facts.hpp"
#include "../core/kit_layout.hpp"
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
#include "../core/theme/tick_pace.hpp"
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

theme::InstalledCheck InstalledIn(const std::filesystem::path& kit) {
    return [kit](const ElementRef& element) {
        std::error_code error;
        return std::filesystem::exists(kit / element.Folder() / element.id / "manifest.json", error);
    };
}

// At each start: settings.txt moves the keys of the elements Ampify retired or merged (theme/retired.hpp), and
// Ampify's own copies of those elements move to kit/retired, so that the settings page no longer offers them. A
// folder someone else wrote under the same name stays. A merged element in use waits for its replacement, which the
// catalog gives (InstallFromCatalog): returns those to install.
std::vector<ElementRef> MoveRetiredElements(const std::filesystem::path& kit, Journal& journal) {
    const std::string path = (kit / "settings.txt").string();
    Settings settings;
    settings.Load(path);
    const theme::InstalledCheck installed = InstalledIn(kit);
    const std::vector<const theme::RetiredElement*> waiting = theme::AwaitingReplacement(settings, installed);
    std::vector<ElementRef> wanted = theme::ReplacementsToInstall(settings, installed);
    if (theme::MoveRetiredSettings(settings, waiting)) {
        std::string error;
        bool written = Settings::UpdateFile(
            path, [&waiting](Settings& current) { theme::MoveRetiredSettings(current, waiting); }, error);
        journal.Write(written ? "Settings: keys of retired and merged elements moved"
                              : "Settings: keys of retired elements not moved, " + error);
        // The folders wait for the keys: a theme.active naming a removed folder would leave no theme at all.
        if (!written) return wanted;
    }
    for (std::string_view retired : theme::RetiredFolders(waiting)) {
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
    for (const theme::RetiredElement* element : waiting) {
        journal.Write("Themes: " + std::string(element->id) + " stays in use until " + std::string(element->into) +
                      " is installed");
    }
    return wanted;
}

// Fetches elements from the catalog one after the other (they share its download), with ampify.exe as `ampify install`
// would. Offline, what waits keeps the look, and the next start tries again. `done` runs once every one was tried,
// with the ids that were not installed.
void InstallFromCatalog(std::vector<ElementRef> wanted, std::filesystem::path kit, Journal& journal,
                        std::vector<std::string> failed, std::function<void(std::vector<std::string>)> done) {
    if (wanted.empty()) {
        done(std::move(failed));
        return;
    }
    ElementRef next = wanted.front();
    wanted.erase(wanted.begin());
    journal.Write("Themes: installing " + next.id + " from the catalog");
    RunAmpifyInBackground({"install", next.id, "--yes", "--kit-dir", ToUtf8(kit.wstring())},
                          [next, wanted = std::move(wanted), kit, &journal, failed = std::move(failed),
                           done = std::move(done)](int code) mutable {
                              journal.Write(code == 0 ? "Themes: " + next.id + " installed"
                                                      : "Themes: " + next.id + " not installed (code " +
                                                            std::to_string(code) + "), tried again at the next start");
                              if (code != 0) failed.push_back(next.id);
                              InstallFromCatalog(std::move(wanted), std::move(kit), journal, std::move(failed),
                                                 std::move(done));
                          });
}

constexpr const char* kStartersKey = "catalog_starters";  // state.txt: "installed" once the starters were put in

// The catalog elements that start on (Now Playing, Time on hover) and that this kit lacks, or none once they were put
// in: a kit that had them is left alone after that, so an element the user removed stays removed.
std::vector<ElementRef> MissingCatalogStarters(const std::filesystem::path& kit) {
    Settings state;
    state.Load((kit / "state.txt").string());
    if (state.GetString(kStartersKey, "") == "installed") return {};
    const theme::InstalledCheck installed = InstalledIn(kit);
    std::vector<ElementRef> missing;
    for (ElementRef& element : CatalogStarters()) {
        if (!installed(element)) missing.push_back(std::move(element));
    }
    return missing;
}

void MarkStartersInstalled(const std::filesystem::path& kit, Journal& journal) {
    std::string error;
    if (!Settings::UpdateFile((kit / "state.txt").string(), kStartersKey, "installed", error)) {
        journal.Write("Themes: state.txt not written, " + error);
    }
}

constexpr const char* kElementsUpdatedKey = "elements_updated_for";  // state.txt: the release that last updated them

// At the first start of a new release, once the retired elements are replaced: every element from the catalog goes to
// its version there, so that one taking in a retired element has what it took in, and active.txt is written again
// (ampify update --elements). Offline, the next start tries again.
void UpdateElementsForRelease(const std::filesystem::path& kit, Journal& journal) {
    const std::string state = (kit / "state.txt").string();
    Settings current;
    current.Load(state);
    if (current.GetString(kElementsUpdatedKey, "") == kKitVersion) return;
    journal.Write("Themes: updating the elements from the catalog for Ampify " + std::string(kKitVersion));
    RunAmpifyInBackground({"update", "--elements", "--kit-dir", ToUtf8(kit.wstring())}, [state, &journal](int code) {
        if (code != 0) {
            journal.Write("Themes: elements not updated (code " + std::to_string(code) + "), tried again at the next start");
            return;
        }
        std::string error;
        bool written = Settings::UpdateFile(state, kElementsUpdatedKey, std::string(kKitVersion), error);
        journal.Write(written ? "Themes: elements updated from the catalog"
                              : "Themes: elements updated from the catalog, state.txt not written, " + error);
    });
}

}  // namespace

// Owned by the service, touched only by the posted tasks, which run on the interface thread.
struct UiSide {
    UiSide(Journal& journal) : session(journal) {}

    XamlTreeBackend backend;
    theme::ThemeSession session;
    bool attached = false;
    bool ticked = false;  // a first tick ran: the theme is on the window, whatever its visibility then
    // Which ticks go over the whole window, and the request the last of them applied.
    theme::TickPace pace;
    std::uint64_t appliedGeneration = 0;
    std::string appliedTheme;
    int rewrites = 0;  // periodic passes that had to write again, written to the log
    // The window showing again: the next tick applies at once instead of after the rest of the wait (worker thread).
    std::atomic<bool> wake{false};
    std::unique_ptr<PresenceListener> presence;
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
    // Once the replacements are in, the elements that waited for them move, in the same session.
    auto replaced = std::make_shared<std::atomic<bool>>(false);
    std::vector<ElementRef> wanted = MoveRetiredElements(m_kit, m_journal);
    const bool replacing = !wanted.empty();
    // The catalog elements that start on come in the same queue, so that two installs never run at once.
    std::vector<ElementRef> starters = MissingCatalogStarters(m_kit);
    if (starters.empty()) MarkStartersInstalled(m_kit, m_journal);
    std::vector<std::string> starterIds;
    for (const ElementRef& element : starters) starterIds.push_back(element.id);
    wanted.insert(wanted.end(), starters.begin(), starters.end());
    // The elements are updated after the replacements are in and the elements that waited for them moved.
    auto migrated = std::make_shared<std::atomic<bool>>(wanted.empty());
    if (!wanted.empty()) {
        InstallFromCatalog(std::move(wanted), m_kit, m_journal, {},
                           [kit = m_kit, &journal = m_journal, replaced, migrated, replacing,
                            starterIds](std::vector<std::string> failed) {
                               const bool startersIn = std::ranges::none_of(starterIds, [&](const std::string& id) {
                                   return std::ranges::find(failed, id) != failed.end();
                               });
                               if (!starterIds.empty() && startersIn) MarkStartersInstalled(kit, journal);
                               *replaced = replacing;
                               *migrated = true;
                           });
    }
    bool elementsAsked = false;

    // Reads the settings, the theme, the snippets, sidebar.json and home.json, and reloads only what changed. The
    // Windows mode and the local time choose the scheme for theme.scheme = auto and theme.schedule.
    theme::ThemeWorker worker(m_kit);
    // The artwork's colours (var(--art-1)...), read by the backend on the interface thread.
    std::shared_ptr<ArtworkColours> artwork = m_ui->backend.ArtworkSample();
    std::map<std::string, std::string> lastArtwork;
    int artworkReports = 0;
    // The settings, the theme and the snippets are read again only when something in the kit moved, Windows' mode
    // changed, a minute passed (theme.schedule) or the artwork's colours changed: not every three seconds.
    KitWatch kitFiles(m_kit, {"themes", "snippets"});
    bool kitMoved = true;
    std::optional<bool> lastDark;
    int lastMinute = -1;
    theme::SessionRequest request;

    while (m_running) {
        if (replaced->exchange(false)) MoveRetiredElements(m_kit, m_journal);  // what failed is tried at the next start
        if (!elementsAsked && *migrated && !*replaced) {
            elementsAsked = true;
            UpdateElementsForRelease(m_kit, m_journal);
        }
        kitMoved = kitFiles.Moved() || kitMoved;
        const bool dark = SystemUsesDarkMode();
        const int minute = LocalMinute();
        bool artworkMoved = false;
        {
            std::lock_guard<std::mutex> guard(artwork->lock);
            if (artwork->variables != lastArtwork) {
                lastArtwork = artwork->variables;
                worker.SetArtwork(lastArtwork);
                artworkMoved = true;
                if (artworkReports++ < kTimedPasses) {
                    std::string list;
                    for (const auto& [name, value] : lastArtwork) list += (list.empty() ? "" : ", ") + name + " " + value;
                    m_journal.Write("Themes: artwork colours " + list);
                }
            }
        }
        if (kitMoved || artworkMoved || dark != lastDark || minute != lastMinute) {
            Settings settings;
            settings.Load((m_kit / "settings.txt").string());
            worker.SetSystemDark(dark);
            request = worker.Tick(settings, minute);
            kitMoved = false;
            lastDark = dark;
            lastMinute = minute;
        }
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
                WatchPresence(journal);  // the work that only draws follows the windows
                // New pages and rows get the theme within a fraction of a second instead of at the next tick.
                std::weak_ptr<UiSide> weak = ui;
                // Hidden or covered, the periodic pass waits; seen again, it goes over the window at once.
                ui->presence = std::make_unique<PresenceListener>(InterfacePresence(), [weak](bool seen) {
                    if (!seen) return;
                    if (std::shared_ptr<UiSide> side = weak.lock()) {
                        side->pace.Force();
                        side->wake = true;
                    }
                });
                ui->backend.WatchChanges([weak, &journal] {
                    std::shared_ptr<UiSide> side = weak.lock();
                    if (!side || side->backend.WindowClosed()) return;
                    ampify::ActivityScope activity("theme pass after a change");
                    auto start = std::chrono::steady_clock::now();
                    side->session.Reapply(side->backend);
                    side->pace.PassedAfterChange(side->session.LastApplied());
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
            RefreshPresence();  // should a window event have been missed
            // Hidden in the notification area (music playing, window closed to the tray) or minimized: the periodic
            // pass waits, and the first tick once the window shows again applies what changed meanwhile. Not the very
            // first tick: a window that starts hidden still gets its theme before it first shows.
            // Covered by other windows counts as hidden: nobody sees it (src/core/presence.hpp). An inspection asked
            // meanwhile (ampify devtools, from a terminal over the window) still runs.
            const bool covered = !InterfacePresence().Seen() && !request.inspect;
            if (ui->ticked && (!ui->backend.WindowShown() || covered)) return;
            bool firstTick = !ui->ticked;
            ui->ticked = true;
            {
                ampify::ActivityScope step("theme tick: Apple Music tweaks");
                KeepAppleTweaks(journal);
                ui->backend.RecheckMedia();
            }

            std::optional<theme::Inspection> inspection;
            bool requestChanged = firstTick || request.inspect || request.generation != ui->appliedGeneration ||
                                  request.themeId != ui->appliedTheme;
            if (ui->pace.Due(requestChanged)) {
                ampify::ActivityScope step("theme tick: apply the theme");
                inspection = ui->session.Tick(ui->backend, request);
                ui->appliedGeneration = request.generation;
                ui->appliedTheme = request.themeId;
                ui->pace.Ticked(ui->session.LastApplied());
                // A periodic pass that writes again means Apple put values back meanwhile: the pace stays quick.
                if (!firstTick && !requestChanged && ui->session.LastApplied() > 0 && ui->rewrites++ < kTimedPasses) {
                    journal.Write("Themes: the periodic pass wrote " + std::to_string(ui->session.LastApplied()) +
                                  " value(s) again (Apple put them back)");
                }
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
            if (kitFiles.Moved()) {
                kitMoved = true;
                break;
            }
            if (m_ui->wake.exchange(false)) break;
        }
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
