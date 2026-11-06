#pragma once

// Internal to the interface service: the state of one window (WindowContext), the shared state of the service
// (UiService::Shared) and the functions its parts call in one another. Included by ui_service.cpp and the
// ui_service_*.cpp parts only. Each part includes this header first, so it compiles without the precompiled header
// (CI, with ccache).

#if defined(AMPIFY_WITH_XAML)

#include "ui_service.hpp"

#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <inspectable.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Windows.System.h>

#include "../core/element_settings.hpp"
#include "../core/i18n.hpp"
#include "../core/json.hpp"
#include "../core/settings.hpp"
#include "../core/ui_render/apple_tweaks.hpp"
#include "../core/ui_render/mods.hpp"
#include "../core/ui_render/probe.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "../core/ui_render/surfaces.hpp"
#include "../core/ui_render/svg_icon.hpp"
#include "amp_item.hpp"
#include "apple_player.hpp"
#include "close_to_tray.hpp"
#include "kit_command.hpp"
#include "notifications.hpp"
#include "player_controls.hpp"
#include "sharp_artwork.hpp"
#include "text_marquee.hpp"
#include "xaml_host.hpp"

namespace ampify::win {

namespace ui_service_detail {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace primitives = xaml::Controls::Primitives;
namespace media = xaml::Media;
namespace automation = xaml::Automation;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
using winrt::Windows::Foundation::IInspectable;
using uirender::ProbeNode;
using uirender::Surface;
using uirender::SurfaceKind;

// Ampify's own settings page, shown in the host of the app pages: the Ampify row of Settings and any app with ui.page
// (Ampify.ui.navigate) open it.
constexpr char kAmpifyPagePath[] = "/ampify/settings";

inline winrt::hstring Wide(const std::string& text) {
    return winrt::to_hstring(text);
}

inline std::uintptr_t Identity(const IInspectable& object) {
    return reinterpret_cast<std::uintptr_t>(winrt::get_abi(object));
}

inline void Name(const xaml::FrameworkElement& element, const std::string& name, const std::string& accessible = {}) {
    element.Name(Wide(name));
    automation::AutomationProperties::SetAutomationId(element, Wide(name));
    if (!accessible.empty()) automation::AutomationProperties::SetName(element, Wide(accessible));
}

struct HostWatch {
    winrt::weak_ref<xaml::FrameworkElement> node;
    std::uintptr_t identity = 0;
    winrt::event_token loaded{};
    winrt::event_token unloaded{};
    xaml::DependencyProperty property{nullptr};
    std::int64_t propertyToken = 0;
};

struct MountedSurface {
    Surface surface;
    std::unique_ptr<XamlRenderer> renderer;
    // Everything Ampify makes is held strongly: in Apple Music a weak reference to a XAML object Ampify made never
    // resolves (the weak frame was gone every 5 s, then the weak slot right after it was made: an empty panel).
    // Released on the interface thread with the context (ReleaseXaml, DropContext).
    xaml::FrameworkElement panelFrame{nullptr};
    controls::Border slot{nullptr};  // holds the renderer's root
    controls::TextBlock title{nullptr};
    controls::ContentDialog dialog{nullptr};
    bool mounted = false;
    bool dismissed = false;  // a popup the user closed
};

struct PlacedControl {
    controls::Button button{nullptr};  // strong, like the panels: see MountedSurface (a sidebar entry is a Button too)
    bool reported = false;
    std::string label;     // shown now: a new one from setLabel replaces it in place
    bool hasIcon = false;  // the label is then the tooltip only
    bool active = false;   // on (Ampify.ui.setActive): drawn in the accent colour
    bool inHeader = false;  // a sidebar entry standing in the sidebar's header (ampify.entry = header)
    // A sidebar entry in the list: the pane state it was laid out for, and its control, for a change of pane.
    bool laidOut = false;
    bool paneOpen = false;
    UiControl entry;
    std::string elementId;  // with id, the button's place among Ampify's buttons
    int id = 0;
    std::string icon;       // for its entry in the "More" menu
    bool clickable = true;  // a text that only shows something never goes into the "More" menu
    bool folded = false;    // in the playback bar's "More" menu while the bar lacks room (FoldPlaybarButtons)
    double width = 0;       // its width when it was folded: the room it needs to come back
};

// One XAML window of Apple Music (MainWindow or MiniPlayerWindow).
struct WindowContext {
    std::wstring property;
    winrt::weak_ref<xaml::Window> window;
    dispatching::DispatcherQueue dispatcher{nullptr};
    uirender::RefreshTicket ticket;
    std::atomic<bool> discoverDirty{true};
    std::atomic<std::uint64_t> rootRevision{0};

    // Interface thread only, from here on.
    bool observed = false;
    winrt::weak_ref<xaml::DependencyObject> root;
    winrt::event_token rootLayout{};
    winrt::event_token rootSize{};
    bool rootLayoutActive = false;  // both handlers above are registered
    std::vector<HostWatch> watches;
    std::unordered_set<std::uintptr_t> watched;
    std::vector<ProbeNode> probe;
    std::vector<winrt::weak_ref<xaml::DependencyObject>> probeObjects;
    winrt::weak_ref<xaml::FrameworkElement> foundSettings;  // from the last discovery
    winrt::weak_ref<xaml::FrameworkElement> settingsPage;
    winrt::event_token settingsLayout{};
    bool settingsMounted = false;
    std::uint64_t settingsSeenMs = 0;
    // The Ampify row at the top of Settings, next to Apple's own (General, Playback...).
    winrt::weak_ref<xaml::FrameworkElement> foundMainSettings;  // from the last discovery
    winrt::weak_ref<xaml::FrameworkElement> entryPage;          // the page that has the row
    controls::Button settingsEntry{nullptr};                    // strong, like the panels: see MountedSurface
    // The sidebar's back arrow, header logo and pane, followed while the sidebar is folded (FollowFoldedLogo).
    winrt::weak_ref<xaml::FrameworkElement> foldBack;
    winrt::weak_ref<xaml::FrameworkElement> foldLogo;
    winrt::weak_ref<xaml::FrameworkElement> foldPane;
    // The playback bar's song column and the toggles at its right (FollowSongRoom), and the song's own margin.
    winrt::weak_ref<xaml::FrameworkElement> songColumn;
    winrt::weak_ref<xaml::FrameworkElement> songToggles;
    std::optional<xaml::Thickness> songMargin;  // Apple's, while Ampify moves its right side
    std::optional<double> songMaxWidth;         // Apple's, while Ampify keeps the song clear of the transport
    double songLimitedFor = 0;                  // the song's width when Ampify limited it
    double songLimit = 0;                       // the maximum width Ampify gave it
    // What stands left of the toggles in the bar, kept clear of Ampify's buttons (FoldPlaybarButtons), and the
    // "More" button that holds the buttons folded meanwhile. Strong, like the buttons.
    winrt::weak_ref<xaml::FrameworkElement> songDisplay;
    winrt::weak_ref<xaml::FrameworkElement> transport;
    controls::Button playbarMore{nullptr};
    bool songRoomQueued = false;  // FollowSongRoom's update waits for the layout pass to end

    // Main window only.
    // Strong, like the Popup: the panels stay ours even when Apple Music rebuilds the children of its page (in the
    // bench, a host added to MainPage's Grid went away within seconds and the panel was mounted again and again).
    controls::Grid panelHost{nullptr};
    bool panelDocked = false;  // the theme left the panels a column of their own (DockPanelPopup)
    // While panels show, the pages make room for them on their right, as Spotify's side views do: the padding of the
    // grid around Apple's pages, and the padding it had before (PushPages). Strong, released with the context.
    controls::Grid pushedPages{nullptr};
    xaml::Thickness pagesPadding{};
    double pushedRight = -1;  // the right padding Ampify set, -1 when it set none
    controls::StackPanel panelStack{nullptr};
    int panelHostsMade = 0;
    // Strong: nothing else holds a Popup that is outside the tree, and its panels went with it (mounted again on every
    // refresh, never on screen). Released on the interface thread (ReleaseXaml, DropContext).
    primitives::Popup panelPopup{nullptr};
    primitives::Popup pagePopup{nullptr};  // app pages, over the content area
    controls::Grid pageHost{nullptr};      // strong, like the panels: see MountedSurface
    controls::Border pageSlot{nullptr};
    controls::ScrollViewer pageScroll{nullptr};  // shared by the pages: back to the top for each new one
    controls::TextBlock pageTitle{nullptr};
    // While an app page shows, Apple Music's page under it is hidden, so the theme's panel shows through the host, and
    // Apple Music's navigation closes the app page. Strong until the app page closes (UncoverApplePage).
    controls::Frame coveredFrame{nullptr};
    double coveredOpacity = 1.0;
    winrt::event_token coveredNavigated{};
    controls::NavigationView coveredSidebar{nullptr};  // its item already selected does not navigate: watched too
    winrt::event_token coveredInvoked{};
    media::Brush pageBackground{nullptr};  // the host's own, when Apple Music's page cannot be hidden
    // The fullscreen view: a Popup of the window over all of its content, strong like the other hosts.
    primitives::Popup fullscreenPopup{nullptr};
    controls::Grid fullscreenHost{nullptr};
    controls::Border fullscreenSlot{nullptr};
    controls::Button fullscreenClose{nullptr};
    std::string fullscreenKey;                // the surface it shows, "" when none
    xaml::UIElement focusBefore{nullptr};     // what had the focus when it opened, focused again when it closes
    // Popups open in the order they were opened: when the panel or page popup opens after the fullscreen view, the
    // view is opened again so it stays on top.
    std::uint64_t overlayOpens = 0;
    std::uint64_t fullscreenRaisedAt = 0;
    std::map<std::string, MountedSurface> surfaces;
    std::map<std::string, PlacedControl> placed;
    // The panel Ampify puts in the sidebar's PaneFooter for the sidebar entries, and the footer Apple Music had there
    // (kept inside the panel, put back when the last entry goes). Strong, like the other hosts.
    controls::StackPanel sidebarHost{nullptr};
    xaml::UIElement sidebarOriginal{nullptr};
    // The NavigationView the entries follow: its pane opening, folding or changing mode lays them out again.
    winrt::weak_ref<controls::NavigationView> sidebarNavigation;
    std::int64_t sidebarPaneToken = 0;
    std::int64_t sidebarModeToken = 0;
    std::set<std::string> closedPanels;
    std::optional<std::pair<std::string, std::string>> route;
    bool pageClosed = false;
    // The pages the host's arrow goes back to, oldest first, within one visit: Apple Music's navigation ends it.
    std::vector<std::pair<std::string, std::string>> routeHistory;
    bool backPending = false;          // the arrow was clicked with a page to go back to
    controls::StackPanel ampifyPage{nullptr};  // strong, like the hosts; built again each time it is opened
    bool ampifyPageStale = true;
    bool dialogOpen = false;
    // The entries of the extensions for Apple Music's menus (ui.menu): "app" ones for its main menu, "context" ones
    // for the menu of an item. They go into a menu while it is open, after the click or the key that opened it
    // (AddMenuEntries); the timer looks a little longer for a menu that opens late.
    std::vector<UiControl> appEntries;
    std::vector<UiControl> contextEntries;
    winrt::weak_ref<xaml::UIElement> menuInput;          // the content whose clicks and keys are followed
    winrt::weak_ref<xaml::DependencyObject> menuSource;  // what the last click or key went to
    dispatching::DispatcherQueueTimer menuTimer{nullptr};
    int menuTries = 0;
};

// One element's settings card and its status line, held strongly: a weak reference to an element Ampify made never
// resolves. Its buttons reach it through a std::weak_ptr, so they hold no cycle (UiService::Shared::settingsCards).
struct SettingsCard {
    controls::StackPanel panel{nullptr};
    controls::TextBlock status{nullptr};
};

using SharedPtr = std::shared_ptr<UiService::Shared>;
using WeakShared = std::weak_ptr<UiService::Shared>;
using WeakContext = std::weak_ptr<WindowContext>;

// The part of the window where Apple Music shows its pages, in the window's coordinates. A theme may move the play bar
// and the panels (to the bottom, as floating panels), so the frame of the pages comes first; the names the discovery
// found are the fallback (Apple Music 1.1792: Grid#TransportBar at the top, Grid#PaneRoot on the left).
struct ContentArea {
    double left = 0;
    double top = 56;
    double width = 0;   // 0: up to the right edge of the window
    double height = 0;  // 0: down to the bottom edge of the window
};

}  // namespace ui_service_detail

struct UiService::Shared : std::enable_shared_from_this<UiService::Shared> {
    Shared(Journal& journal, std::filesystem::path kit, UiEventSink sink, UiModel& model)
        : journal(journal), kit(std::move(kit)), sink(std::move(sink)), model(model) {}

    Journal& journal;
    std::filesystem::path kit;
    UiEventSink sink;
    UiModel& model;  // thread-safe; the Mods card opens the Marketplace page through it
    std::atomic<bool> running{false};

    std::mutex mutex;  // guards what follows, up to the interface-thread part
    std::vector<std::shared_ptr<ui_service_detail::WindowContext>> windows;
    std::vector<uirender::Surface> surfaces;
    std::optional<std::pair<std::string, std::string>> pendingRoute;
    bool applePlayerPending = false;  // Ampify.ui.applePlayer.open, run by the next refresh of the main window
    bool playerCheckPending = false;  // a window showed: the next refresh looks for Apple's own full-screen player
    uirender::PlayerMode playerMode = uirender::PlayerMode::Window;  // apple.fullscreen, read by the worker
    bool closeToTray = false;  // apple.close, read by the worker
    std::atomic<bool> entryInHeader{false};  // ampify.entry, read on the interface thread (PlaceSidebarItem)
    std::string cardTheme;   // the theme the Ampify page shows
    bool cardStale = false;  // the theme changed elsewhere (Marketplace, ampify theme set): the card is built again

    // Interface thread only.
    int detachedReports = 0;  // log lines about panels taken out of their host, at most 5
    int goneReports = 0;      // log lines about surfaces gone from the model, at most 5
    StyleProbe probe;  // copied from the settings page once it was seen; WinUI defaults before
    // The settings cards of the Ampify page, which owns them: dropped when the page is built again, and a card out of
    // the page (its theme section rebuilt) when the next card is made.
    std::vector<std::shared_ptr<ui_service_detail::SettingsCard>> settingsCards;

    std::mutex onceMutex;
    std::set<std::string> logged;

    void Once(const std::string& key, const std::string& line) {
        {
            std::lock_guard<std::mutex> lock(onceMutex);
            if (!logged.insert(key).second) return;
        }
        journal.Write(line);
    }
};

namespace ui_service_detail {

// ui_service.cpp
ProbeNode Describe(const xaml::DependencyObject& node, int parent);
void RemoveFromParent(const xaml::FrameworkElement& element);
void QueueRefresh(const WeakShared& weakShared, const std::shared_ptr<WindowContext>& context) noexcept;
void Wake(const WeakShared& weakShared, const WeakContext& weakContext, std::uint64_t revision, bool discover) noexcept;

// ui_service_settings.cpp
controls::StackPanel BuildAmpifyPage(UiService::Shared& shared);
void WatchSettingsPage(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context);
void WatchSettingsEntry(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context);

// ui_service_mods.cpp
void OpenKitFolder(const std::filesystem::path& kit);

// The kind of an element as the settings functions name it: "extension", "theme" or "snippet".
inline std::string_view SettingKindOf(const uirender::ModEntry& mod) {
    return mod.snippet ? "snippet" : mod.theme ? "theme" : "extension";
}

// Export and import of one element's settings, as a file and through the clipboard (ui_service_settings_io.cpp).
// `say` shows a short line under the card; `refresh` builds the card again after an import.
using SettingsSay = std::function<void(const std::string&)>;
void ExportElementSettings(UiService::Shared& shared, const uirender::ModEntry& mod, const SettingsSay& say);
void ImportElementSettings(UiService::Shared& shared, const uirender::ModEntry& mod, const std::function<void()>& refresh,
                           const SettingsSay& say);
xaml::FrameworkElement ModRow(UiService::Shared& shared, const uirender::ModEntry& mod);
xaml::FrameworkElement ModSettings(UiService::Shared& shared, const uirender::ModEntry& mod);
xaml::FrameworkElement ThemeSettingsSection(UiService::Shared& shared, const uirender::ModEntry& theme);

// ui_service_panels.cpp
void QuietScrollBar(const controls::ScrollViewer& scroll);
ContentArea FindContentArea(const WindowContext& context, double windowHeight);
void OpenOverlay(WindowContext& context, const primitives::Popup& popup);
void ReleasePages(WindowContext& context);
void PlacePanelPopup(WindowContext& context, const xaml::XamlRoot& root);
std::string SurfaceLabel(const Surface& surface);
uirender::PatchStats RenderInto(UiService::Shared& shared, MountedSurface& mounted, const Surface& surface);
void JournalRender(UiService::Shared& shared, const char* what, const Surface& surface, const uirender::PatchStats& stats,
                   bool first);
void SlidePanelOut(WindowContext& context, const xaml::FrameworkElement& frame);
void MountPanel(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                const xaml::DependencyObject& content, const Surface& surface);

// ui_service_pages.cpp
void UncoverApplePage(WindowContext& context) noexcept;
UiEvent FullscreenClosed(const std::string& elementId, const std::string& session);
void ApplySurfaces(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                   const xaml::DependencyObject& content);

// ui_service_sidebar.cpp
void ReleaseSidebarHost(WindowContext& context);
void PlaceSidebarItem(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context, PlacedControl& placed,
                      const UiControl& control);
void FollowFoldedLogo(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context);

// ui_service_menus.cpp
void FollowMenus(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context);

// ui_service_playbar.cpp
std::uint32_t StableIndex(const std::map<std::string, PlacedControl>& placed, const controls::StackPanel& parent,
                          std::optional<std::uint32_t> anchor, const std::string& elementId, int id);
void PlaceControls(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                   const std::vector<Surface>& surfaces);
void FollowSongRoom(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context);

}  // namespace ui_service_detail

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
