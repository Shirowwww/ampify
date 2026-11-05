// Written against the C++/WinRT projection of Windows App SDK 1.8.
// Window discovery, page watching and the refresh tickets are adapted from AMExclusive (MIT,
// https://github.com/AntiOblivionis/AMExclusive, mod/src/UiAdapter.cpp: ScheduleUiDiscovery, QueueUiRefresh,
// ObserveUiHosts, RefreshUiWindow, AttachOrRefreshToggle, at 91f589d).

#if defined(AMPIFY_WITH_XAML)

#include "ui_service.hpp"

#include <windows.h>
#include <shellapi.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
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

namespace {

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

constexpr auto kModelPoll = std::chrono::milliseconds(250);
constexpr auto kDiscoveryPoll = std::chrono::seconds(5);
constexpr auto kTweaksPoll = std::chrono::seconds(1);  // settings.txt's date, for the Apple Music part of the card
constexpr std::size_t kDiscoveryLimit = 512;
constexpr std::size_t kPageLimit = 4000;
constexpr std::size_t kMaxWatches = 64;
constexpr double kPanelWidth = 380;
constexpr std::size_t kMaxRouteHistory = 16;
constexpr double kAmpifyPageWidth = 1000;  // as wide as Apple's settings rows
// Ampify's own settings page, shown in the host of the app pages: the Ampify row of Settings and any app with ui.page
// (Ampify.ui.navigate) open it.
constexpr char kAmpifyPagePath[] = "/ampify/settings";
constexpr char kAmpifyPageOwner[] = "ampify";
constexpr char kAtmosLeveler[] = "atmos-leveler";  // built into the kernel: no manifest, so no row of its own

std::atomic<bool> g_windowEvent{true};  // set by the WinEvent hook, read by the worker
std::atomic<bool> g_windowShown{false};  // a top-level window showed: perhaps Apple's full-screen player
int g_modsUpdating = 0;                 // interface thread only: > 0 while the Mods card sets its toggles itself

winrt::hstring Wide(const std::string& text) {
    return winrt::to_hstring(text);
}

std::uintptr_t Identity(const IInspectable& object) {
    return reinterpret_cast<std::uintptr_t>(winrt::get_abi(object));
}

void CALLBACK WindowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG objectId, LONG childId, DWORD, DWORD) {
    if ((event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_SHOW) || objectId != OBJID_WINDOW ||
        childId != CHILDID_SELF || !window || GetAncestor(window, GA_ROOT) != window) {
        return;
    }
    // An HWND does not say which XAML window it belongs to: the worker asks AMP.Utils again.
    g_windowEvent = true;
    if (event == EVENT_OBJECT_SHOW) g_windowShown = true;
}

// What the discovery knows of a node, read with try_as on the projected types.
ProbeNode Describe(const xaml::DependencyObject& node, int parent) {
    ProbeNode probe;
    probe.parent = parent;
    probe.className = Utf8(winrt::get_class_name(node));
    if (auto element = node.try_as<xaml::FrameworkElement>()) probe.name = Utf8(element.Name());
    probe.automationId = Utf8(automation::AutomationProperties::GetAutomationId(node));
    unsigned flags = 0;
    if (node.try_as<controls::Panel>()) flags |= uirender::kProbePanel;
    if (auto stack = node.try_as<controls::StackPanel>()) {
        flags |= uirender::kProbeStackPanel;
        if (stack.Orientation() == controls::Orientation::Horizontal) flags |= uirender::kProbeHorizontal;
    }
    if (node.try_as<controls::Grid>()) flags |= uirender::kProbeGrid;
    if (node.try_as<controls::ToggleSwitch>()) flags |= uirender::kProbeToggleSwitch;
    if (node.try_as<controls::ItemsControl>()) flags |= uirender::kProbeItemsControl;
    if (node.try_as<controls::ItemsRepeater>()) flags |= uirender::kProbeItemsRepeater;
    if (node.try_as<primitives::ButtonBase>()) flags |= uirender::kProbeButtonBase;
    if (node.try_as<controls::Page>()) flags |= uirender::kProbePage;
    if (node.try_as<controls::Frame>()) flags |= uirender::kProbeFrame;
    if (node.try_as<controls::UserControl>()) flags |= uirender::kProbeUserControl;
    if (node.try_as<controls::TextBlock>()) flags |= uirender::kProbeTextBlock;
    if (node.try_as<controls::Border>()) flags |= uirender::kProbeBorder;
    if (node.try_as<controls::ComboBox>()) flags |= uirender::kProbeComboBox;
    if (node.try_as<controls::Slider>()) flags |= uirender::kProbeSlider;
    if (node.try_as<controls::TextBox>()) flags |= uirender::kProbeTextBox;
    if (node.try_as<controls::AutoSuggestBox>()) flags |= uirender::kProbeAutoSuggest;
    if (node.try_as<controls::NavigationView>()) flags |= uirender::kProbeNavigationView;
    probe.flags = flags;
    return probe;
}

// Removes `element` from the panel or border that holds it.
void RemoveFromParent(const xaml::FrameworkElement& element) {
    if (!element) return;
    try {
        xaml::DependencyObject parent = element.Parent();
        if (!parent) parent = media::VisualTreeHelper::GetParent(element);
        if (auto panel = parent.try_as<controls::Panel>()) {
            std::uint32_t index = 0;
            if (panel.Children().IndexOf(element, index)) panel.Children().RemoveAt(index);
        } else if (auto border = parent.try_as<controls::Border>()) {
            border.Child(nullptr);
        } else if (auto popup = parent.try_as<primitives::Popup>()) {
            popup.IsOpen(false);
            popup.Child(nullptr);
        }
    } catch (...) {
    }
}

void Name(const xaml::FrameworkElement& element, const std::string& name, const std::string& accessible = {}) {
    element.Name(Wide(name));
    automation::AutomationProperties::SetAutomationId(element, Wide(name));
    if (!accessible.empty()) automation::AutomationProperties::SetName(element, Wide(accessible));
}

constexpr std::size_t kScrollBarSearchLimit = 96;  // the bar and its arrows sit a few levels into the template

std::vector<xaml::DependencyObject> TemplateBelow(const xaml::DependencyObject& root) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < kScrollBarSearchLimit; i++) {
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return queue;
}

// The expanded state of a scroll bar brings its arrows back: no room and no ink keeps them gone.
void HideArrows(const xaml::DependencyObject& bar) {
    for (const auto& item : TemplateBelow(bar)) {
        auto button = item.try_as<primitives::RepeatButton>();
        if (!button) continue;
        std::wstring_view name = button.Name();
        if (name != L"VerticalSmallDecrease" && name != L"VerticalSmallIncrease") continue;
        button.Height(0);
        button.Opacity(0);
    }
}

void SetOpacityOf(IInspectable const& sender, double opacity) noexcept {
    try {
        sender.as<xaml::UIElement>().Opacity(opacity);
    } catch (...) {
    }
}

// The page host is a popup, which themes leave alone, and Windows draws its scroll bar over the page with arrows at
// both ends. It stays out of sight until the pointer reaches it, without the arrows, as the layouts do for Apple's.
// The handlers take the bar from their sender: capturing it would make the bar hold itself.
void QuietScrollBar(const controls::ScrollViewer& scroll) {
    scroll.Loaded([](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            for (const auto& item : TemplateBelow(sender.as<xaml::DependencyObject>())) {
                auto bar = item.try_as<primitives::ScrollBar>();
                if (!bar || bar.Orientation() != controls::Orientation::Vertical) continue;
                bar.Opacity(0);
                using xaml::Input::PointerRoutedEventArgs;
                bar.PointerEntered([](IInspectable const& entered, PointerRoutedEventArgs const&) noexcept {
                    SetOpacityOf(entered, 1);
                });
                bar.PointerExited([](IInspectable const& exited, PointerRoutedEventArgs const&) noexcept {
                    SetOpacityOf(exited, 0);
                });
                HideArrows(bar);
                bar.Loaded([](IInspectable const& loaded, xaml::RoutedEventArgs const&) noexcept {
                    try {
                        HideArrows(loaded.as<xaml::DependencyObject>());
                    } catch (...) {
                    }
                });
                return;
            }
        } catch (...) {
        }
    });
}

}  // namespace

// The state of the service, in a named namespace: UiService::Shared (declared in the header) holds it.
namespace ui_service_detail {

using uirender::Surface;
using uirender::SurfaceKind;

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
    std::string elementId;  // with id, the button's place among Ampify's buttons
    int id = 0;
    std::string icon;       // for its entry in the "More" menu
    bool clickable = true;  // a text that only shows something never goes into the "More" menu
    bool folded = false;    // in the playback bar's "More" menu while the bar lacks room (FoldPlaybarButtons)
    double width = 0;       // its width when it was folded: the room it needs to come back
};

// The accent colour while a top bar or play bar button is on, the text colour otherwise. An icon is drawn again with the
// colour; a text takes it as its foreground. The state is also said to screen readers.
void TintControl(const controls::Button& button, const UiControl& control, bool hasIcon) {
    const wchar_t* key = control.active ? L"AccentTextFillColorPrimaryBrush" : L"TextFillColorPrimaryBrush";
    if (hasIcon) {
        if (xaml::FrameworkElement icon = MakeIconElement(control.icon, 16, key)) {
            // A glyph takes the button's own colour unless it is given one; a drawing already follows the key.
            if (auto glyph = icon.try_as<controls::FontIcon>(); glyph && control.active) {
                if (auto brush = ThemeResource(key).try_as<media::Brush>()) glyph.Foreground(brush);
            }
            button.Content(icon);
        }
    } else if (control.active) {
        if (auto brush = ThemeResource(key).try_as<media::Brush>()) button.Foreground(brush);
    } else {
        button.ClearValue(controls::Control::ForegroundProperty());
    }
    automation::AutomationProperties::SetItemStatus(button, control.active ? L"on" : L"");
}

// Ampify's buttons keep one order whatever is switched on, off or reloaded: by element, then in the order the
// element added them.
bool PlacedBefore(const PlacedControl& a, const std::string& elementId, int id) {
    return a.elementId != elementId ? a.elementId < elementId : a.id < id;
}

// Where a new button goes among the children of `parent`: after its anchor, and among the Ampify buttons already
// there, at its place in the order above.
std::uint32_t StableIndex(const std::map<std::string, PlacedControl>& placed, const controls::StackPanel& parent,
                          std::optional<std::uint32_t> anchor, const std::string& elementId, int id) {
    auto children = parent.Children();
    std::optional<std::uint32_t> firstAfter, lastBefore;
    for (const auto& [key, other] : placed) {
        std::uint32_t index = 0;
        if (!other.button || other.elementId.empty() || !children.IndexOf(other.button, index)) continue;
        if (PlacedBefore(other, elementId, id)) {
            if (!lastBefore || index > *lastBefore) lastBefore = index;
        } else if (!firstAfter || index < *firstAfter) {
            firstAfter = index;
        }
    }
    if (firstAfter) return *firstAfter;
    if (lastBefore) return *lastBefore + 1;
    return anchor ? *anchor + 1 : children.Size();
}

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

}  // namespace ui_service_detail

using namespace ui_service_detail;

struct UiService::Shared : std::enable_shared_from_this<UiService::Shared> {
    Shared(Journal& journal, std::filesystem::path kit, UiEventSink sink, UiModel& model)
        : journal(journal), kit(std::move(kit)), sink(std::move(sink)), model(model) {}

    Journal& journal;
    std::filesystem::path kit;
    UiEventSink sink;
    UiModel& model;  // thread-safe; the Mods card opens the Marketplace page through it
    std::atomic<bool> running{false};

    std::mutex mutex;  // guards what follows, up to the interface-thread part
    std::vector<std::shared_ptr<WindowContext>> windows;
    std::vector<Surface> surfaces;
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

namespace {

using SharedPtr = std::shared_ptr<UiService::Shared>;
using WeakShared = std::weak_ptr<UiService::Shared>;
using WeakContext = std::weak_ptr<WindowContext>;

void Refresh(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context);

std::string WindowLabel(const WindowContext& context) {
    return winrt::to_string(context.property);
}

// Queues one refresh of the window on its interface thread. A refresh already queued is not queued twice.
void QueueRefresh(const WeakShared& weakShared, const std::shared_ptr<WindowContext>& context) noexcept {
    std::uint64_t ticket = context->ticket.Request();
    if (!ticket) return;
    try {
        WeakContext weakContext = context;
        if (context->dispatcher.TryEnqueue(dispatching::DispatcherQueuePriority::Low,
                                           [weakShared, weakContext, ticket]() noexcept {
                                               std::shared_ptr<WindowContext> live = weakContext.lock();
                                               if (!live || !live->ticket.Take(ticket)) return;
                                               SharedPtr shared = weakShared.lock();
                                               if (!shared || !shared->running) return;
                                               try {
                                                   Refresh(*shared, live);
                                               } catch (const winrt::hresult_error& error) {
                                                   shared->journal.Write("Interface: refresh of \"" +
                                                                         WindowLabel(*live) + "\" failed (" +
                                                                         HresultText(error.code().value) + ")");
                                               } catch (...) {
                                                   shared->journal.Write("Interface: refresh of \"" +
                                                                         WindowLabel(*live) +
                                                                         "\" failed (unknown exception)");
                                               }
                                           })) {
            return;
        }
    } catch (...) {
        // The dispatcher may be shutting down while the window closes.
    }
    context->ticket.Take(ticket);
}

// A Loaded, Unloaded, Content or LayoutUpdated callback: stale ones (older root) are ignored.
void Wake(const WeakShared& weakShared, const WeakContext& weakContext, std::uint64_t revision, bool discover) noexcept {
    try {
        std::shared_ptr<WindowContext> live = weakContext.lock();
        if (!live || live->rootRevision.load() != revision) return;
        if (discover) live->discoverDirty = true;
        QueueRefresh(weakShared, live);
    } catch (...) {
    }
}

void ForgetFullscreenHost(WindowContext& context) {
    context.fullscreenPopup = nullptr;
    context.fullscreenHost = nullptr;
    context.fullscreenSlot = nullptr;
    context.fullscreenClose = nullptr;
    context.fullscreenKey.clear();
    context.focusBefore = nullptr;
}

void ClearWatches(WindowContext& context) {
    for (HostWatch& watch : context.watches) {
        try {
            if (auto element = watch.node.get()) {
                element.Loaded(watch.loaded);
                element.Unloaded(watch.unloaded);
                if (watch.property) element.UnregisterPropertyChangedCallback(watch.property, watch.propertyToken);
            }
        } catch (...) {
        }
    }
    context.watches.clear();
    context.watched.clear();
}

void UncoverApplePage(WindowContext& context) noexcept;
void ReleasePages(WindowContext& context);

// Drops what the window holds of XAML: the watches, the panel popup, the renderers and their controls, the placed
// buttons. Interface thread only: XAML objects must not be released from the worker, which drops the contexts of
// closed or replaced windows (DiscoverWindows).
void ReleaseXaml(WindowContext& context) noexcept {
    try {
        ClearWatches(context);
        UncoverApplePage(context);
        ReleasePages(context);  // the pages' grid outlives the context: it takes its padding back
        if (context.panelPopup) context.panelPopup.IsOpen(false);
        if (context.pagePopup) context.pagePopup.IsOpen(false);
        if (context.fullscreenPopup) context.fullscreenPopup.IsOpen(false);
        RemoveFromParent(context.settingsEntry);
    } catch (...) {
    }
    try {
        ForgetFullscreenHost(context);
        context.settingsEntry = nullptr;
        context.panelPopup = nullptr;
        context.pagePopup = nullptr;
        context.panelHost = nullptr;
        context.panelDocked = false;
        context.panelStack = nullptr;
        context.pageHost = nullptr;
        context.pageSlot = nullptr;
        context.pageScroll = nullptr;
        context.pageTitle = nullptr;
        context.pageBackground = nullptr;
        context.ampifyPage = nullptr;
        context.sidebarHost = nullptr;
        context.sidebarOriginal = nullptr;
        if (context.menuTimer) context.menuTimer.Stop();
        context.menuTimer = nullptr;
        context.appEntries.clear();
        context.contextEntries.clear();
        context.surfaces.clear();
        context.placed.clear();
        context.playbarMore = nullptr;
        context.probe.clear();
        context.probeObjects.clear();
        context.dialogOpen = false;
    } catch (...) {
    }
}

// A context that leaves the service (window closed or replaced): its last reference goes on the window's interface
// thread, after ReleaseXaml. When the queue refuses the work (shutting down), the context is kept for good instead:
// releasing its XAML objects from the worker could crash Apple Music while it closes.
void DropContext(std::shared_ptr<WindowContext> context) noexcept {
    auto* holder = new (std::nothrow) std::shared_ptr<WindowContext>(std::move(context));
    if (!holder) return;
    try {
        dispatching::DispatcherQueue dispatcher = (*holder)->dispatcher;
        if (dispatcher && dispatcher.TryEnqueue(dispatching::DispatcherQueuePriority::Low, [holder]() noexcept {
                ReleaseXaml(**holder);
                delete holder;
            })) {
            return;
        }
    } catch (...) {
    }
    // Not queued: `holder` is never freed.
}

// ---- Discovery ---------------------------------------------------------------------------------------------------

// Bounded breadth-first walk of the window content (512 nodes), never into item lists, buttons or Ampify's
// elements. Watches Loaded, Unloaded and Content of the Frame, Page and UserControl met, so a navigation wakes the
// refresh. Keeps the flat list of nodes for the anchors (uirender::Choose*), and notes the PlaybackSettingsPage.
void Discover(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
              const xaml::DependencyObject& content) {
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = context;
    std::uint64_t revision = context->rootRevision.load();

    std::erase_if(context->watches, [&context](const HostWatch& watch) {
        if (watch.node.get()) return false;
        context->watched.erase(watch.identity);
        return true;
    });

    std::vector<ProbeNode> nodes;
    std::vector<winrt::weak_ref<xaml::DependencyObject>> objects;
    std::unordered_map<std::uintptr_t, int> parentOf;
    xaml::FrameworkElement settings{nullptr};
    xaml::FrameworkElement mainSettings{nullptr};
    std::size_t pages = 0;

    uirender::WalkBounded(
        content, [](const xaml::DependencyObject& node) { return Identity(node); },
        [&](const xaml::DependencyObject& node, auto push) {
            auto known = parentOf.find(Identity(node));
            int parent = known == parentOf.end() ? -1 : known->second;
            ProbeNode probe = Describe(node, parent);
            int index = static_cast<int>(nodes.size());
            nodes.push_back(probe);
            objects.push_back(winrt::make_weak(node));

            auto element = node.try_as<xaml::FrameworkElement>();
            bool isPage = probe.Is(uirender::kProbePage);
            bool isSettings = isPage && probe.className.find("PlaybackSettingsPage") != std::string::npos;
            if (isPage) pages++;
            if (isSettings) settings = element;
            if (isPage && probe.className.find("MainSettingsPage") != std::string::npos) mainSettings = element;

            // The logical child of a Frame or a UserControl, or a ContentControl that holds a Page: the route
            // to the pages, shorter than the visual tree of their templates.
            xaml::DependencyProperty property{nullptr};
            xaml::DependencyObject logical{nullptr};
            if (auto frame = node.try_as<controls::Frame>()) {
                property = controls::ContentControl::ContentProperty();
                logical = frame.Content().try_as<xaml::DependencyObject>();
            } else if (auto user = node.try_as<controls::UserControl>()) {
                property = controls::UserControl::ContentProperty();
                logical = user.Content();
            } else if (auto holder = node.try_as<controls::ContentControl>()) {
                auto child = holder.Content().try_as<xaml::DependencyObject>();
                if (node == content || (child && child.try_as<controls::Page>())) {
                    property = controls::ContentControl::ContentProperty();
                    logical = child;
                }
            }

            std::uintptr_t identity = Identity(node);
            bool watchable = element && (property || isPage || node == content);
            if (watchable && context->watches.size() < kMaxWatches && !context->watched.count(identity)) {
                HostWatch watch;
                watch.node = winrt::make_weak(element);
                watch.identity = identity;
                watch.property = property;
                // Loaded does not bubble: each container is watched itself.
                watch.loaded = element.Loaded([weakShared, weakContext, revision](IInspectable const&,
                                                                                   xaml::RoutedEventArgs const&) noexcept {
                    Wake(weakShared, weakContext, revision, true);
                });
                watch.unloaded = element.Unloaded([weakShared, weakContext, revision, isSettings](
                                                      IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
                    if (auto live = weakContext.lock(); live && isSettings) live->settingsMounted = false;
                    Wake(weakShared, weakContext, revision, true);
                });
                if (property) {
                    watch.propertyToken = element.RegisterPropertyChangedCallback(
                        property, [weakShared, weakContext, revision](xaml::DependencyObject const&,
                                                                      xaml::DependencyProperty const&) noexcept {
                            Wake(weakShared, weakContext, revision, true);
                        });
                }
                context->watches.push_back(std::move(watch));
                context->watched.insert(identity);
            }

            if (isSettings || !uirender::ShouldDescend(probe)) return;  // the page's own tree is read when it mounts
            auto pushChild = [&](const xaml::DependencyObject& child) {
                if (!child) return true;
                parentOf[Identity(child)] = index;
                return push(child);
            };
            if (logical) {
                pushChild(logical);
                return;
            }
            int count = media::VisualTreeHelper::GetChildrenCount(node);
            for (int child = 0; child < count; child++) {
                if (!pushChild(media::VisualTreeHelper::GetChild(node, child))) break;
            }
        },
        kDiscoveryLimit);

    context->probe = std::move(nodes);
    context->probeObjects = std::move(objects);
    context->foundSettings = settings ? winrt::make_weak(settings) : winrt::weak_ref<xaml::FrameworkElement>{};
    context->foundMainSettings = mainSettings ? winrt::make_weak(mainSettings) : winrt::weak_ref<xaml::FrameworkElement>{};
    shared.Once("discover|" + WindowLabel(*context) + "|" + std::to_string(revision),
                "Interface: discovery of \"" + WindowLabel(*context) + "\": " +
                    std::to_string(context->probe.size()) + " node(s) walked, " + std::to_string(pages) +
                    " page(s), " + std::to_string(context->watches.size()) + " container(s) followed");
}

// ---- The Mods card in Settings > Playback ------------------------------------------------------------------------

// The page's own tree, depth first, parents before children (4000 nodes at most).
void CollectPage(const xaml::DependencyObject& page, std::vector<ProbeNode>& nodes,
                 std::vector<xaml::DependencyObject>& objects) {
    std::vector<std::pair<xaml::DependencyObject, int>> stack{{page, -1}};
    while (!stack.empty() && nodes.size() < kPageLimit) {
        auto [node, parent] = stack.back();
        stack.pop_back();
        int index = static_cast<int>(nodes.size());
        nodes.push_back(Describe(node, parent));
        objects.push_back(node);
        int count = media::VisualTreeHelper::GetChildrenCount(node);
        for (int child = count - 1; child >= 0; child--) stack.push_back({media::VisualTreeHelper::GetChild(node, child), index});
    }
}

void OpenKitFolder(const std::filesystem::path& kit) {
    ShellExecuteW(nullptr, L"open", kit.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

xaml::FrameworkElement ModRow(UiService::Shared& shared, const uirender::ModEntry& mod) {
    controls::Grid row;
    row.ColumnSpacing(16);
    controls::ColumnDefinition text;
    text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition toggleColumn;
    toggleColumn.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    row.ColumnDefinitions().Append(text);
    row.ColumnDefinitions().Append(toggleColumn);

    controls::StackPanel labels;
    labels.VerticalAlignment(xaml::VerticalAlignment::Center);
    // The manifest's translation for Windows' language, else its English name and description.
    const std::string& shownName = mod.NameIn(i18n::Language());
    const std::string& shownDescription = mod.DescriptionIn(i18n::Language());
    labels.Children().Append(MakeText(Wide(shownName).c_str(), "body", false, &shared.probe));
    if (!shownDescription.empty()) labels.Children().Append(MakeText(Wide(shownDescription).c_str(), "caption", true, &shared.probe));
    row.Children().Append(labels);

    controls::ToggleSwitch toggle;
    if (shared.probe.toggle) toggle.Style(shared.probe.toggle);
    toggle.MinWidth(0);
    // The states in Ampify's language (Apple Music's own words in French), not in whatever Windows would pick.
    toggle.OnContent(winrt::box_value(Wide(i18n::Text("ui.on"))));
    toggle.OffContent(winrt::box_value(Wide(i18n::Text("ui.off"))));
    toggle.VerticalAlignment(xaml::VerticalAlignment::Center);
    toggle.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    Name(toggle, "AMPIFY_ModsToggle_" + mod.id, shownName);
    automation::AutomationProperties::SetHelpText(toggle, Wide(i18n::Text("mods.toggleHelp")));
    g_modsUpdating++;
    toggle.IsOn(mod.enabled);
    g_modsUpdating--;
    controls::Grid::SetColumn(toggle, 1);
    row.Children().Append(toggle);

    Journal* journal = &shared.journal;
    std::filesystem::path kit = shared.kit;
    std::string id = mod.id;
    toggle.Toggled([journal, kit, id](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            if (g_modsUpdating > 0) return;
            auto control = sender.as<controls::ToggleSwitch>();
            bool on = control.IsOn();
            std::string error;
            if (uirender::SwitchMod(kit, id, on, error)) {
                journal->Write("Settings: extension \"" + id + "\" " + (on ? "turned on" : "turned off") +
                               " from the Ampify page (" + (on ? "start " : "stop ") + id + " requested)");
            } else {
                journal->Write("Settings: could not change extension \"" + id + "\": " + error);
                g_modsUpdating++;
                control.IsOn(!on);
                g_modsUpdating--;
            }
        } catch (...) {
            g_modsUpdating = 0;
        }
    });
    return row;
}

// Whose setting a control writes: an extension's, or the active theme's.
struct SettingOwner {
    std::string id;
    bool theme = false;
    bool snippet = false;
};

// Writes one declared setting from the card. An extension restarts with it (SetModSetting); the theme service sees
// settings.txt change and applies the theme and the snippets again (SetThemeSetting, SetSnippetSetting).
void CommitSetting(UiService::Shared& shared, const SettingOwner& owner, const std::string& key, const std::string& value) {
    std::string error;
    bool written = owner.snippet ? uirender::SetSnippetSetting(shared.kit, owner.id, key, value, error)
                   : owner.theme ? uirender::SetThemeSetting(shared.kit, owner.id, key, value, error)
                                 : uirender::SetModSetting(shared.kit, owner.id, key, value, error);
    if (written) {
        shared.journal.Write("Settings: \"" + owner.id + "\" " + key + " = " + value + " from the Ampify page" +
                             (owner.snippet ? std::string(" (snippet)")
                              : owner.theme ? std::string(" (theme)")
                                            : " (restart " + owner.id + " requested)"));
    } else {
        shared.journal.Write("Settings: could not change \"" + owner.id + "\" " + key + ": " + error);
    }
}

// A number typed in a field: the text when it reads as one within the bounds, else nothing. A comma counts as the
// decimal point, as French people type it.
std::optional<std::string> NumberIn(std::string text, const SettingSpec& spec) {
    std::replace(text.begin(), text.end(), ',', '.');
    double number = 0;
    const char* end = text.data() + text.size();
    auto [stop, error] = std::from_chars(text.data(), end, number);
    if (text.empty() || error != std::errc() || stop != end || !std::isfinite(number)) return std::nullopt;
    if ((spec.min && number < *spec.min) || (spec.max && number > *spec.max)) return std::nullopt;
    return SettingNumberText(number);
}

// A text or number field, written when it is left or on Enter. A number that does not read puts the last value back.
controls::TextBox SettingField(UiService::Shared& shared, const SettingOwner& target, const SettingSpec& spec,
                               const std::string& value) {
    controls::TextBox box;
    if (shared.probe.textBox) box.Style(shared.probe.textBox);
    box.MinWidth(spec.type == SettingSpec::Type::Number ? 96 : 200);
    box.MaxLength(200);  // the length docs/extensions/manifest.md allows a text default
    // settings.txt keeps a point; a French or German reader sees a comma, like the labels' "0,3 à 0,9".
    auto shown = [number = spec.type == SettingSpec::Type::Number](std::string text) {
        if (number && i18n::UsesDecimalComma()) std::replace(text.begin(), text.end(), '.', ',');
        return Wide(text);
    };
    box.Text(shown(value));
    auto last = std::make_shared<std::string>(value);
    UiService::Shared* owner = &shared;
    auto commit = [owner, target, spec, last, shown](const controls::TextBox& field) {
        std::string typed = Utf8(field.Text());
        std::optional<std::string> next = spec.type == SettingSpec::Type::Number ? NumberIn(typed, spec) : typed;
        if (!next) {
            field.Text(shown(*last));
            return;
        }
        if (*next == *last) return;
        *last = *next;
        CommitSetting(*owner, target, spec.key, *next);
    };
    box.LostFocus([commit](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            commit(sender.as<controls::TextBox>());
        } catch (...) {
        }
    });
    box.KeyDown([commit](IInspectable const& sender, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
        try {
            if (args.Key() == winrt::Windows::System::VirtualKey::Enter) commit(sender.as<controls::TextBox>());
        } catch (...) {
        }
    });
    return box;
}

// A number with both bounds: a slider, so the range shows, with its value written beside it. settings.txt is written
// once the slider rests, not at each step of a drag: a theme applies again on every write.
xaml::FrameworkElement SettingSlider(UiService::Shared& shared, const SettingOwner& target, const SettingSpec& spec,
                                     const std::string& value) {
    constexpr auto kRestDelay = std::chrono::milliseconds(350);
    constexpr double kSliderWidth = 200;
    const double low = *spec.min;
    const double high = *spec.max;
    const bool whole = std::floor(low) == low && std::floor(high) == high;
    std::optional<std::string> stored = NumberIn(value, spec);
    std::optional<std::string> fallback = NumberIn(spec.defaultValue, spec);
    std::string start = SettingNumberText(low);
    if (stored) {
        start = *stored;
    } else if (fallback) {
        start = *fallback;
    }

    controls::Slider slider;
    if (shared.probe.slider) slider.Style(shared.probe.slider);
    slider.Minimum(low);
    slider.Maximum(high);
    slider.StepFrequency(whole ? 1.0 : (high - low) / 100);
    slider.Width(kSliderWidth);
    slider.Value(std::strtod(start.c_str(), nullptr));
    slider.VerticalAlignment(xaml::VerticalAlignment::Center);

    // A point in settings.txt, a comma for a French or German reader, as in SettingField.
    auto shown = [](double number) {
        std::string text = SettingNumberText(number);
        if (i18n::UsesDecimalComma()) std::replace(text.begin(), text.end(), '.', ',');
        return Wide(text);
    };
    controls::TextBlock reading = MakeText(shown(slider.Value()).c_str(), "body", false, &shared.probe);
    reading.MinWidth(32);
    reading.TextAlignment(xaml::TextAlignment::Right);
    reading.VerticalAlignment(xaml::VerticalAlignment::Center);

    controls::StackPanel box;
    box.Orientation(controls::Orientation::Horizontal);
    box.Spacing(12);
    box.Children().Append(reading);
    box.Children().Append(slider);

    dispatching::DispatcherQueueTimer rest = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
    rest.Interval(kRestDelay);
    rest.IsRepeating(false);
    auto last = std::make_shared<std::string>(start);
    UiService::Shared* owner = &shared;
    winrt::weak_ref<controls::Slider> weakSlider = winrt::make_weak(slider);
    rest.Tick([owner, target, key = spec.key, last, weakSlider](auto const&, auto const&) noexcept {
        try {
            controls::Slider live = weakSlider.get();
            if (!live) return;
            std::string next = SettingNumberText(live.Value());
            if (next == *last) return;
            *last = next;
            CommitSetting(*owner, target, key, next);
        } catch (...) {
        }
    });
    slider.ValueChanged([rest, reading, shown](IInspectable const&,
                                               primitives::RangeBaseValueChangedEventArgs const& args) noexcept {
        try {
            reading.Text(shown(args.NewValue()));
            rest.Stop();
            rest.Start();
        } catch (...) {
        }
    });
    return box;
}

// The text of `key` in one list of the manifest's translation (`local`, null when there is none), else `fallback`.
const std::string& Localized(const ManifestText* local, std::map<std::string, std::string> ManifestText::*list,
                             const std::string& key, const std::string& fallback) {
    if (!local) return fallback;
    auto found = (local->*list).find(key);
    return found == (local->*list).end() ? fallback : found->second;
}

// `local` is the manifest's text in the interface's language, or null: a choice shows its options' translated texts
// and still stores their values.
xaml::FrameworkElement SettingControl(UiService::Shared& shared, const SettingOwner& target, const SettingSpec& spec,
                                      const std::string& value, const ManifestText* local) {
    UiService::Shared* owner = &shared;
    if (spec.type == SettingSpec::Type::Toggle) {
        controls::ToggleSwitch toggle;
        if (shared.probe.toggle) toggle.Style(shared.probe.toggle);
        toggle.MinWidth(0);
        toggle.OnContent(winrt::box_value(Wide(i18n::Text("ui.on"))));
        toggle.OffContent(winrt::box_value(Wide(i18n::Text("ui.off"))));
        toggle.IsOn(value == "true" || value == "1" || value == "on" || value == "yes");
        toggle.Toggled([owner, target, key = spec.key](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
            try {
                CommitSetting(*owner, target, key, sender.as<controls::ToggleSwitch>().IsOn() ? "true" : "false");
            } catch (...) {
            }
        });
        return toggle;
    }
    if (spec.type == SettingSpec::Type::Choice) {
        controls::ComboBox combo;
        if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
        combo.MinWidth(200);
        for (const std::string& option : spec.options) {
            combo.Items().Append(winrt::box_value(Wide(Localized(local, &ManifestText::options, option, option))));
        }
        auto chosen = std::find(spec.options.begin(), spec.options.end(), value);
        if (chosen == spec.options.end()) chosen = std::find(spec.options.begin(), spec.options.end(), spec.defaultValue);
        combo.SelectedIndex(static_cast<int32_t>(chosen - spec.options.begin()));
        combo.SelectionChanged([owner, target, key = spec.key, options = spec.options](
                                   IInspectable const& sender, controls::SelectionChangedEventArgs const&) noexcept {
            try {
                int32_t index = sender.as<controls::ComboBox>().SelectedIndex();
                if (index >= 0 && index < static_cast<int32_t>(options.size())) CommitSetting(*owner, target, key, options[index]);
            } catch (...) {
            }
        });
        return combo;
    }
    if (spec.type == SettingSpec::Type::Number && spec.min && spec.max && *spec.min < *spec.max) {
        return SettingSlider(shared, target, spec, value);
    }
    return SettingField(shared, target, spec, value);
}

// The settings an extension or the active theme declares, under its row: one line each, its label on the left and
// its control on the right.
xaml::FrameworkElement ModSettings(UiService::Shared& shared, const uirender::ModEntry& mod) {
    controls::StackPanel panel;
    panel.Spacing(8);
    panel.Margin(xaml::Thickness{16, 0, 0, 0});
    auto language = mod.translations.find(i18n::Language());
    const ManifestText* local = language == mod.translations.end() ? nullptr : &language->second;
    for (size_t index = 0; index < mod.settings.size() && index < mod.values.size(); index++) {
        const SettingSpec& spec = mod.settings[index];
        controls::Grid row;
        row.ColumnSpacing(16);
        controls::ColumnDefinition text;
        text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
        controls::ColumnDefinition control;
        control.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
        row.ColumnDefinitions().Append(text);
        row.ColumnDefinitions().Append(control);
        const std::string& shown = Localized(local, &ManifestText::settings, spec.key, spec.label);
        controls::TextBlock label = MakeText(Wide(shown).c_str(), "body", false, &shared.probe);
        label.VerticalAlignment(xaml::VerticalAlignment::Center);
        row.Children().Append(label);
        xaml::FrameworkElement input = SettingControl(shared, {mod.id, mod.theme, mod.snippet}, spec, mod.values[index], local);
        input.VerticalAlignment(xaml::VerticalAlignment::Center);
        input.HorizontalAlignment(xaml::HorizontalAlignment::Right);
        const char* prefix = mod.snippet ? "AMPIFY_SnippetSetting_" : mod.theme ? "AMPIFY_ThemeSetting_" : "AMPIFY_ModsSetting_";
        Name(input, std::string(prefix) + mod.id + "_" + spec.key,
             shown);
        controls::Grid::SetColumn(input, 1);
        row.Children().Append(input);
        panel.Children().Append(row);
    }
    return panel;
}

// The active theme's settings: its name, a line on what they do, then one row per setting.
xaml::FrameworkElement ThemeSettingsSection(UiService::Shared& shared, const uirender::ModEntry& theme) {
    controls::StackPanel section;
    section.Spacing(8);
    Name(section, (theme.snippet ? "AMPIFY_SnippetSettings_" : "AMPIFY_ThemeSettings_") + theme.id,
         theme.NameIn(i18n::Language()));
    controls::TextBlock title =
        MakeText(Wide(i18n::Text(theme.snippet ? "mods.snippetTitle" : "mods.themeTitle",
                                 {{"name", theme.NameIn(i18n::Language())}}))
                     .c_str(),
                 "bodyStrong", false, &shared.probe);
    section.Children().Append(title);
    section.Children().Append(MakeText(Wide(i18n::Text(theme.snippet ? "mods.snippetSubtitle" : "mods.themeSubtitle")).c_str(),
                                       "caption", true, &shared.probe));
    section.Children().Append(ModSettings(shared, theme));
    return section;
}

// A row of the card: a label and its help on the left, a control on the right.
controls::Grid LabelledRow(UiService::Shared& shared, const std::string& label, const std::string& help,
                           const xaml::FrameworkElement& control) {
    controls::Grid row;
    row.ColumnSpacing(16);
    controls::ColumnDefinition text;
    text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition right;
    right.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    row.ColumnDefinitions().Append(text);
    row.ColumnDefinitions().Append(right);
    controls::StackPanel titles;
    titles.VerticalAlignment(xaml::VerticalAlignment::Center);
    titles.Children().Append(MakeText(Wide(label).c_str(), "body", false, &shared.probe));
    titles.Children().Append(MakeText(Wide(help).c_str(), "caption", true, &shared.probe));
    row.Children().Append(titles);
    control.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::Grid::SetColumn(control, 1);
    row.Children().Append(control);
    return row;
}

// Shows the settings of the theme active now under the theme list, then those of the snippets switched on, or nothing
// when none declares any.
void FillThemeSettings(UiService::Shared& shared, const controls::StackPanel& host) {
    host.Children().Clear();
    if (std::optional<uirender::ModEntry> theme = uirender::ActiveThemeSettings(shared.kit)) {
        host.Children().Append(MakeDivider());
        host.Children().Append(ThemeSettingsSection(shared, *theme));
    }
    for (const uirender::ModEntry& snippet : uirender::EnabledSnippetSettings(shared.kit)) {
        host.Children().Append(MakeDivider());
        host.Children().Append(ThemeSettingsSection(shared, snippet));
    }
}

// Writes theme.accent ("" for the theme's own), which the theme engine follows like theme.scheme.
bool CommitAccent(UiService::Shared& shared, const std::string& accent) {
    std::string error;
    if (!uirender::ChooseAccent(shared.kit, accent, error)) {
        shared.journal.Write("Settings: could not change theme.accent: " + error);
        return false;
    }
    std::string written = uirender::ChosenAccent(shared.kit);
    shared.journal.Write("Settings: theme.accent = " + (written.empty() ? std::string("the theme's") : written) +
                         " from the Ampify page");
    return true;
}

// The hex field of a custom accent, written when it is left or on Enter. A colour that does not read puts the last
// one back.
controls::TextBox AccentField(UiService::Shared& shared, const std::string& value) {
    controls::TextBox box;
    if (shared.probe.textBox) box.Style(shared.probe.textBox);
    box.MinWidth(110);
    box.MaxLength(7);
    box.PlaceholderText(L"#RRGGBB");
    box.Text(Wide(value.empty() ? value : "#" + value));
    Name(box, "AMPIFY_AccentCustom", i18n::Text("accent.custom"));
    UiService::Shared* owner = &shared;
    auto commit = [owner](const controls::TextBox& field) {
        // Compared with settings.txt, not the field's last text: a preset may have been chosen since.
        std::string current = uirender::ChosenAccent(owner->kit);
        std::string typed = Utf8(field.Text());
        if (typed.empty() || typed == "#" + current) return;
        if (!CommitAccent(*owner, typed)) field.Text(Wide(current.empty() ? current : "#" + current));
    };
    box.LostFocus([commit](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            commit(sender.as<controls::TextBox>());
        } catch (...) {
        }
    });
    box.KeyDown([commit](IInspectable const& sender, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
        try {
            if (args.Key() == winrt::Windows::System::VirtualKey::Enter) commit(sender.as<controls::TextBox>());
        } catch (...) {
        }
    });
    return box;
}

// The accent over the theme's: the theme's own, a preset, or a colour typed in hex ("Custom…" shows the field).
xaml::FrameworkElement AccentChoice(UiService::Shared& shared) {
    std::string chosen = uirender::ChosenAccent(shared.kit);
    controls::ComboBox combo;
    if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
    combo.MinWidth(220);
    Name(combo, "AMPIFY_AccentChoice", i18n::Text("accent.title"));
    auto values = std::make_shared<std::vector<std::string>>(1, "");  // "" is the theme's own accent
    combo.Items().Append(winrt::box_value(Wide(i18n::Text("accent.theme"))));
    for (const uirender::AccentPreset& preset : uirender::AccentPresets()) {
        values->push_back(preset.value);
        combo.Items().Append(winrt::box_value(Wide(i18n::Text(preset.textKey))));
    }
    combo.Items().Append(winrt::box_value(Wide(i18n::Text("accent.custom"))));
    auto known = std::find(values->begin(), values->end(), chosen);
    int32_t customIndex = static_cast<int32_t>(values->size());
    combo.SelectedIndex(known == values->end() ? customIndex : static_cast<int32_t>(known - values->begin()));

    controls::TextBox custom = AccentField(shared, known == values->end() ? chosen : "");
    custom.Visibility(known == values->end() ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    UiService::Shared* owner = &shared;
    combo.SelectionChanged([owner, values, customIndex, custom](IInspectable const& sender,
                                                                controls::SelectionChangedEventArgs const&) noexcept {
        try {
            int32_t index = sender.as<controls::ComboBox>().SelectedIndex();
            if (index < 0 || index > customIndex) return;
            bool typed = index == customIndex;
            custom.Visibility(typed ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
            if (typed) {
                custom.Focus(xaml::FocusState::Programmatic);
            } else if ((*values)[index] != uirender::ChosenAccent(owner->kit)) {
                CommitAccent(*owner, (*values)[index]);
            }
        } catch (...) {
        }
    });
    controls::StackPanel control;
    control.Orientation(controls::Orientation::Horizontal);
    control.Spacing(8);
    control.Children().Append(custom);
    control.Children().Append(combo);
    return LabelledRow(shared, i18n::Text("accent.title"), i18n::Text("accent.help"), control);
}

// One choice of the card as a menu row, written to settings.txt when it changes (uirender::SetAppleTweak).
xaml::FrameworkElement TweakRow(UiService::Shared& shared, const uirender::TweakChoice& choice) {
    UiService::Shared* owner = &shared;
    controls::ComboBox combo;
    if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
    combo.MinWidth(220);
    auto values = std::make_shared<std::vector<std::string>>();
    for (const auto& [value, textKey] : choice.options) {
        values->push_back(value);
        combo.Items().Append(winrt::box_value(Wide(i18n::Text(textKey))));
    }
    auto chosen = std::find(values->begin(), values->end(), choice.value);
    combo.SelectedIndex(chosen == values->end() ? 0 : static_cast<int32_t>(chosen - values->begin()));
    Name(combo, "AMPIFY_Tweak_" + choice.key, i18n::Text(choice.labelKey));
    combo.SelectionChanged([owner, values, key = choice.key](IInspectable const& sender,
                                                             controls::SelectionChangedEventArgs const&) noexcept {
        try {
            int32_t index = sender.as<controls::ComboBox>().SelectedIndex();
            if (index < 0 || index >= static_cast<int32_t>(values->size())) return;
            std::string error;
            if (uirender::SetAppleTweak(owner->kit, key, (*values)[index], error)) {
                owner->journal.Write("Settings: " + key + " = " + (*values)[index] + " from the Ampify page");
            } else {
                owner->journal.Write("Settings: could not change " + key + ": " + error);
            }
        } catch (...) {
        }
    });
    return LabelledRow(shared, i18n::Text(choice.labelKey), i18n::Text(choice.helpKey), combo);
}

// The theme in use, chosen from the installed ones, then its settings. A choice is written to settings.txt, which the
// theme service watches: the theme changes at once, and its settings replace the last one's.
xaml::FrameworkElement ThemeChoice(UiService::Shared& shared) {
    controls::StackPanel section;
    section.Spacing(10);
    std::vector<uirender::ModEntry> themes = uirender::ListThemes(shared.kit);
    std::string active = uirender::ActiveThemeId(shared.kit);
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.cardTheme = active;
    }
    controls::ComboBox combo;
    if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
    combo.MinWidth(220);
    Name(combo, "AMPIFY_ThemeChoice", i18n::Text("mods.theme"));
    auto ids = std::make_shared<std::vector<std::string>>(1, "");  // "" is the choice "None"
    combo.Items().Append(winrt::box_value(Wide(i18n::Text("mods.themeNone"))));
    for (const uirender::ModEntry& theme : themes) {
        ids->push_back(theme.id);
        combo.Items().Append(winrt::box_value(Wide(theme.NameIn(i18n::Language()))));
    }
    auto chosen = std::find(ids->begin(), ids->end(), active);
    combo.SelectedIndex(chosen == ids->end() ? 0 : static_cast<int32_t>(chosen - ids->begin()));
    section.Children().Append(LabelledRow(shared, i18n::Text("mods.theme"), i18n::Text("mods.themeHelp"), combo));
    section.Children().Append(AccentChoice(shared));
    // Like the accent, the glass level holds for every theme and snippet, and the engine follows it within a tick.
    section.Children().Append(TweakRow(shared, uirender::GlassChoice(shared.kit)));

    controls::StackPanel settingsHost;
    settingsHost.Spacing(10);
    FillThemeSettings(shared, settingsHost);
    section.Children().Append(settingsHost);

    UiService::Shared* owner = &shared;
    combo.SelectionChanged([owner, ids, settingsHost](IInspectable const& sender,
                                                      controls::SelectionChangedEventArgs const&) noexcept {
        try {
            int32_t index = sender.as<controls::ComboBox>().SelectedIndex();
            if (index < 0 || index >= static_cast<int32_t>(ids->size())) return;
            const std::string& id = (*ids)[index];
            if (id == uirender::ActiveThemeId(owner->kit)) return;
            std::string error;
            {
                std::lock_guard<std::mutex> lock(owner->mutex);
                owner->cardTheme = id;  // the card already shows it: no rebuild
            }
            if (!uirender::ChooseTheme(owner->kit, id, error)) {
                owner->journal.Write("Settings: could not choose the theme \"" + id + "\": " + error);
                return;
            }
            owner->journal.Write("Settings: theme \"" + (id.empty() ? std::string("none") : id) +
                                 "\" chosen from the Ampify page");
            FillThemeSettings(*owner, settingsHost);
        } catch (...) {
        }
    });
    return section;
}

// Adds a row to the rows of a section's card, under a line when it is not the first.
void AddRow(const controls::StackPanel& rows, const xaml::UIElement& row) {
    if (rows.Children().Size() > 0) rows.Children().Append(MakeDivider());
    rows.Children().Append(row);
}

// One choice of the card as a menu row, written to settings.txt when it changes (uirender::SetAppleTweak).
void AddTweakRow(UiService::Shared& shared, const controls::StackPanel& rows, const uirender::TweakChoice& choice) {
    AddRow(rows, TweakRow(shared, choice));
}

// What Ampify changes in Apple Music's own behaviour (uirender::AppleTweakChoices): one choice per row. The kernel
// reads settings.txt again within a second (UiService::Run), so a choice applies at once.
void AddAppleTweakRows(UiService::Shared& shared, const controls::StackPanel& rows) {
    for (const uirender::TweakChoice& choice : uirender::AppleTweakChoices(shared.kit)) AddTweakRow(shared, rows, choice);
}

// A zip of the state and the logs with names, folders and music taken out (`ampify report`), opened in Explorer once
// written. The button says when it is done; the button is moved back to the interface thread before it is released.
xaml::FrameworkElement ReportRow(UiService::Shared& shared) {
    controls::Button report;
    if (shared.probe.button) report.Style(shared.probe.button);
    report.Content(winrt::box_value(Wide(i18n::Text("mods.report.run"))));
    report.VerticalAlignment(xaml::VerticalAlignment::Center);
    Name(report, "AMPIFY_ModsReport", i18n::Text("mods.report.run"));
    Journal* journal = &shared.journal;
    report.Click([journal](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            controls::Button button = sender.as<controls::Button>();
            button.IsEnabled(false);
            button.Content(winrt::box_value(Wide(i18n::Text("mods.report.running"))));
            dispatching::DispatcherQueue queue = dispatching::DispatcherQueue::GetForCurrentThread();
            auto held = std::make_shared<controls::Button>(button);
            RunAmpifyInBackground({"report", "--dir"}, [queue, held, journal](int code) mutable {
                journal->Write("Settings: report exported from the Ampify page (code " + std::to_string(code) + ")");
                queue.TryEnqueue([held = std::move(held), code]() noexcept {
                    try {
                        held->IsEnabled(true);
                        held->Content(winrt::box_value(Wide(i18n::Text(code == 0 ? "mods.report.done" : "mods.report.failed"))));
                    } catch (...) {
                    }
                });
            });
        } catch (...) {
        }
    });
    return LabelledRow(shared, i18n::Text("mods.report"), i18n::Text("mods.reportHelp"), report);
}

// A button with Apple's look, or the accent's for the main action of a page.
controls::Button MakeButton(UiService::Shared& shared, const std::string& textKey, const std::string& name, bool primary) {
    controls::Button button;
    if (shared.probe.button) button.Style(shared.probe.button);
    if (primary) {
        if (controls::ControlTemplate accent = PrimaryButtonTemplate()) button.Template(accent);
    }
    button.Content(winrt::box_value(Wide(i18n::Text(textKey))));
    button.VerticalAlignment(xaml::VerticalAlignment::Center);
    Name(button, name, i18n::Text(textKey));
    return button;
}

// A section of the Ampify page, as Apple groups its own settings: a heading, a line on what it holds, then one card
// whose rows are split by lines.
controls::StackPanel Section(UiService::Shared& shared, const std::string& name, const std::string& titleKey,
                             const std::string& subtitleKey, const controls::StackPanel& rows) {
    controls::StackPanel section;
    section.Spacing(4);
    const std::string title = i18n::Text(titleKey);
    Name(section, "AMPIFY_Settings_" + name, title);
    controls::TextBlock heading = MakeText(Wide(title).c_str(), "subtitle", false, nullptr);
    automation::AutomationProperties::SetHeadingLevel(heading, automation::Peers::AutomationHeadingLevel::Level2);
    section.Children().Append(heading);
    if (!subtitleKey.empty()) {
        section.Children().Append(MakeText(Wide(i18n::Text(subtitleKey)).c_str(), "caption", true, &shared.probe));
    }
    controls::Border card = MakeCardBorder(shared.probe);
    card.Margin(xaml::Thickness{0, 8, 0, 0});
    rows.Spacing(12);
    card.Child(rows);
    section.Children().Append(card);
    return section;
}

// One row per installed extension, its settings under it.
controls::StackPanel ExtensionRows(UiService::Shared& shared) {
    controls::StackPanel rows;
    std::vector<uirender::ModEntry> mods = uirender::ListMods(shared.kit);
    if (mods.empty()) rows.Children().Append(MakeText(Wide(i18n::Text("mods.none")).c_str(), "caption", true, &shared.probe));
    for (const uirender::ModEntry& mod : mods) {
        controls::StackPanel item;
        item.Spacing(10);
        item.Children().Append(ModRow(shared, mod));
        if (!mod.settings.empty()) item.Children().Append(ModSettings(shared, mod));
        AddRow(rows, item);
    }
    return rows;
}

// The Atmos leveler is built into the kernel, so no manifest brings its row: it is written here, in words a beginner
// reads, and switched like an extension (uirender::SwitchMod).
controls::StackPanel SoundRows(UiService::Shared& shared) {
    uirender::ModEntry leveler;
    leveler.id = kAtmosLeveler;
    leveler.name = i18n::Text("sound.leveler");
    leveler.description = i18n::Text("sound.levelerHelp");
    leveler.enabled = uirender::IsModOn(shared.kit, kAtmosLeveler);
    controls::StackPanel rows;
    AddRow(rows, ModRow(shared, leveler));
    return rows;
}

xaml::FrameworkElement FolderRow(UiService::Shared& shared) {
    controls::Button open = MakeButton(shared, "mods.openFolder", "AMPIFY_ModsOpenFolder", false);
    std::filesystem::path kit = shared.kit;
    open.Click([kit](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            OpenKitFolder(kit);
        } catch (...) {
        }
    });
    return LabelledRow(shared, i18n::Text("mods.folder"), i18n::Text("mods.folderHelp"), open);
}

// Ampify's settings page: a line on what Ampify is and the way to the Marketplace, then one section per subject.
controls::StackPanel BuildAmpifyPage(UiService::Shared& shared) {
    controls::StackPanel page;
    page.Spacing(28);
    page.MaxWidth(kAmpifyPageWidth);
    page.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    page.Padding(xaml::Thickness{0, 0, 0, 24});
    Name(page, "AMPIFY_SettingsPage", "Ampify");

    controls::Grid intro;
    intro.ColumnSpacing(16);
    controls::ColumnDefinition text;
    text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition action;
    action.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    intro.ColumnDefinitions().Append(text);
    intro.ColumnDefinitions().Append(action);
    controls::TextBlock line = MakeText(Wide(i18n::Text("mods.subtitle")).c_str(), "body", true, nullptr);
    line.VerticalAlignment(xaml::VerticalAlignment::Center);
    intro.Children().Append(line);
    controls::Button market = MakeButton(shared, "mods.openMarketplace", "AMPIFY_ModsOpenMarketplace", true);
    UiModel* model = &shared.model;
    Journal* journal = &shared.journal;
    market.Click([model, journal](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            model->Navigate("marketplace", "/marketplace");
            journal->Write("Settings: Marketplace opened from the Ampify page");
        } catch (...) {
        }
    });
    controls::Grid::SetColumn(market, 1);
    intro.Children().Append(market);
    page.Children().Append(intro);

    page.Children().Append(Section(shared, "Extensions", "mods.extensions", "mods.extensionsHelp", ExtensionRows(shared)));
    page.Children().Append(Section(shared, "Sound", "sound.title", "", SoundRows(shared)));
    controls::StackPanel lookRows;
    lookRows.Children().Append(ThemeChoice(shared));
    page.Children().Append(Section(shared, "Look", "mods.look", "", lookRows));
    controls::StackPanel appleRows;
    AddAppleTweakRows(shared, appleRows);
    page.Children().Append(Section(shared, "AppleMusic", "tweaks.title", "tweaks.subtitle", appleRows));
    controls::StackPanel helpRows;
    AddTweakRow(shared, helpRows, uirender::KitUpdateChoice(shared.kit));
    AddTweakRow(shared, helpRows, uirender::EntryChoice(shared.kit));
    AddRow(helpRows, FolderRow(shared));
    AddRow(helpRows, ReportRow(shared));
    page.Children().Append(Section(shared, "Help", "mods.help", "", helpRows));
    return page;
}

// Copies the look of Apple's controls from Settings > Playback (switches, menus, buttons, texts) once that page was
// seen, so the Ampify page reads like Apple's settings. Nothing is added to Apple's page. False until its rows are
// laid out, a few layout passes after Loaded.
bool CaptureSettingsStyle(UiService::Shared& shared, const xaml::FrameworkElement& page) {
    StyleProbe probe;
    CaptureStyleProbe(page, probe, "PlaybackSettingsPage");
    if (probe.Found() == 0) return false;
    shared.probe = probe;
    shared.Once("settings-style", "Settings: the look of Apple's settings copied from the Playback page (" +
                                      std::to_string(probe.Found()) + " native style value(s))");
    return true;
}

void WatchSettingsPage(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (shared.cardStale) context->ampifyPageStale = true;
        shared.cardStale = false;
    }
    xaml::FrameworkElement page = context->foundSettings.get();
    xaml::FrameworkElement current = context->settingsPage.get();
    if (page != current) {
        if (current) {
            try {
                current.LayoutUpdated(context->settingsLayout);
            } catch (...) {
            }
        }
        context->settingsPage = page ? winrt::make_weak(page) : winrt::weak_ref<xaml::FrameworkElement>{};
        context->settingsMounted = false;
        if (page) {
            WeakShared weakShared = shared.weak_from_this();
            WeakContext weakContext = context;
            std::uint64_t revision = context->rootRevision.load();
            // LayoutUpdated until the page is mounted: its rows appear a few layout passes after Loaded.
            context->settingsLayout = page.LayoutUpdated([weakShared, weakContext, revision](IInspectable const&,
                                                                                            IInspectable const&) noexcept {
                try {
                    auto live = weakContext.lock();
                    if (live && !live->settingsMounted && live->ticket.OnLayout(NowMs())) {
                        Wake(weakShared, weakContext, revision, false);
                    }
                } catch (...) {
                }
            });
            context->ticket.ArmLayout(NowMs());
            context->settingsSeenMs = NowMs();
            shared.journal.Write("Settings: PlaybackSettingsPage seen, waiting for it to load");
        }
    }
    if (page && !context->settingsMounted && page.IsLoaded()) {
        if (CaptureSettingsStyle(shared, page)) {
            context->settingsMounted = true;
            context->ticket.StopLayout();
        }
    }
}

// ---- The Ampify row at the top of Settings -----------------------------------------------------------------------

// Ampify's logo, the puzzle piece of the sidebar entry (apps/marketplace/main.js).
constexpr char kAmpifyIcon[] =
    "<svg viewBox=\"0 0 64 64\"><path d=\"M12.3 13L19 13C21.5 13 22.25 11.25 20.75 9.25C17.75 5 20.5 .75 26 .75"
    "C31.5 .75 34.25 5 31.25 9.25C29.75 11.25 30.5 13 33 13L39.7 13A11.3 11.3 0 0 1 51 24.3L51 31C51 33.5 52.75 34.25"
    " 54.75 32.75C59 29.75 63.25 32.5 63.25 38C63.25 43.5 59 46.25 54.75 43.25C52.75 41.75 51 42.5 51 45L51 51.7"
    "A11.3 11.3 0 0 1 39.7 63L12.3 63A11.3 11.3 0 0 1 1 51.7L1 24.3A11.3 11.3 0 0 1 12.3 13Z\"/></svg>";

// The index of the TextBlock named `name` (the title of one of Apple's rows, such as "PlaybackNavigationViewItem").
std::optional<int> FindNamed(const std::vector<ProbeNode>& nodes, std::string_view name) {
    for (std::size_t index = 0; index < nodes.size(); index++) {
        if (nodes[index].name == name) return static_cast<int>(index);
    }
    return std::nullopt;
}

// The Button of Apple's row whose title is the node at `title`.
controls::Button RowOf(const std::vector<ProbeNode>& nodes, const std::vector<xaml::DependencyObject>& objects,
                       std::optional<int> title) {
    if (!title) return nullptr;
    for (int parent = nodes[*title].parent; parent >= 0; parent = nodes[parent].parent) {
        if (auto button = objects[parent].try_as<controls::Button>()) return button;
    }
    return nullptr;
}

// A TextBlock that reads like `model`, Apple's title or description of a row.
controls::TextBlock TextLike(const controls::TextBlock& model, const std::string& text) {
    controls::TextBlock block;
    block.Text(Wide(text));
    block.TextWrapping(xaml::TextWrapping::WrapWholeWords);
    if (!model) return block;
    block.FontFamily(model.FontFamily());
    block.FontSize(model.FontSize());
    block.FontWeight(model.FontWeight());
    block.Foreground(model.Foreground());
    block.Opacity(model.Opacity());  // Apple dims its descriptions by opacity (0.6), not by their brush
    block.CharacterSpacing(model.CharacterSpacing());
    block.Margin(model.Margin());
    return block;
}

// A row like Playback's (Apple's style, so the chevron, the hover and the press come with it). A click opens Ampify's
// own settings page over Settings, as Apple's rows open theirs; Apple's back arrow or the page's own closes it.
controls::Button BuildSettingsEntry(UiService::Shared& shared, const controls::Button& playback,
                                    const controls::TextBlock& titleModel, const controls::TextBlock& textModel) {
    controls::Grid content;
    controls::ColumnDefinition iconColumn;
    iconColumn.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    controls::ColumnDefinition textColumn;
    textColumn.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    content.ColumnDefinitions().Append(iconColumn);
    content.ColumnDefinitions().Append(textColumn);
    xaml::FrameworkElement icon = MakeIconElement(kAmpifyIcon, 16, L"TextFillColorPrimaryBrush");
    if (icon) {
        // Lines the icon and the title up with Apple's rows (icon centred 16 px in, title 48 px in).
        icon.Margin(xaml::Thickness{8, 0, 24, 0});
        icon.VerticalAlignment(xaml::VerticalAlignment::Center);
        content.Children().Append(icon);
    }
    controls::StackPanel texts;
    texts.Padding(xaml::Thickness{0, 12, 0, 12});
    texts.Children().Append(TextLike(titleModel, "Ampify"));
    texts.Children().Append(TextLike(textModel, i18n::Text("settings.entryHelp")));
    controls::Grid::SetColumn(texts, 1);
    content.Children().Append(texts);

    controls::Button entry;
    entry.Style(playback.Style());
    entry.Margin(playback.Margin());
    entry.Padding(playback.Padding());
    entry.HorizontalAlignment(playback.HorizontalAlignment());
    entry.HorizontalContentAlignment(playback.HorizontalContentAlignment());
    entry.VerticalContentAlignment(playback.VerticalContentAlignment());
    entry.Background(playback.Background());
    entry.BorderBrush(playback.BorderBrush());
    entry.BorderThickness(playback.BorderThickness());
    entry.CornerRadius(playback.CornerRadius());
    entry.Content(content);
    Name(entry, "AMPIFY_SettingsEntry", "Ampify");
    automation::AutomationProperties::SetHelpText(entry, Wide(i18n::Text("settings.entryHelp")));

    UiModel* model = &shared.model;
    Journal* journal = &shared.journal;
    entry.Click([model, journal](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            model->Navigate(kAmpifyPageOwner, kAmpifyPagePath);
            journal->Write("Settings: Ampify page opened from the Ampify row");
        } catch (...) {
        }
    });
    return entry;
}

// Puts the Ampify row after Apple's last row (Advanced) of the Settings page, once per page.
void WatchSettingsEntry(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    xaml::FrameworkElement page = context->foundMainSettings.get();
    if (!page || !page.IsLoaded()) return;
    if (context->entryPage.get() == page && context->settingsEntry && context->settingsEntry.Parent()) return;

    std::vector<ProbeNode> nodes;
    std::vector<xaml::DependencyObject> objects;
    CollectPage(page, nodes, objects);
    std::optional<int> playbackTitle = FindNamed(nodes, "PlaybackNavigationViewItem");
    controls::Button playback = RowOf(nodes, objects, playbackTitle);
    if (!playback) return;  // its rows appear a few layout passes after Loaded: the next refresh tries again
    auto panel = media::VisualTreeHelper::GetParent(playback).try_as<controls::Panel>();
    if (!panel) {
        shared.Once("settings-entry-panel", "Settings: Apple's rows are not in a panel, Ampify row not added");
        return;
    }

    controls::TextBlock titleModel = objects[*playbackTitle].try_as<controls::TextBlock>();
    controls::TextBlock textModel{nullptr};
    if (auto texts = media::VisualTreeHelper::GetParent(titleModel).try_as<controls::Panel>(); texts && texts.Children().Size() > 1) {
        textModel = texts.Children().GetAt(1).try_as<controls::TextBlock>();
    }
    RemoveFromParent(context->settingsEntry);
    context->settingsEntry = BuildSettingsEntry(shared, playback, titleModel, textModel);

    auto children = panel.Children();
    std::uint32_t at = children.Size();
    controls::Button last = RowOf(nodes, objects, FindNamed(nodes, "AdvancedNavigationViewItem"));
    std::uint32_t index = 0;
    if (last && children.IndexOf(last, index)) at = index + 1;
    children.InsertAt(at, context->settingsEntry);
    context->entryPage = winrt::make_weak(page);
    shared.journal.Write("Settings: row \"Ampify\" added to the Settings page");
}

// ---- Panels, pages, popups ---------------------------------------------------------------------------------------

// The frame of a panel: a flyout-like surface with a title and a close button, and a slot for the rendered tree.
controls::Border MakeSurfaceFrame(const std::string& frameName, const std::string& title,
                                  controls::TextBlock& titleBlock, controls::Border& slot, controls::Button& close) {
    controls::Border frame = LoadKitXaml(L"<Border " + std::wstring(KitXmlns()) +
                                         L" Background='{ThemeResource AcrylicInAppFillColorDefaultBrush}'"
                                         L" BorderBrush='{ThemeResource SurfaceStrokeColorFlyoutBrush}'"
                                         L" BorderThickness='1' CornerRadius='8' Padding='16,12,16,16'/>")
                                 .try_as<controls::Border>();
    if (!frame) {
        frame = controls::Border();
        frame.Background(ThemeResource(L"SolidBackgroundFillColorBaseBrush").try_as<media::Brush>());
        frame.BorderBrush(ThemeResource(L"CardStrokeColorDefaultBrush").try_as<media::Brush>());
        frame.BorderThickness(xaml::Thickness{1, 1, 1, 1});
        frame.CornerRadius(xaml::CornerRadius{8, 8, 8, 8});
        frame.Padding(xaml::Thickness{16, 12, 16, 16});
    }
    frame.Name(Wide(frameName));

    controls::StackPanel body;
    body.Spacing(12);
    controls::Grid header;
    controls::ColumnDefinition left;
    left.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition right;
    right.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    header.ColumnDefinitions().Append(left);
    header.ColumnDefinitions().Append(right);
    titleBlock = MakeText(Wide(title).c_str(), "subtitle", false);
    titleBlock.VerticalAlignment(xaml::VerticalAlignment::Center);
    header.Children().Append(titleBlock);
    close = controls::Button();
    controls::FontIcon cross;
    cross.Glyph(L"\xE711");  // Cancel, in Segoe Fluent Icons
    cross.FontSize(12);
    close.Content(cross);
    close.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
    close.BorderThickness(xaml::Thickness{0, 0, 0, 0});
    close.VerticalAlignment(xaml::VerticalAlignment::Center);
    Name(close, frameName + "_close", i18n::Text("ui.close"));
    controls::ToolTipService::SetToolTip(close, winrt::box_value(Wide(i18n::Text("ui.close"))));
    controls::Grid::SetColumn(close, 1);
    header.Children().Append(close);
    body.Children().Append(header);
    slot = controls::Border();
    body.Children().Append(slot);
    frame.Child(body);
    return frame;
}

// The part of the window where Apple Music shows its pages, in the window's coordinates. A theme may move the play bar
// and the panels (to the bottom, as floating panels), so the frame of the pages comes first; the names the discovery
// found are the fallback (Apple Music 1.1792: Grid#TransportBar at the top, Grid#PaneRoot on the left).
struct ContentArea {
    double left = 0;
    double top = 56;
    double width = 0;   // 0: up to the right edge of the window
    double height = 0;  // 0: down to the bottom edge of the window
};

// Where a named element of Apple Music's window lies, in the window's coordinates; nothing while it is not laid out.
std::optional<winrt::Windows::Foundation::Rect> NamedBounds(const WindowContext& context, std::string_view name) {
    std::optional<int> index = uirender::FindByName(context.probe, name);
    if (!index || *index >= static_cast<int>(context.probeObjects.size())) return std::nullopt;
    auto element = context.probeObjects[*index].get().try_as<xaml::FrameworkElement>();
    if (!element || element.ActualWidth() <= 0 || element.ActualHeight() <= 0) return std::nullopt;
    auto origin = element.TransformToVisual(nullptr).TransformPoint({0, 0});
    return winrt::Windows::Foundation::Rect{origin.X, origin.Y, static_cast<float>(element.ActualWidth()),
                                            static_cast<float>(element.ActualHeight())};
}

// The bottom of the strip at the window's top that holds its buttons and takes the drags, 0 when it is not found.
double TitlebarBottom(const WindowContext& context) noexcept {
    try {
        if (auto titlebar = NamedBounds(context, "Titlebar")) return titlebar->Y + titlebar->Height;
    } catch (...) {
        // Leaving the tree while it is measured: the next refresh places the panels again.
    }
    return 0;
}

ContentArea FindContentArea(const WindowContext& context, double windowHeight) {
    ContentArea area;
    auto bounds = [&context](std::string_view name) { return NamedBounds(context, name); };
    try {
        std::optional<winrt::Windows::Foundation::Rect> bar = bounds("TransportBar");
        if (auto frame = bounds("ContentFrame")) {
            area = {frame->X, frame->Y, frame->Width, frame->Height};
        } else {
            area.top = 0;
            if (auto pane = bounds("PaneRoot")) area.left = std::max(0.0f, pane->X + pane->Width);
        }
        // Apple Music's pages run under its play bar, which is see-through: the host stops at the bar, above or below.
        if (bar && bar->Y < windowHeight / 2) {
            double barBottom = bar->Y + bar->Height;
            if (area.top < barBottom) {
                if (area.height > 0) area.height = std::max(0.0, area.height - (barBottom - area.top));
                area.top = barBottom;
            }
        } else if (bar) {
            double pagesBottom = area.height > 0 ? area.top + area.height : windowHeight;
            if (pagesBottom > bar->Y) area.height = std::max(0.0, static_cast<double>(bar->Y) - area.top);
        }
    } catch (...) {
        // An element leaving the tree while it is measured: the defaults hold until the next refresh.
    }
    return area;
}

// Places the Popup of the panels on the right of the window, under the play bar. Called on every refresh and when
// the window changes size, so the panels follow it.
// Opens one of the overlays under the fullscreen view, and counts it so the view goes back on top (ApplySurfaces).
void OpenOverlay(WindowContext& context, const primitives::Popup& popup) {
    if (popup.IsOpen()) return;
    popup.IsOpen(true);
    context.overlayOpens++;
}

// Docked, the column is the card: each panel's own card (MakeSurfaceFrame) loses its background, border and padding,
// so a panel reads as part of the column. Floating again, the cards come back.
void DressPanelFrames(WindowContext& context, bool docked) {
    if (!context.panelStack) return;
    xaml::Thickness border = docked ? xaml::Thickness{} : xaml::Thickness{1, 1, 1, 1};
    xaml::Thickness padding = docked ? xaml::Thickness{} : xaml::Thickness{16, 12, 16, 16};
    for (const xaml::UIElement& child : context.panelStack.Children()) {
        auto frame = child.try_as<controls::Border>();
        if (!frame || frame.BorderThickness() == border) continue;
        frame.BorderThickness(border);
        frame.Padding(padding);
        if (docked) {
            frame.Background(media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        } else if (auto brush = ThemeResource(L"AcrylicInAppFillColorDefaultBrush").try_as<media::Brush>()) {
            frame.Background(brush);
        }
    }
}

// A theme that leaves room on the right of the pages, a right margin on .content as wide as a panel and two gaps, gets
// the panels docked there: a column of its own, as high as the pages, with their background and corners. Without the
// room, the panels float over the right of the pages.
// The grid that holds the pages, whose padding makes the room for the panels (PushPages): themes write the pages' own
// margin, and write it again while they run, so a margin Ampify added was undone. They never write this padding.
controls::Grid PagesParent(const controls::Grid& pages) {
    return pages ? media::VisualTreeHelper::GetParent(pages).try_as<controls::Grid>() : nullptr;
}

// The room on the right of the pages: their margin and their parent's padding.
double RoomRight(const controls::Grid& pages) {
    controls::Grid parent = PagesParent(pages);
    return pages.Margin().Right + (parent ? parent.Padding().Right : 0);
}

bool DockPanelPopup(WindowContext& context, const winrt::Windows::Foundation::Size& size) {
    // Apple has more than one Grid#ContentRoot: the pages are the one given the room on the right.
    controls::Grid pages{nullptr};
    for (const auto& object : context.probeObjects) {
        auto grid = object.get().try_as<controls::Grid>();
        if (!grid || grid.Name() != L"ContentRoot" || grid.ActualWidth() <= 0) continue;
        if (RoomRight(grid) >= kPanelWidth) {
            pages = grid;
            break;
        }
    }
    if (!pages) return false;
    auto pagesOrigin = pages.TransformToVisual(nullptr).TransformPoint({0, 0});
    winrt::Windows::Foundation::Rect bounds{pagesOrigin.X, pagesOrigin.Y, static_cast<float>(pages.ActualWidth()),
                                            static_cast<float>(pages.ActualHeight())};
    // Without a theme, Apple's pages run under its see-through play bar at the window's top: the column starts a gap
    // below the bar instead of over its buttons.
    constexpr float kBarGap = 8;
    if (auto bar = NamedBounds(context, "TransportBar"); bar && bar->Y < size.Height / 2) {
        float top = bar->Y + bar->Height + kBarGap;
        if (bounds.Y < top) {
            bounds.Height = std::max(0.0f, bounds.Height - (top - bounds.Y) - kBarGap);
            bounds.Y = top;
        }
    }
    // A theme's room is the pages' margin: Apple's lyrics and queue pane, when open, starts right after it, and the
    // pages' parent runs under that pane, so measuring up to the parent's edge would put the column over the pane.
    // Ampify's own room is the parent's padding (PushPages): that pane, a child of the same grid, opens inside it,
    // between the pages and the room, so the column takes the parent's right edge.
    double start = bounds.X + bounds.Width;
    double room = std::min(RoomRight(pages), static_cast<double>(size.Width) - start);
    controls::Grid parent = PagesParent(pages);
    if (double padding = parent ? parent.Padding().Right : 0; padding >= kPanelWidth + 8) {
        double parentRight = parent.TransformToVisual(nullptr).TransformPoint({0, 0}).X + parent.ActualWidth();
        room = std::min(padding, static_cast<double>(size.Width) - (parentRight - padding));
        start = parentRight - padding;
    }
    if (room < kPanelWidth + 8) return false;
    double gap = (room - kPanelWidth) / 2;
    double width = kPanelWidth;
    if (context.panelHost.Width() != width) context.panelHost.Width(width);
    if (context.panelHost.Height() != bounds.Height) context.panelHost.Height(bounds.Height);
    double left = start + gap;
    if (context.panelPopup.HorizontalOffset() != left) context.panelPopup.HorizontalOffset(left);
    if (context.panelPopup.VerticalOffset() != bounds.Y) context.panelPopup.VerticalOffset(bounds.Y);
    media::Brush background = pages.Background();
    if (context.panelHost.Background() != background) context.panelHost.Background(background);
    xaml::CornerRadius corners = pages.CornerRadius();
    xaml::CornerRadius held = context.panelHost.CornerRadius();
    if (held.TopLeft != corners.TopLeft || held.BottomRight != corners.BottomRight) context.panelHost.CornerRadius(corners);
    if (!context.panelDocked) context.panelHost.Padding(xaml::Thickness{16, 16, 16, 16});
    context.panelDocked = true;
    DressPanelFrames(context, true);
    return true;
}

constexpr double kPushGap = 8;  // between the pages and the panels they make room for

// A panel on its way out no longer takes clicks (SlidePanelOut): the pages need room only for the others.
bool AnyPanelShown(const WindowContext& context) {
    if (!context.panelStack) return false;
    for (const xaml::UIElement& child : context.panelStack.Children()) {
        if (child.IsHitTestVisible()) return true;
    }
    return false;
}

// The grid of Apple's pages: the Grid#ContentRoot that holds Frame#ContentFrame (Apple has more than one ContentRoot).
controls::Grid PagesGrid(const WindowContext& context) {
    std::optional<int> index = uirender::FindByName(context.probe, "ContentFrame");
    if (!index || *index >= static_cast<int>(context.probeObjects.size())) return nullptr;
    xaml::DependencyObject node = context.probeObjects[*index].get();
    for (int up = 0; node && up < 12; up++) {
        auto grid = node.try_as<controls::Grid>();
        if (grid && grid.Name() == L"ContentRoot") return grid;
        node = media::VisualTreeHelper::GetParent(node);
    }
    return nullptr;
}

// Panels take a column of their own instead of floating over the pages: the room on the pages' right grows by a panel
// and two gaps, and DockPanelPopup docks them there.
void PushPages(WindowContext& context) {
    if (context.pushedPages) {
        if (context.pushedPages.Padding().Right == context.pushedRight) return;
    }
    controls::Grid pages = PagesGrid(context);
    if (!pages) return;
    if (!context.pushedPages && pages.Margin().Right >= kPanelWidth) return;  // the theme left a column already
    controls::Grid parent = context.pushedPages ? context.pushedPages : PagesParent(pages);
    if (!parent) return;
    xaml::Thickness padding = parent.Padding();
    if (!context.pushedPages) context.pagesPadding = padding;
    context.pushedPages = parent;
    context.pushedRight = context.pagesPadding.Right + kPanelWidth + 2 * kPushGap;
    padding.Right = context.pushedRight;
    parent.Padding(padding);
    parent.UpdateLayout();  // so the column docks at once rather than floating for one refresh
}

// The pages take their width back once no panel shows.
void ReleasePages(WindowContext& context) {
    if (!context.pushedPages) return;
    if (context.pushedPages.Padding().Right == context.pushedRight) context.pushedPages.Padding(context.pagesPadding);
    context.pushedPages = nullptr;
    context.pushedRight = -1;
}

constexpr double kPagesMinWidth = 720;  // narrower, a playlist's header cut its buttons: the panels float instead
constexpr double kDockMargin = 8;       // room past that before the pages make room again: no back and forth

// Whether the pages keep kPagesMinWidth beside a column of panels, measured on the pages as they are, pushed or not.
bool PagesKeepRoom(const WindowContext& context) {
    controls::Grid pages = PagesGrid(context);
    if (!pages || pages.ActualWidth() <= 0) return true;
    double width = pages.ActualWidth();
    if (context.pushedPages) return width >= kPagesMinWidth;
    return width - (kPanelWidth + 2 * kPushGap) >= kPagesMinWidth + kDockMargin;
}

void PlacePanelPopup(WindowContext& context, const xaml::XamlRoot& root) {
    auto size = root.Size();
    if (AnyPanelShown(context)) {
        if (PagesKeepRoom(context)) {
            PushPages(context);
        } else {
            ReleasePages(context);  // a narrow window: the panels float over the pages rather than squeeze them
        }
    }
    if (DockPanelPopup(context, size)) {
        OpenOverlay(context, context.panelPopup);
        return;
    }
    if (context.panelDocked) {
        context.panelDocked = false;
        context.panelHost.Width(kPanelWidth);
        context.panelHost.Background(nullptr);
        context.panelHost.CornerRadius(xaml::CornerRadius{});
        context.panelHost.Padding(xaml::Thickness{});
        DressPanelFrames(context, false);
    }
    ContentArea area = FindContentArea(context, size.Height);
    double bottom = area.height > 0 ? area.top + area.height : static_cast<double>(size.Height);
    double right = area.width > 0 ? area.left + area.width : static_cast<double>(size.Width);
    // With the play bar moved down by a theme, the pages reach the window's top, where the window's buttons are.
    double top = std::max(area.top, TitlebarBottom(context)) + 8;
    double height = std::max(200.0, bottom - top - 16.0);
    double left = std::max(area.left, right - kPanelWidth - 16.0);
    if (context.panelHost.Height() != height) context.panelHost.Height(height);
    if (context.panelPopup.HorizontalOffset() != left) context.panelPopup.HorizontalOffset(left);
    if (context.panelPopup.VerticalOffset() != top) context.panelPopup.VerticalOffset(top);
    OpenOverlay(context, context.panelPopup);
}

// The overlay of the panels, on the right of the main window: a Popup on the window's XamlRoot, which Apple Music's
// own pages never touch (AMExclusive inserts into the settings page only). Apple Music takes out the children it does
// not know from its own panels (Grid#PageRoot), so there is no fallback there: without a XamlRoot yet, nothing.
controls::StackPanel EnsurePanelHost(UiService::Shared& shared, WindowContext& context, const xaml::DependencyObject& content) {
    auto rootElement = content.try_as<xaml::UIElement>();
    xaml::XamlRoot xamlRoot = rootElement ? rootElement.XamlRoot() : nullptr;
    if (!xamlRoot) return nullptr;
    if (context.panelStack && context.panelHost && context.panelPopup) {
        PlacePanelPopup(context, xamlRoot);
        return context.panelStack;
    }
    if (context.panelHostsMade++ > 0 && context.panelHostsMade <= 4) {
        shared.journal.Write("Interface: panel host recreated (no. " + std::to_string(context.panelHostsMade) + ")");
    }
    controls::Grid host;
    Name(host, "AMPIFY_PanelHost", "Extension panels");
    host.Width(kPanelWidth);
    host.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    host.VerticalAlignment(xaml::VerticalAlignment::Stretch);
    controls::ScrollViewer scroll;
    scroll.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
    scroll.HorizontalScrollMode(controls::ScrollMode::Disabled);
    scroll.HorizontalScrollBarVisibility(controls::ScrollBarVisibility::Disabled);
    QuietScrollBar(scroll);  // as the page host's: out of sight until the pointer reaches it, without arrows
    Name(scroll, "AMPIFY_PanelScroll");
    controls::StackPanel stack;
    stack.Spacing(12);
    scroll.Content(stack);
    host.Children().Append(scroll);

    primitives::Popup popup;
    popup.XamlRoot(xamlRoot);
    popup.Child(host);
    context.panelPopup = popup;
    context.panelHost = host;
    context.panelStack = stack;
    PlacePanelPopup(context, xamlRoot);
    shared.Once("panel-host", "Interface: panel host opened in a Popup of the window, on the right (" +
                                  Utf8(winrt::get_class_name(content)) + ")");
    return stack;
}

std::string SurfaceLabel(const Surface& surface) {
    return surface.elementId + "/" + surface.id;
}

// Renders a surface into its slot; returns the stats for the log.
uirender::PatchStats RenderInto(UiService::Shared& shared, MountedSurface& mounted, const Surface& surface) {
    if (!mounted.renderer) mounted.renderer = std::make_unique<XamlRenderer>(surface.elementId, shared.sink);
    uirender::PatchStats stats;
    bool replaced = false;
    xaml::FrameworkElement root =
        mounted.renderer->Render(uirender::BuildRenderTree(surface.tree, surface.nodeId), shared.probe, stats, replaced);
    if (auto slot = mounted.slot) {
        if (replaced || slot.Child() != root) slot.Child(root);
    }
    if (auto title = mounted.title) {
        std::string text = surface.title.empty() ? surface.id : surface.title;
        if (Utf8(title.Text()) != text) title.Text(Wide(text));
    }
    mounted.surface = surface;
    return stats;
}

void JournalRender(UiService::Shared& shared, const char* what, const Surface& surface, const uirender::PatchStats& stats,
                   bool first) {
    if (first) {
        shared.journal.Write(std::string("Interface: ") + what + " \"" + SurfaceLabel(surface) + "\" mounted (" +
                             std::to_string(stats.created) + " element(s), AutomationId " +
                             uirender::ElementName(surface.elementId, surface.nodeId) + ")");
    } else if (stats.created > 0 || stats.replaced > 0 || stats.removed > 0) {
        // A text or a value that changed is the panel living (a clock, the lyrics): only a new shape goes to the log.
        shared.journal.Write(std::string("Interface: ") + what + " \"" + SurfaceLabel(surface) +
                             "\" updated in place (created " + std::to_string(stats.created) + ", changed " +
                             std::to_string(stats.updated) + ", rebuilt " + std::to_string(stats.replaced) +
                             ", removed " + std::to_string(stats.removed) + ")");
    }
}

constexpr float kPanelSlide = 48;  // how far a panel comes from, on the right
constexpr auto kPanelIn = std::chrono::milliseconds(280);
constexpr auto kPanelOut = std::chrono::milliseconds(200);  // leaving is quicker than coming

// A new panel comes in from the right and fades in, on the compositor thread, once it is in the window.
void SlidePanelIn(const controls::Border& frame) {
    frame.Translation({kPanelSlide, 0, 0});
    frame.Opacity(0);
    frame.Loaded([](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            auto loaded = sender.as<controls::Border>();
            if (loaded.Opacity() != 0) return;  // shown already: a frame put back after something took it out
            xaml::Vector3Transition slide;
            slide.Duration(kPanelIn);
            loaded.TranslationTransition(slide);
            xaml::ScalarTransition fade;
            fade.Duration(kPanelIn);
            loaded.OpacityTransition(fade);
            loaded.Translation({0, 0, 0});
            loaded.Opacity(1);
        } catch (...) {
        }
    });
}

// A panel taken out slides back to the right and fades, then leaves the column; the column itself goes when it is
// empty, so a theme's docked column does not stay behind as an empty card.
void SlidePanelOut(WindowContext& context, const xaml::FrameworkElement& frame) {
    if (!frame) return;
    auto timer = std::make_shared<winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer>(
        winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer());
    xaml::Vector3Transition slide;
    slide.Duration(kPanelOut);
    frame.TranslationTransition(slide);
    xaml::ScalarTransition fade;
    fade.Duration(kPanelOut);
    frame.OpacityTransition(fade);
    frame.Translation({kPanelSlide, 0, 0});
    frame.Opacity(0);
    frame.IsHitTestVisible(false);  // leaving (AnyPanelShown)
    if (!AnyPanelShown(context)) ReleasePages(context);
    controls::StackPanel stack = context.panelStack;
    primitives::Popup popup = context.panelPopup;
    timer->Interval(kPanelOut);
    timer->IsRepeating(false);
    timer->Tick([timer, frame, stack, popup](auto&&, auto&&) mutable noexcept {
        try {
            RemoveFromParent(frame);
            if (stack && stack.Children().Size() == 0 && popup && popup.IsOpen()) popup.IsOpen(false);
            timer->Stop();
        } catch (...) {
        }
        timer.reset();  // the timer held this handler, which held the timer
    });
    timer->Start();
}

void MountPanel(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                const xaml::DependencyObject& content, const Surface& surface) {
    if (context->closedPanels.count(surface.key)) return;
    MountedSurface& mounted = context->surfaces[surface.key];
    bool alive = mounted.panelFrame != nullptr;
    bool detached = alive && !mounted.panelFrame.Parent();
    if (alive && !detached && mounted.surface.revision == surface.revision) return;
    controls::StackPanel stack = EnsurePanelHost(shared, *context, content);
    if (!stack) return;
    if (detached) {
        // Something took the frame out of the host: put back, and said, so the log shows who does it and when.
        stack.Children().Append(mounted.panelFrame);
        if (shared.detachedReports++ < 5) {
            shared.journal.Write("Interface: panel \"" + SurfaceLabel(surface) + "\" taken out of its host, put back");
        }
    }
    bool first = !alive;
    if (first) {
        controls::TextBlock title{nullptr};
        controls::Border slot{nullptr};
        controls::Button close{nullptr};
        std::string frameName = uirender::ElementName(surface.elementId, surface.nodeId) + "_panel";
        controls::Border frame = MakeSurfaceFrame(frameName, surface.title.empty() ? surface.id : surface.title, title, slot, close);
        WeakContext weakContext = context;
        WeakShared weakShared = shared.weak_from_this();
        std::string key = surface.key;
        std::string label = SurfaceLabel(surface);
        Journal* journal = &shared.journal;
        // The element is told (entry "panelClose"): its panel leaves its model, so its button can show it again with
        // one click, and its React root unmounts. Until the model drops it, closedPanels keeps it from coming back.
        UiEvent closed{surface.elementId, 0, false, "{\"panel\":" + Json::Quote(surface.id) + "}", "panelClose"};
        close.Click([weakContext, weakShared, key, label, journal, closed](IInspectable const& sender,
                                                                         xaml::RoutedEventArgs const&) noexcept {
            try {
                auto live = weakContext.lock();
                if (!live) return;
                if (SharedPtr owner = weakShared.lock(); owner && owner->sink) owner->sink(closed);
                live->closedPanels.insert(key);
                auto found = live->surfaces.find(key);
                if (found != live->surfaces.end()) {
                    RemoveFromParent(found->second.panelFrame);
                    if (!AnyPanelShown(*live)) {
                        ReleasePages(*live);
                        if (live->panelPopup) live->panelPopup.IsOpen(false);
                    }
                    live->surfaces.erase(found);
                }
                journal->Write("Interface: panel \"" + label + "\" closed by the user");
                (void)sender;
            } catch (...) {
            }
        });
        SlidePanelIn(frame);
        stack.Children().Append(frame);
        // The pages make room for the first panel now rather than at the next refresh.
        if (auto element = content.try_as<xaml::UIElement>(); element && element.XamlRoot()) {
            PlacePanelPopup(*context, element.XamlRoot());
        }
        mounted.renderer.reset();
        mounted.panelFrame = frame;
        mounted.slot = slot;
        mounted.title = title;
    }
    uirender::PatchStats stats = RenderInto(shared, mounted, surface);
    JournalRender(shared, "panel", surface, stats, first);
}

// Covers the area of Apple Music's pages with the page host, wherever the theme put it.
void PlacePageHost(WindowContext& context, const xaml::XamlRoot& root) {
    if (!context.pagePopup || !context.pageHost) return;
    auto size = root.Size();
    ContentArea area = FindContentArea(context, size.Height);
    double width = std::max(200.0, area.width > 0 ? area.width : static_cast<double>(size.Width) - area.left);
    double height = std::max(200.0, area.height > 0 ? area.height : static_cast<double>(size.Height) - area.top);
    if (context.pageHost.Width() != width) context.pageHost.Width(width);
    if (context.pageHost.Height() != height) context.pageHost.Height(height);
    if (context.pagePopup.HorizontalOffset() != area.left) context.pagePopup.HorizontalOffset(area.left);
    if (context.pagePopup.VerticalOffset() != area.top) context.pagePopup.VerticalOffset(area.top);
}

// Shows Apple Music's page again, and gives the host its own background back. Safe to call when nothing is covered.
void UncoverApplePage(WindowContext& context) noexcept {
    try {
        if (auto frame = std::exchange(context.coveredFrame, nullptr)) {
            frame.Navigated(context.coveredNavigated);
            frame.Opacity(context.coveredOpacity);
        }
        if (auto sidebar = std::exchange(context.coveredSidebar, nullptr)) sidebar.ItemInvoked(context.coveredInvoked);
        if (context.pageHost && context.pageBackground) context.pageHost.Background(context.pageBackground);
    } catch (...) {
    }
}

void ShowPageHost(WindowContext& context, bool visible) {
    if (!context.pagePopup || !context.pageHost) return;
    context.pageHost.Visibility(visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    if (visible) {
        if (context.pagePopup.IsOpen()) return;
        OpenOverlay(context, context.pagePopup);
        // The last Popup opened lies on top: the panels, opened first, went under the see-through page and showed
        // through its header. Opened again, they float over the page as over Apple Music's own.
        if (context.panelPopup && context.panelPopup.IsOpen()) {
            context.panelPopup.IsOpen(false);
            OpenOverlay(context, context.panelPopup);
        }
        return;
    }
    UncoverApplePage(context);
    if (context.pagePopup.IsOpen()) context.pagePopup.IsOpen(false);
}

// Hides Apple Music's page under the shown app page: the host turns see-through, so the app page sits on the theme's
// panel (its colour, acrylic, corners) as Apple Music's pages do. A navigation of Apple Music (sidebar, back arrow,
// search) closes the app page, as leaving a Spicetify app does.
void CoverApplePage(const std::shared_ptr<WindowContext>& owner) {
    WindowContext& context = *owner;
    std::optional<int> index = uirender::FindByName(context.probe, "ContentFrame");
    if (!index || *index >= static_cast<int>(context.probeObjects.size()) || !context.pageHost) return;
    auto frame = context.probeObjects[*index].get().try_as<controls::Frame>();
    if (!frame || frame == context.coveredFrame) return;
    UncoverApplePage(context);
    WeakContext weakContext = owner;
    auto leave = [weakContext]() noexcept {
        try {
            if (auto live = weakContext.lock()) {
                live->pageClosed = true;
                ShowPageHost(*live, false);
            }
        } catch (...) {
        }
    };
    context.coveredNavigated = frame.Navigated(
        [leave](IInspectable const&, xaml::Navigation::NavigationEventArgs const&) noexcept { leave(); });
    if (std::optional<int> nav = uirender::FindByName(context.probe, "NavView");
        nav && *nav < static_cast<int>(context.probeObjects.size())) {
        if (auto sidebar = context.probeObjects[*nav].get().try_as<controls::NavigationView>()) {
            // A folder of the sidebar that only opens or closes leaves the page where it is.
            using InvokedArgs = controls::NavigationViewItemInvokedEventArgs;
            context.coveredInvoked = sidebar.ItemInvoked([leave](IInspectable const&, InvokedArgs const& args) noexcept {
                try {
                    auto item = args.InvokedItemContainer().try_as<controls::NavigationViewItem>();
                    if (args.IsSettingsInvoked() || !item || item.SelectsOnInvoked()) leave();
                } catch (...) {
                }
            });
            context.coveredSidebar = sidebar;
        }
    }
    context.coveredFrame = frame;
    context.coveredOpacity = frame.Opacity();
    frame.Opacity(0);
    if (!context.pageBackground) context.pageBackground = context.pageHost.Background();
    context.pageHost.Background(media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));  // still takes clicks
}

// The host of an app page: a Popup of the window over the content area while the route points to the page (Apple
// Music takes out what Ampify adds to its own panels). Apple Music's own frame is not replaced.
controls::Border EnsurePageHost(UiService::Shared& shared, const std::shared_ptr<WindowContext>& owner,
                                const xaml::DependencyObject& content) {
    WindowContext& context = *owner;
    auto rootElement = content.try_as<xaml::UIElement>();
    xaml::XamlRoot xamlRoot = rootElement ? rootElement.XamlRoot() : nullptr;
    if (!xamlRoot) return nullptr;
    if (context.pageSlot) {
        PlacePageHost(context, xamlRoot);
        return context.pageSlot;
    }
    controls::Grid host = LoadKitXaml(L"<Grid " + std::wstring(KitXmlns()) +
                                      L" Background='{ThemeResource SolidBackgroundFillColorBaseBrush}'/>")
                              .try_as<controls::Grid>();
    if (!host) host = controls::Grid();
    Name(host, "AMPIFY_PageHost", "App page");
    host.Padding(xaml::Thickness{32, 16, 16, 16});
    controls::RowDefinition headerRow;
    headerRow.Height(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    controls::RowDefinition bodyRow;
    bodyRow.Height(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    host.RowDefinitions().Append(headerRow);
    host.RowDefinitions().Append(bodyRow);

    controls::StackPanel header;
    header.Orientation(controls::Orientation::Horizontal);
    header.Spacing(12);
    header.Margin(xaml::Thickness{0, 0, 0, 16});
    // An arrow, as Apple Music's own back button; its name stays readable for screen readers and in the tooltip.
    controls::Button back;
    controls::FontIcon arrow;
    arrow.Glyph(L"\xE72B");  // Back, in Segoe Fluent Icons
    arrow.FontSize(14);
    back.Content(arrow);
    controls::ToolTipService::SetToolTip(back, winrt::box_value(winrt::to_hstring(i18n::Text("page.back"))));
    Name(back, "AMPIFY_PageHost_back", i18n::Text("page.back"));
    controls::TextBlock title = MakeText(L"", "title", false);
    title.FontWeight(winrt::Windows::UI::Text::FontWeight{600});  // as Apple's page titles (Settings, 28 or 34 px)
    title.VerticalAlignment(xaml::VerticalAlignment::Center);
    header.Children().Append(back);
    header.Children().Append(title);
    host.Children().Append(header);

    // Apple Music's style lets a ScrollViewer scroll sideways, which measured the page wider than the panel and cut its
    // right column. The right margin keeps the content clear of the scroll bar, which Windows draws over it.
    controls::ScrollViewer scroll;
    controls::Grid::SetRow(scroll, 1);
    scroll.HorizontalScrollMode(controls::ScrollMode::Disabled);
    scroll.HorizontalScrollBarVisibility(controls::ScrollBarVisibility::Disabled);
    controls::Border slot;
    slot.Margin(xaml::Thickness{0, 0, 16, 0});
    scroll.Content(slot);
    QuietScrollBar(scroll);
    host.Children().Append(scroll);

    host.Visibility(xaml::Visibility::Collapsed);
    primitives::Popup popup;
    popup.XamlRoot(xamlRoot);
    popup.Child(host);

    // The handler holds the window context weakly, not the host: the host holds the button that holds the handler.
    // The arrow goes back to the page shown before (a detail view of the Marketplace to its list), as Apple Music's own
    // arrow does; from the first page of the visit, it closes the host.
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = owner;
    back.Click([weakShared, weakContext](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            auto live = weakContext.lock();
            if (!live) return;
            if (!live->routeHistory.empty()) {
                live->backPending = true;
                QueueRefresh(weakShared, live);
                return;
            }
            live->pageClosed = true;
            ShowPageHost(*live, false);
        } catch (...) {
        }
    });
    context.pagePopup = popup;
    context.pageHost = host;
    context.pageSlot = slot;
    context.pageScroll = scroll;
    context.pageTitle = title;
    PlacePageHost(context, xamlRoot);
    shared.Once("page-host", "Interface: app page host opened in a Popup of the window");
    return slot;
}

void MountPage(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
               const xaml::DependencyObject& content, const Surface& surface) {
    bool routed = context->route && context->route->first == surface.elementId && context->route->second == surface.id;
    MountedSurface& mounted = context->surfaces[surface.key];
    if (!routed) {
        mounted.surface = surface;
        return;  // rendered when the extension navigates to it
    }
    controls::Border slot = EnsurePageHost(shared, context, content);
    if (!slot) return;
    bool first = !mounted.mounted || mounted.slot != slot;
    if (!first && mounted.surface.revision == surface.revision) return;
    if (first) mounted.renderer.reset();
    mounted.slot = slot;
    mounted.title = context->pageTitle;
    uirender::PatchStats stats = RenderInto(shared, mounted, surface);
    mounted.mounted = true;
    if (!context->pageClosed) {
        ShowPageHost(*context, true);
        CoverApplePage(context);
    }
    JournalRender(shared, "page", surface, stats, first);
}

// The element named `name` under `node` in the visual tree (a template part), or null.
xaml::FrameworkElement FindPart(const xaml::DependencyObject& node, std::wstring_view name, int depth = 0) {
    constexpr int kPartDepth = 12;
    if (auto element = node.try_as<xaml::FrameworkElement>(); element && element.Name() == name) return element;
    if (depth >= kPartDepth) return nullptr;
    for (int i = 0, count = media::VisualTreeHelper::GetChildrenCount(node); i < count; ++i) {
        if (auto found = FindPart(media::VisualTreeHelper::GetChild(node, i), name, depth + 1)) return found;
    }
    return nullptr;
}

// Apple's dialog template sets WebDings (its ContentControlThemeFontFamily) on the dialog's buttons, which beats any
// style: "Close" showed as symbols (05/11). A local value beats the template: the font it gives the content.
void GiveDialogButtonsTextFont(const controls::ContentDialog& dialog) {
    auto content = FindPart(dialog, L"ContentScrollViewer").try_as<controls::Control>();
    if (!content) return;
    for (const wchar_t* name : {L"PrimaryButton", L"SecondaryButton", L"CloseButton"}) {
        if (auto button = FindPart(dialog, name).try_as<controls::Control>()) button.FontFamily(content.FontFamily());
    }
}

void MountPopup(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                const xaml::DependencyObject& content, const Surface& surface) {
    MountedSurface& mounted = context->surfaces[surface.key];
    if (mounted.dismissed) return;
    if (auto dialog = mounted.dialog) {
        if (mounted.surface.revision == surface.revision) return;
        uirender::PatchStats stats;
        bool replaced = false;
        xaml::FrameworkElement root = mounted.renderer->Render(uirender::BuildRenderTree(surface.tree, surface.nodeId),
                                                               shared.probe, stats, replaced);
        if (replaced) dialog.Content(root);
        mounted.surface = surface;
        JournalRender(shared, "popup", surface, stats, false);
        return;
    }
    if (context->dialogOpen) return;  // one dialog at a time: the next one opens when this one closes
    auto rootElement = content.try_as<xaml::UIElement>();
    if (!rootElement || !rootElement.XamlRoot()) return;

    mounted.renderer = std::make_unique<XamlRenderer>(surface.elementId, shared.sink);
    uirender::PatchStats stats;
    bool replaced = false;
    xaml::FrameworkElement root = mounted.renderer->Render(uirender::BuildRenderTree(surface.tree, surface.nodeId),
                                                           shared.probe, stats, replaced);
    controls::ContentDialog dialog;
    if (auto style = ThemeResource(L"DefaultContentDialogStyle").try_as<xaml::Style>()) dialog.Style(style);
    dialog.XamlRoot(rootElement.XamlRoot());
    dialog.Title(winrt::box_value(Wide(surface.title.empty() ? surface.elementId : surface.title)));
    dialog.Content(root);
    dialog.CloseButtonText(Wide(i18n::Text("ui.close")));
    // Close, alone, stands for the default button: Apple's plain style with Ampify's accent template, as Ampify's other
    // primary buttons (Apple's AccentButtonStyle draws text in WebDings). Its font: GiveDialogButtonsTextFont.
    auto accent = xaml::Markup::XamlReader::Load(
                      L"<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='Button'>"
                      L"<Setter Property='CornerRadius' Value='{ThemeResource ControlCornerRadius}'/></Style>")
                      .as<xaml::Style>();
    if (shared.probe.button) accent.BasedOn(shared.probe.button);
    if (controls::ControlTemplate primary = PrimaryButtonTemplate()) {
        accent.Setters().Append(xaml::Setter(controls::Control::TemplateProperty(), primary));
    }
    dialog.CloseButtonStyle(accent);
    Name(dialog, uirender::ElementName(surface.elementId, surface.nodeId) + "_dialog",
         surface.title.empty() ? surface.elementId : surface.title);

    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = context;
    std::string key = surface.key;
    dialog.Opened([](controls::ContentDialog const& sender, controls::ContentDialogOpenedEventArgs const&) noexcept {
        try {
            GiveDialogButtonsTextFont(sender);
        } catch (...) {
        }
    });
    dialog.Closed([weakShared, weakContext, key](controls::ContentDialog const&,
                                                 controls::ContentDialogClosedEventArgs const&) noexcept {
        try {
            auto live = weakContext.lock();
            if (!live) return;
            live->dialogOpen = false;
            auto found = live->surfaces.find(key);
            if (found != live->surfaces.end()) {
                found->second.dismissed = true;
                found->second.dialog = nullptr;
            }
            QueueRefresh(weakShared, live);  // the next popup, if one waits
        } catch (...) {
        }
    });
    try {
        dialog.ShowAsync();
    } catch (...) {
        return;  // another ContentDialog of Apple Music is open: tried again on the next refresh
    }
    context->dialogOpen = true;
    mounted.dialog = dialog;
    mounted.surface = surface;
    JournalRender(shared, "popup", surface, stats, true);
}

// ---- The fullscreen view ------------------------------------------------------------------------------------------
// A Popup of the window that covers all of its content, above Apple Music and the other overlays, with a Close button
// at the top right. Escape closes it too. It lives outside Apple Music's pages, so their navigation leaves it alone.

UiEvent FullscreenClosed(const std::string& elementId, const std::string& session) {
    return UiEvent{elementId, 0, false, "{\"session\":" + session + "}", "fullscreenClose"};
}

// Covers the whole window: called at each refresh, and the root's SizeChanged queues one.
void PlaceFullscreen(WindowContext& context, const xaml::XamlRoot& root) {
    if (!context.fullscreenPopup || !context.fullscreenHost) return;
    auto size = root.Size();
    double width = std::max(1.0, static_cast<double>(size.Width));
    double height = std::max(1.0, static_cast<double>(size.Height));
    if (context.fullscreenHost.Width() != width) context.fullscreenHost.Width(width);
    if (context.fullscreenHost.Height() != height) context.fullscreenHost.Height(height);
}

// Hides the view and gives the focus back to what had it, so the view never keeps it once closed.
void HideFullscreen(WindowContext& context) {
    if (!context.fullscreenPopup || !context.fullscreenHost) return;
    context.fullscreenHost.Visibility(xaml::Visibility::Collapsed);
    if (context.fullscreenPopup.IsOpen()) context.fullscreenPopup.IsOpen(false);
    xaml::UIElement previous = std::exchange(context.focusBefore, nullptr);
    try {
        if (previous && previous.XamlRoot()) previous.Focus(xaml::FocusState::Programmatic);
    } catch (...) {
        // That element left the tree meanwhile: the focus stays where XAML puts it.
    }
}

void ShowFullscreen(WindowContext& context, const xaml::XamlRoot& root) {
    if (!context.fullscreenPopup || !context.fullscreenHost) return;
    if (context.fullscreenHost.Visibility() != xaml::Visibility::Visible) {
        context.fullscreenHost.Visibility(xaml::Visibility::Visible);
    }
    if (context.fullscreenPopup.IsOpen()) return;
    try {
        context.focusBefore = xaml::Input::FocusManager::GetFocusedElement(root).try_as<xaml::UIElement>();
    } catch (...) {
        context.focusBefore = nullptr;
    }
    context.fullscreenPopup.IsOpen(true);
    context.fullscreenRaisedAt = context.overlayOpens;
    // The focus inside the view, so Escape reaches it at once.
    if (context.fullscreenClose) context.fullscreenClose.Focus(xaml::FocusState::Programmatic);
}

// The user closed the view (Close button or Escape): hidden at once, the element told, and its controls released at
// the next refresh, outside this handler. The surface stays closed until the element opens a new one.
void DismissFullscreen(const WeakShared& weakShared, const WeakContext& weakContext) noexcept {
    try {
        std::shared_ptr<WindowContext> live = weakContext.lock();
        SharedPtr shared = weakShared.lock();
        if (!live || !shared) return;
        HideFullscreen(*live);
        auto found = live->surfaces.find(live->fullscreenKey);
        if (found == live->surfaces.end() || found->second.dismissed) return;
        found->second.dismissed = true;
        const Surface& surface = found->second.surface;
        if (shared->sink) shared->sink(FullscreenClosed(surface.elementId, surface.id));
        shared->journal.Write("Interface: fullscreen view \"" + SurfaceLabel(surface) + "\" closed by the user");
        QueueRefresh(weakShared, live);
    } catch (...) {
        // A handler runs inside Apple Music's XAML frame: nothing may escape it.
    }
}

controls::Button MakeFullscreenClose() {
    controls::Button close;
    controls::FontIcon glyph;
    glyph.Glyph(L"\xE711");  // Cancel
    glyph.FontSize(16);
    close.Content(glyph);
    // Top left: the window's own minimize, maximize and close buttons hold the top right corner.
    close.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    close.VerticalAlignment(xaml::VerticalAlignment::Top);
    close.Margin(xaml::Thickness{16, 16, 0, 0});
    close.Padding(xaml::Thickness{10, 10, 10, 10});
    controls::ToolTipService::SetToolTip(close, winrt::box_value(Wide(i18n::Text("ui.close"))));
    Name(close, "AMPIFY_FullscreenHost_close", i18n::Text("ui.close"));
    return close;
}

controls::Border EnsureFullscreenHost(UiService::Shared& shared, const std::shared_ptr<WindowContext>& owner,
                                      const xaml::DependencyObject& content) {
    WindowContext& context = *owner;
    auto rootElement = content.try_as<xaml::UIElement>();
    xaml::XamlRoot xamlRoot = rootElement ? rootElement.XamlRoot() : nullptr;
    if (!xamlRoot) return nullptr;
    if (context.fullscreenSlot) {
        PlaceFullscreen(context, xamlRoot);
        return context.fullscreenSlot;
    }
    // Opaque, so a view that does not fill the window never shows Apple Music through it.
    controls::Grid host = LoadKitXaml(L"<Grid " + std::wstring(KitXmlns()) +
                                      L" Background='{ThemeResource SolidBackgroundFillColorBaseBrush}'/>")
                              .try_as<controls::Grid>();
    if (!host) host = controls::Grid();
    Name(host, "AMPIFY_FullscreenHost", "Fullscreen view");
    controls::Border slot;
    Name(slot, "AMPIFY_FullscreenSlot");
    controls::Button close = MakeFullscreenClose();
    host.Children().Append(slot);
    host.Children().Append(close);
    host.Visibility(xaml::Visibility::Collapsed);

    primitives::Popup popup;
    popup.XamlRoot(xamlRoot);
    popup.Child(host);
    // The handlers hold the window context weakly, never the host that holds them.
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = owner;
    close.Click([weakShared, weakContext](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        DismissFullscreen(weakShared, weakContext);
    });
    host.KeyDown([weakShared, weakContext](IInspectable const&, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
        try {
            if (args.Key() != winrt::Windows::System::VirtualKey::Escape) return;
            args.Handled(true);
            DismissFullscreen(weakShared, weakContext);
        } catch (...) {
        }
    });
    context.fullscreenPopup = popup;
    context.fullscreenHost = host;
    context.fullscreenSlot = slot;
    context.fullscreenClose = close;
    PlaceFullscreen(context, xamlRoot);
    shared.Once("fullscreen-host", "Interface: fullscreen host opened in a Popup of the window");
    return slot;
}

void MountFullscreen(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                     const xaml::DependencyObject& content, const Surface& surface) {
    MountedSurface& mounted = context->surfaces[surface.key];
    mounted.surface.kind = SurfaceKind::Fullscreen;  // so Unmount knows it before the first render
    mounted.surface.key = surface.key;
    if (mounted.dismissed) {
        // Closed by the user: its controls go now, outside the handler that closed it.
        if (mounted.renderer) {
            if (mounted.slot && mounted.slot.Child() == mounted.renderer->Root()) mounted.slot.Child(nullptr);
            mounted.renderer.reset();
        }
        return;
    }
    controls::Border slot = EnsureFullscreenHost(shared, context, content);
    if (!slot) return;
    bool first = !mounted.mounted || mounted.slot != slot;
    if (first || mounted.surface.revision != surface.revision) {
        if (first) mounted.renderer.reset();
        mounted.slot = slot;
        uirender::PatchStats stats = RenderInto(shared, mounted, surface);
        mounted.mounted = true;
        JournalRender(shared, "fullscreen view", surface, stats, first);
    }
    context->fullscreenKey = surface.key;
    auto element = content.try_as<xaml::UIElement>();
    if (element && element.XamlRoot()) ShowFullscreen(*context, element.XamlRoot());
}

void Unmount(WindowContext& context, MountedSurface& mounted) {
    try {
        if (mounted.surface.kind == SurfaceKind::Fullscreen) {
            if (auto slot = mounted.slot; slot && mounted.renderer && slot.Child() == mounted.renderer->Root()) {
                slot.Child(nullptr);
            }
            if (context.fullscreenKey == mounted.surface.key) {
                HideFullscreen(context);
                context.fullscreenKey.clear();
            }
        }
        if (auto dialog = mounted.dialog) dialog.Hide();
        if (mounted.surface.kind == SurfaceKind::Panel) SlidePanelOut(context, mounted.panelFrame);
        if (mounted.surface.kind == SurfaceKind::Page) {
            if (auto slot = mounted.slot; slot && mounted.renderer && slot.Child() == mounted.renderer->Root()) {
                slot.Child(nullptr);
                ShowPageHost(context, false);
            }
        }
    } catch (...) {
    }
}

// ---- Buttons of the top bar and the play bar ----------------------------------------------------------------------

// Sidebar entries. Apple Music fills its menu and footer through item sources of its own view models: an item Ampify
// added to its footer source crashed it at once (1.1792), and a NavigationViewItem anywhere in the pane would report
// its taps to Apple Music's navigation. So the entries are flat buttons laid out like the sidebar's items, in a panel
// that takes the place of the NavigationView's PaneFooter, with Apple Music's own footer (a separator) kept at its end.
controls::NavigationView FindNavigation(const WindowContext& context) {
    for (size_t i = 0; i < context.probe.size(); i++) {
        if (!context.probe[i].Is(uirender::kProbeNavigationView)) continue;
        if (auto navigation = context.probeObjects[i].get().try_as<controls::NavigationView>()) return navigation;
    }
    return nullptr;
}

controls::StackPanel EnsureSidebarHost(WindowContext& context, const controls::NavigationView& navigation) {
    xaml::UIElement footer = navigation.PaneFooter();
    if (context.sidebarHost && footer == context.sidebarHost) return context.sidebarHost;
    controls::StackPanel host;
    Name(host, "AMPIFY_SidebarHost");
    navigation.PaneFooter(nullptr);  // frees Apple Music's footer, a separator, which becomes the first child of the panel
    if (footer) host.Children().Append(footer);
    navigation.PaneFooter(host);
    context.sidebarHost = host;
    context.sidebarOriginal = footer;
    return host;
}

// Puts Apple Music's own footer back once no element has an entry left.
void ReleaseSidebarHost(WindowContext& context) {
    if (!context.sidebarHost || context.sidebarHost.Children().Size() > (context.sidebarOriginal ? 1u : 0u)) return;
    controls::NavigationView navigation = FindNavigation(context);
    if (navigation && navigation.PaneFooter() == context.sidebarHost) {
        navigation.PaneFooter(nullptr);
        context.sidebarHost.Children().Clear();
        navigation.PaneFooter(context.sidebarOriginal);
    }
    context.sidebarHost = nullptr;
    context.sidebarOriginal = nullptr;
}

// The inside of a sidebar entry: the icon and the label, and a bar at the left edge like Apple Music's own selected item.
// An entry that is on takes the accent colour. The icon is drawn again with the colour, so a state change rebuilds it.
// The icon of a sidebar entry, in the text brush like Apple Music's own icons (the Marketplace gives Ampify's logo as
// an SVG), or in the accent while the entry is on.
xaml::FrameworkElement SidebarIcon(const UiControl& control, bool active) {
    const wchar_t* key = active ? L"AccentTextFillColorPrimaryBrush" : L"TextFillColorPrimaryBrush";
    xaml::FrameworkElement icon = nullptr;
    if (!control.icon.empty() && !uirender::IsGlyphIcon(control.icon)) icon = MakeIconElement(control.icon, 16, key);
    if (!icon) {
        controls::FontIcon glyph;
        glyph.FontSize(16);
        glyph.Glyph(uirender::IsGlyphIcon(control.icon) ? Wide(control.icon) : winrt::hstring(L"\xE790"));  // Color
        if (active) {
            if (auto accent = ThemeResource(key).try_as<media::Brush>()) glyph.Foreground(accent);
        }
        icon = glyph;
    }
    icon.VerticalAlignment(xaml::VerticalAlignment::Center);
    return icon;
}

xaml::UIElement SidebarContent(const UiControl& control, bool active) {
    const wchar_t* key = active ? L"AccentTextFillColorPrimaryBrush" : L"TextFillColorPrimaryBrush";
    const media::Brush accent = active ? ThemeResource(key).try_as<media::Brush>() : nullptr;
    controls::StackPanel row;
    row.Orientation(controls::Orientation::Horizontal);
    row.Spacing(16);
    xaml::FrameworkElement icon = SidebarIcon(control, active);
    controls::TextBlock label;
    label.Text(Wide(control.label));
    label.VerticalAlignment(xaml::VerticalAlignment::Center);
    if (accent) label.Foreground(accent);
    row.Children().Append(icon);
    row.Children().Append(label);

    controls::Grid content;
    content.Children().Append(row);
    if (active) {
        controls::Border bar;
        bar.Width(3);
        bar.Height(16);
        bar.CornerRadius(xaml::CornerRadius{1.5, 1.5, 1.5, 1.5});
        bar.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        bar.VerticalAlignment(xaml::VerticalAlignment::Center);
        bar.Margin(xaml::Thickness{-12, 0, 0, 0});  // on the button's own edge, in the padding
        bar.IsHitTestVisible(false);
        if (auto fill = ThemeResource(L"AccentFillColorDefaultBrush").try_as<media::Brush>()) bar.Background(fill);
        content.Children().Append(bar);
    }
    return content;
}

// A flat button with the height, margins and corners of Apple Music's sidebar items, and the subtle hover of Fluent.
controls::Button MakeSidebarButton(const UiControl& control, bool active) {
    const std::wstring brushes =
        L"<StaticResource x:Key='ButtonBackground' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonBackgroundPointerOver' ResourceKey='SubtleFillColorSecondaryBrush'/>"
        L"<StaticResource x:Key='ButtonBackgroundPressed' ResourceKey='SubtleFillColorTertiaryBrush'/>"
        L"<StaticResource x:Key='ButtonBorderBrush' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonBorderBrushPointerOver' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonBorderBrushPressed' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonForegroundPointerOver' ResourceKey='TextFillColorPrimaryBrush'/>"
        L"<StaticResource x:Key='ButtonForegroundPressed' ResourceKey='TextFillColorSecondaryBrush'/>";
    const std::wstring text =
        L"<Button xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
        L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' HorizontalAlignment='Stretch' "
        L"HorizontalContentAlignment='Left' Height='36' Margin='4,2,4,2' Padding='12,0,12,0' CornerRadius='4' "
        L"BorderThickness='0'><Button.Resources><ResourceDictionary><ResourceDictionary.ThemeDictionaries>"
        L"<ResourceDictionary x:Key='Default'>" + brushes + L"</ResourceDictionary>"
        L"<ResourceDictionary x:Key='Light'>" + brushes + L"</ResourceDictionary>"
        L"</ResourceDictionary.ThemeDictionaries></ResourceDictionary></Button.Resources></Button>";
    controls::Button button = xaml::Markup::XamlReader::Load(text).as<controls::Button>();
    button.Content(SidebarContent(control, active));
    automation::AutomationProperties::SetItemStatus(button, active ? L"on" : L"");
    return button;
}

constexpr char kMarketplaceId[] = "marketplace";
constexpr double kMoreButtonWidth = 32;  // Apple's "···" before its first layout
constexpr double kHeaderButtonHeight = 28;  // the height of "···"
constexpr double kHeaderSpacing = 2;

// The sidebar's header (Apple's logo, its name and "···"), where Ampify's own entry stands with ampify.entry = header.
// Nothing while the sidebar is folded: Apple hides the header then, and the entry stays in the list.
controls::Grid EntryHeader(const UiService::Shared& shared, const controls::NavigationView& navigation,
                           const UiControl& control) {
    if (control.elementId != kMarketplaceId || !shared.entryInHeader || !navigation.IsPaneOpen()) return nullptr;
    return navigation.PaneHeader().try_as<controls::Grid>();
}

// The entry's icon as a button the size of "···", just left of it in the same cell, its label as the tooltip.
controls::Button MakeHeaderButton(const controls::Grid& header, const UiControl& control, bool active) {
    controls::Button button = MakeSidebarButton(control, active);  // for its subtle brushes
    button.Content(SidebarIcon(control, active));
    button.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    button.HorizontalContentAlignment(xaml::HorizontalAlignment::Center);
    button.VerticalAlignment(xaml::VerticalAlignment::Center);
    button.Width(kMoreButtonWidth);
    button.Height(kHeaderButtonHeight);
    button.Padding(xaml::Thickness{});
    double right = kMoreButtonWidth + kHeaderSpacing;
    for (const xaml::UIElement& child : header.Children()) {
        auto more = child.try_as<xaml::FrameworkElement>();
        if (!more || more.Name() != L"MoreButton") continue;
        right = (more.ActualWidth() > 0 ? more.ActualWidth() : kMoreButtonWidth) + more.Margin().Right + kHeaderSpacing;
        controls::Grid::SetColumn(button, controls::Grid::GetColumn(more));
        controls::Grid::SetRow(button, controls::Grid::GetRow(more));
    }
    button.Margin(xaml::Thickness{0, 0, right, 0});
    controls::ToolTipService::SetToolTip(button, winrt::box_value(Wide(control.label)));
    return button;
}

// An entry is on when its element asked for it (Ampify.ui.setActive), or while the page of that element is on screen.
bool SidebarEntryIsOn(const WindowContext& context, const UiControl& control) {
    return control.active || (context.route && !context.pageClosed && context.route->first == control.elementId);
}

void PlaceSidebarItem(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context, PlacedControl& placed,
                      const UiControl& control) {
    controls::NavigationView navigation = FindNavigation(*context);
    if (!navigation) {
        if (!placed.reported) {
            placed.reported = true;
            shared.journal.Write("Interface: sidebar not found for the entry \"" + control.label + "\" of " +
                                 control.elementId);
        }
        return;
    }
    controls::Grid header = EntryHeader(shared, navigation, control);
    const bool inHeader = static_cast<bool>(header);
    controls::StackPanel host = inHeader ? nullptr : EnsureSidebarHost(*context, navigation);
    controls::UIElementCollection place = inHeader ? header.Children() : host.Children();
    const bool active = SidebarEntryIsOn(*context, control);
    std::uint32_t index = 0;
    if (placed.button && placed.inHeader == inHeader && place.IndexOf(placed.button, index)) {
        if (placed.active != active) {  // still in place: only its state may change
            placed.active = active;
            placed.button.Content(inHeader ? SidebarIcon(control, active) : SidebarContent(control, active));
            automation::AutomationProperties::SetItemStatus(placed.button, active ? L"on" : L"");
        }
        return;
    }

    RemoveFromParent(placed.button);  // from the list to the header or back, as the sidebar folds or opens
    controls::Button button = inHeader ? MakeHeaderButton(header, control, active) : MakeSidebarButton(control, active);
    placed.active = active;
    placed.inHeader = inHeader;
    Name(button, uirender::ElementName(control.elementId, "control-" + std::to_string(control.id)), control.label);
    UiEventSink sink = shared.sink;
    std::string elementId = control.elementId;
    int id = control.id;
    button.Click([sink, elementId, id](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            if (sink) sink(UiEvent{elementId, id, true, {}});
        } catch (...) {
        }
    });
    // After Apple Music's separator, so the entries sit with the account, below the line that ends the list, in the
    // order of the elements (an entry back from the header takes its place again).
    if (inHeader) {
        place.Append(button);
    } else {
        place.InsertAt(StableIndex(context->placed, host, std::nullopt, control.elementId, control.id), button);
    }
    placed.button = button;
    placed.elementId = control.elementId;
    placed.id = control.id;
    shared.journal.Write("Interface: sidebar entry \"" + control.label + "\" of " + control.elementId + " placed" +
                         (inHeader ? " in the sidebar's header" : ""));
}

using MenuItems = winrt::Windows::Foundation::Collections::IVector<controls::MenuFlyoutItemBase>;

constexpr wchar_t kMenuEntryTag[] = L"AMPIFY_MENU_ENTRY";
constexpr auto kMenuPoll = std::chrono::milliseconds(50);
constexpr int kMenuTries = 10;  // half a second after the click or the key
constexpr double kFlyoutMargin = 8;

bool IsMenuEntry(const controls::MenuFlyoutItemBase& item) {
    return item && winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"") == kMenuEntryTag;
}

bool HasMenuEntries(const MenuItems& items) {
    for (const controls::MenuFlyoutItemBase& item : items) {
        if (IsMenuEntry(item)) return true;
    }
    return false;
}

// True when `node` is inside the element named `name`.
bool InsideNamed(xaml::DependencyObject node, std::wstring_view name) {
    for (int depth = 0; node && depth < 64; depth++) {
        if (auto element = node.try_as<xaml::FrameworkElement>(); element && element.Name() == name) return true;
        node = media::VisualTreeHelper::GetParent(node);
    }
    return false;
}

std::vector<std::string> MenuTexts(const MenuItems& items) {
    std::vector<std::string> texts;
    for (const controls::MenuFlyoutItemBase& item : items) {
        if (auto entry = item.try_as<controls::MenuFlyoutItem>()) texts.push_back(Utf8(entry.Text()));
        else if (auto submenu = item.try_as<controls::MenuFlyoutSubItem>()) texts.push_back(Utf8(submenu.Text()));
    }
    return texts;
}

// The style Apple Music gives its own items of that exact class, so Ampify's entries look like them.
xaml::Style StyleOf(const MenuItems& items, std::wstring_view className) {
    for (const controls::MenuFlyoutItemBase& item : items) {
        if (winrt::get_class_name(item) != className) continue;
        if (xaml::Style style = item.Style()) return style;
    }
    return nullptr;
}

// What a menu is about: the item's name, taken from the menu's own "Play «Name»" entry, and what Apple Music's data
// says about it (its identifiers and kind), read once when the menu opens.
struct MenuTarget {
    std::string title;
    std::optional<AmpItemInfo> item;
};

// The item the menu is about: the first data context, from the element the click came from upwards, that Apple Music
// ties to a content descriptor and that carries the menu's own name (the row of a song list, an album's card...), not
// the page's, which has a descriptor too.
std::optional<AmpItemInfo> MenuItemInfo(xaml::DependencyObject node, const std::string& title) {
    constexpr int kMaxDepth = 16;
    for (int depth = 0; node && depth < kMaxDepth; depth++) {
        auto element = node.try_as<xaml::FrameworkElement>();
        if (auto data = element ? element.DataContext() : nullptr) {
            winrt::com_ptr<::IUnknown> unknown = data.as<::IUnknown>();
            if (auto info = ReadItemInfo(rt::Ref(unknown.detach()), title)) return info;
        }
        node = media::VisualTreeHelper::GetParent(node);
    }
    return std::nullopt;
}

std::string MenuPayload(const UiControl& control, const MenuTarget& target) {
    Json::Object fields{{"id", Json(static_cast<double>(control.id))}, {"title", Json(target.title)}};
    if (target.item) {
        Json::Object item{{"kind", Json(target.item->kind)}};
        if (!target.item->id.empty()) item.emplace_back("id", Json(target.item->id));
        if (!target.item->canonicalId.empty()) item.emplace_back("canonicalId", Json(target.item->canonicalId));
        fields.emplace_back("item", Json(std::move(item)));
    }
    return Json(std::move(fields)).Dump();
}

controls::MenuFlyoutItem MakeMenuEntry(const UiEventSink& sink, const UiControl& control, const MenuTarget& target,
                                       const xaml::Style& style) {
    controls::MenuFlyoutItem item;
    if (style) item.Style(style);
    item.Text(Wide(control.label));
    item.Tag(winrt::box_value(winrt::hstring(kMenuEntryTag)));
    if (uirender::IsGlyphIcon(control.icon)) {
        controls::FontIcon icon;
        icon.Glyph(Wide(control.icon));
        item.Icon(icon);
    }
    Name(item, uirender::ElementName(control.elementId, "control-" + std::to_string(control.id)), control.label);
    std::string payload = MenuPayload(control, target);
    std::string elementId = control.elementId;
    item.Click([sink, elementId, payload](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            if (sink) sink(UiEvent{elementId, 0, false, payload, "menuClick"});
        } catch (...) {
        }
    });
    return item;
}

// A submenu: its own entries are the controls whose parent is it. Apple Music's submenus give it their style.
controls::MenuFlyoutSubItem MakeMenuSubmenu(const UiEventSink& sink, const UiControl& control,
                                            const std::vector<UiControl>& entries, const MenuTarget& target,
                                            const xaml::Style& itemStyle, const xaml::Style& submenuStyle) {
    controls::MenuFlyoutSubItem submenu;
    if (submenuStyle) submenu.Style(submenuStyle);
    submenu.Text(Wide(control.label));
    submenu.Tag(winrt::box_value(winrt::hstring(kMenuEntryTag)));
    if (uirender::IsGlyphIcon(control.icon)) {
        controls::FontIcon icon;
        icon.Glyph(Wide(control.icon));
        submenu.Icon(icon);
    }
    Name(submenu, uirender::ElementName(control.elementId, "control-" + std::to_string(control.id)), control.label);
    for (const UiControl& child : entries) {
        if (child.parent == control.id) submenu.Items().Append(MakeMenuEntry(sink, child, target, itemStyle));
    }
    return submenu;
}

// A separator, then the entries, at the end of an open menu. Apple Music keeps a menu's items from one opening to the
// next and fills them again from its own list, so Ampify's leave once the menu has closed.
void AppendMenuEntries(const UiEventSink& sink, const primitives::Popup& popup, const MenuItems& items,
                       const std::vector<UiControl>& entries, const MenuTarget& target) {
    xaml::Style itemStyle = StyleOf(items, L"Microsoft.UI.Xaml.Controls.MenuFlyoutItem");
    xaml::Style submenuStyle = StyleOf(items, L"Microsoft.UI.Xaml.Controls.MenuFlyoutSubItem");
    controls::MenuFlyoutSeparator separator;
    if (xaml::Style lineStyle = StyleOf(items, L"Microsoft.UI.Xaml.Controls.MenuFlyoutSeparator")) separator.Style(lineStyle);
    separator.Tag(winrt::box_value(winrt::hstring(kMenuEntryTag)));
    items.Append(separator);
    for (const UiControl& control : entries) {
        if (control.parent != 0) continue;  // it is shown inside its submenu
        if (control.clickable) items.Append(MakeMenuEntry(sink, control, target, itemStyle));
        else items.Append(MakeMenuSubmenu(sink, control, entries, target, itemStyle, submenuStyle));
    }

    auto token = std::make_shared<winrt::event_token>();
    *token = popup.Closed([items, token](IInspectable const& sender, IInspectable const&) noexcept {
        try {
            if (auto closed = sender.try_as<primitives::Popup>()) closed.Closed(*token);
            // Once the close has run its course. A menu opened in the meantime is looked for after this, in order.
            dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue(
                dispatching::DispatcherQueuePriority::Low, [items]() noexcept {
                    try {
                        for (std::uint32_t index = items.Size(); index-- > 0;) {
                            if (IsMenuEntry(items.GetAt(index))) items.RemoveAt(index);
                        }
                    } catch (...) {
                    }
                });
        } catch (...) {
        }
    });
}

// The menu the last click or key opened gets the extensions' entries: Apple Music's main menu (opened from its
// header, Grid#PaneHeaderGrid) the "app" ones, the menu of an item (a song, an album, a playlist: the menus with a
// "Play «Name»" entry) the "context" ones, with that item's name. True once that menu has them, or when nothing is to
// be added.
bool AddMenuEntries(UiService::Shared& shared, WindowContext& context) {
    xaml::UIElement input = context.menuInput.get();
    xaml::XamlRoot root = input ? input.XamlRoot() : nullptr;
    if (!root) return true;
    bool app = InsideNamed(context.menuSource.get(), L"PaneHeaderGrid");
    const std::vector<UiControl>& entries = app ? context.appEntries : context.contextEntries;
    if (entries.empty()) return true;
    for (const primitives::Popup& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root)) {
        auto presenter = popup.Child().try_as<controls::MenuFlyoutPresenter>();
        if (!presenter) continue;
        MenuItems items = presenter.ItemsSource().try_as<MenuItems>();
        if (!items) {
            shared.Once("menu-items", "Interface: a menu whose items are not MenuFlyoutItemBase, left as it is");
            continue;
        }
        if (HasMenuEntries(items)) return true;  // this opening has them already
        std::string title = app ? std::string() : uirender::QuotedMenuName(MenuTexts(items));
        if (!app && title.empty()) continue;  // a menu of options, or one not filled in yet
        MenuTarget target{title, std::nullopt};
        if (!app) {
            target.item = MenuItemInfo(context.menuSource.get(), title);
            shared.Once(target.item ? "menu-item-known" : "menu-item-unknown",
                        target.item ? "Interface: an item's menu knows its content descriptor (identifier and kind)"
                                    : "Interface: an item's menu has no content descriptor behind it, entries get the name only");
        }
        AppendMenuEntries(shared.sink, popup, items, entries, target);
        shared.Once(app ? "menu-app-open" : "menu-context-open",
                    app ? "Interface: entries added to the main menu" : "Interface: entries added to an item's menu");
        return true;
    }
    return false;
}

// Apple Music opens the volume flyout from the button's left edge: with the toggles against the window's right edge, it
// ran past the window. A flyout that would moves left, a margin short of that edge. True once an open flyout has its
// size, so a click that opens none keeps looking for half a second.
bool KeepFlyoutInside(UiService::Shared& shared, const WindowContext& context) {
    xaml::UIElement input = context.menuInput.get();
    xaml::XamlRoot root = input ? input.XamlRoot() : nullptr;
    if (!root) return true;
    double room = root.Size().Width - kFlyoutMargin;
    for (const primitives::Popup& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root)) {
        auto presenter = popup.Child().try_as<controls::FlyoutPresenter>();
        if (!presenter) continue;
        double width = presenter.ActualWidth();
        if (width <= 0) return false;
        if (popup.HorizontalOffset() + width > room) {
            popup.HorizontalOffset(std::max(0.0, room - width));
            shared.Once("flyout-inside", "Interface: a flyout that ran past the window's right edge moved left");
        }
        return true;
    }
    return false;
}

void LookForMenu(const WeakShared& weakShared, const WeakContext& weakContext) noexcept {
    try {
        SharedPtr shared = weakShared.lock();
        std::shared_ptr<WindowContext> context = weakContext.lock();
        if (!shared || !context) return;
        bool menuDone = AddMenuEntries(*shared, *context);
        bool flyoutDone = KeepFlyoutInside(*shared, *context);
        if ((menuDone && flyoutDone) || ++context->menuTries >= kMenuTries) {
            if (context->menuTimer) context->menuTimer.Stop();
            return;
        }
        if (!context->menuTimer) {
            context->menuTimer = context->dispatcher.CreateTimer();
            context->menuTimer.Interval(kMenuPoll);
            context->menuTimer.Tick([weakShared, weakContext](auto const&, auto const&) noexcept {
                LookForMenu(weakShared, weakContext);
            });
        }
        if (!context->menuTimer.IsRunning()) context->menuTimer.Start();
    } catch (...) {
    }
}

// Follows the clicks and keys of the window's content, handled ones too (the button that opens a menu marks its click
// handled), and looks for the menu or flyout they open once the input has been dealt with.
void FollowMenus(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    xaml::Window window = context->window.get();
    xaml::UIElement content = window ? window.Content() : nullptr;
    if (!content || context->menuInput.get() == content) return;
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = context;
    auto look = [weakShared, weakContext](IInspectable const&, auto const& args) noexcept {
        try {
            std::shared_ptr<WindowContext> current = weakContext.lock();
            if (!current) return;
            auto source = args.OriginalSource().template try_as<xaml::DependencyObject>();
            current->menuSource = source ? winrt::make_weak(source) : winrt::weak_ref<xaml::DependencyObject>();
            current->menuTries = 0;
            current->dispatcher.TryEnqueue(dispatching::DispatcherQueuePriority::Low, [weakShared, weakContext]() noexcept {
                LookForMenu(weakShared, weakContext);
            });
        } catch (...) {
        }
    };
    content.AddHandler(xaml::UIElement::PointerReleasedEvent(), winrt::box_value(xaml::Input::PointerEventHandler(look)),
                       true);
    // KeyDown rather than KeyUp: a menu opened by the menu key takes the focus, and the key's release goes to it.
    content.AddHandler(xaml::UIElement::KeyDownEvent(), winrt::box_value(xaml::Input::KeyEventHandler(look)), true);
    context->menuInput = winrt::make_weak(content);
}

void QueueSongRoom(const UiEventSink& sink, const std::shared_ptr<WindowContext>& context) noexcept;

void PlaceControls(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                   const std::vector<Surface>& surfaces) {
    std::set<std::string> present;
    std::vector<UiControl> appEntries;
    std::vector<UiControl> contextEntries;
    for (const Surface& surface : surfaces) {
        if (surface.kind != SurfaceKind::Control) continue;
        present.insert(surface.key);
        const UiControl& control = surface.control;
        if (control.placement == "sidebar") {
            PlaceSidebarItem(shared, context, context->placed[surface.key], control);
            continue;
        }
        if (control.placement == "menu-app" || control.placement == "menu-context") {
            bool app = control.placement == "menu-app";
            (app ? appEntries : contextEntries).push_back(control);
            shared.Once("menu|" + surface.key, "Interface: menu entry \"" + control.label + "\" of " + control.elementId +
                                                   (app ? " ready for the main menu" : " ready for the menus of items"));
            continue;
        }
        if (control.placement != "topbar" && control.placement != "playbar") continue;
        PlacedControl& placed = context->placed[surface.key];
        if (placed.button && placed.button.Parent()) {  // in place: only its label may have changed (setLabel)
            if (placed.label != control.label) {
                placed.label = control.label;
                if (!placed.hasIcon) placed.button.Content(winrt::box_value(Wide(control.label)));
                controls::ToolTipService::SetToolTip(placed.button, winrt::box_value(Wide(control.label)));
                automation::AutomationProperties::SetName(placed.button, Wide(control.label));
            }
            if (placed.active != control.active) {
                placed.active = control.active;
                TintControl(placed.button, control, placed.hasIcon);
            }
            continue;
        }

        std::optional<uirender::Insertion> insertion = control.placement == "playbar"
                                                           ? uirender::ChoosePlaybarInsertion(context->probe)
                                                           : uirender::ChooseTopbarInsertion(context->probe);
        controls::StackPanel parent{nullptr};
        if (insertion) parent = context->probeObjects[insertion->parent].get().try_as<controls::StackPanel>();
        if (!parent) {
            if (!placed.reported) {
                placed.reported = true;
                shared.journal.Write("Interface: placement \"" + control.placement + "\" not found for button \"" +
                                     control.label + "\" of " + control.elementId + " (provisional landmarks)");
            }
            continue;
        }

        controls::Button button;
        if (insertion->styleFrom >= 0) {
            auto neighbour = context->probeObjects[insertion->styleFrom].get();
            if (!neighbour.try_as<controls::Button>()) {
                // A ToggleButton (Apple Music's lyrics or queue button): its style does not fit a Button, so Ampify
                // button stays flat like it, and takes its size below.
                button.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
                button.BorderThickness(xaml::Thickness{0, 0, 0, 0});
            }
            if (auto native = neighbour.try_as<controls::Control>()) {
                // An AppBarButton's style does not fit a Button: only a plain Button lends its style.
                bool plainButton = winrt::get_class_name(neighbour) == L"Microsoft.UI.Xaml.Controls.Button";
                if (auto style = native.Style(); style && plainButton) {
                    button.Style(style);
                }
                button.Width(native.Width());
                button.Height(native.Height());
                button.MinWidth(native.MinWidth());
                button.MinHeight(native.MinHeight());
                button.Padding(native.Padding());
                button.Margin(native.Margin());
                button.VerticalAlignment(native.VerticalAlignment());
            }
        }
        xaml::FrameworkElement icon = MakeIconElement(control.icon, 16, L"TextFillColorPrimaryBrush");
        if (icon) {
            button.Content(icon);
        } else {
            button.Content(winrt::box_value(Wide(control.label)));
            if (!std::isnan(button.Width())) {  // a text is wider than Apple's icon buttons: it may grow, never shrink
                button.MinWidth(button.Width());
                button.Width(std::numeric_limits<double>::quiet_NaN());
            }
        }
        if (control.active) TintControl(button, control, static_cast<bool>(icon));
        // A text that only shows something: no hover, no press, the pointer goes through to the bar (so no tooltip).
        if (control.clickable) controls::ToolTipService::SetToolTip(button, winrt::box_value(Wide(control.label)));
        else button.IsHitTestVisible(false);
        Name(button, uirender::ElementName(control.elementId, "control-" + std::to_string(control.id)), control.label);
        UiEventSink sink = shared.sink;
        std::string elementId = control.elementId;
        int id = control.id;
        button.Click([sink, elementId, id](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
            try {
                if (sink) sink(UiEvent{elementId, id, true, {}});
            } catch (...) {
            }
        });

        std::uint32_t index = 0;
        std::optional<std::uint32_t> anchor;
        auto after = insertion->after >= 0 ? context->probeObjects[insertion->after].get().try_as<xaml::UIElement>() : nullptr;
        if (after && parent.Children().IndexOf(after, index)) anchor = index;
        parent.Children().InsertAt(StableIndex(context->placed, parent, anchor, control.elementId, control.id), button);
        placed.button = button;
        placed.label = control.label;
        placed.hasIcon = static_cast<bool>(icon);
        placed.active = control.active;
        placed.elementId = control.elementId;
        placed.id = control.id;
        placed.icon = control.icon;
        placed.clickable = control.clickable;
        placed.folded = false;  // a new button shows; FoldPlaybarButtons folds it again if the bar lacks room
        shared.journal.Write("Interface: button \"" + control.label + "\" of " + control.elementId + " placed in \"" +
                             control.placement + "\" (provisional landmarks)");
    }
    for (auto it = context->placed.begin(); it != context->placed.end();) {
        if (present.count(it->first)) {
            ++it;
            continue;
        }
        RemoveFromParent(it->second.button);
        it = context->placed.erase(it);
    }
    ReleaseSidebarHost(*context);
    context->appEntries = std::move(appEntries);
    context->contextEntries = std::move(contextEntries);
    FollowMenus(shared, context);
    QueueSongRoom(shared.sink, context);  // a button gone may leave a single one folded
}

constexpr double kFoldedPaneWidth = 100;  // the folded column is 48 wide, the open sidebar 290

// Folded, Apple Music moves the sidebar's header right when the back arrow shows, for the arrow and the logo to sit side
// by side as in the open sidebar: the column cut the logo off but for 3 pixels, a sliver at its edge. The logo goes
// while the column is folded and the arrow shows, and comes back when either changes.
void ShowFoldedLogo(const WindowContext& context) {
    xaml::FrameworkElement back = context.foldBack.get();
    xaml::FrameworkElement logo = context.foldLogo.get();
    xaml::FrameworkElement pane = context.foldPane.get();
    if (!back || !logo || !pane) return;
    bool pushedOut = back.Visibility() == xaml::Visibility::Visible && pane.ActualWidth() < kFoldedPaneWidth;
    double opacity = pushedOut ? 0 : 1;
    if (logo.Opacity() != opacity) logo.Opacity(opacity);
}

void FollowFoldedLogo(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    const std::vector<ProbeNode>& nodes = context->probe;
    std::optional<int> back = uirender::FindByName(nodes, "NavigationViewBackButton");
    std::optional<int> header = uirender::FindByName(nodes, "PaneHeaderContentBorder");
    std::optional<int> pane = uirender::FindByName(nodes, "PaneContentGrid");
    if (!back || !header || !pane) return;
    std::optional<int> logo;
    for (int node = *header + 1; node < static_cast<int>(nodes.size()); node++) {
        if (nodes[node].className == "Microsoft.UI.Xaml.Controls.Image" && uirender::IsUnder(nodes, node, *header)) {
            logo = node;
            break;
        }
    }
    if (!logo) return;
    auto element = [&](int node) { return context->probeObjects[node].get().try_as<xaml::FrameworkElement>(); };
    xaml::FrameworkElement backElement = element(*back);
    xaml::FrameworkElement paneElement = element(*pane);
    xaml::FrameworkElement logoElement = element(*logo);
    if (!backElement || !paneElement || !logoElement || context->foldBack.get() == backElement) return;
    context->foldBack = winrt::make_weak(backElement);
    context->foldLogo = winrt::make_weak(logoElement);
    context->foldPane = winrt::make_weak(paneElement);
    WeakContext weakContext = context;
    auto update = [weakContext]() noexcept {
        try {
            if (auto live = weakContext.lock()) ShowFoldedLogo(*live);
        } catch (...) {
        }
    };
    backElement.RegisterPropertyChangedCallback(
        xaml::UIElement::VisibilityProperty(),
        [update](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept { update(); });
    paneElement.SizeChanged([update](IInspectable const&, xaml::SizeChangedEventArgs const&) noexcept { update(); });
    ShowFoldedLogo(*context);
    shared.Once("folded-logo", "Interface: the folded sidebar's logo follows its back arrow");
}

constexpr double kSongClearance = 24;  // between the song's display and the first toggle (the volume)
constexpr double kSongMinWidth = 200;  // the transport guard leaves the song at least this: artwork and a few letters

// A layout that sizes the song itself (bottom-bar's width) ran it under the transport in a narrow bar, the star on
// the shuffle button: the song gets a maximum width that ends it a clear space short of the transport, and loses it
// as the room comes back. Only a real overlap starts or narrows it: in a layout where the transport follows the song,
// each narrowing would draw the transport after it, down to the minimum.
// True when the song's size changes, which lays it out again and queues the next pass.
bool KeepSongClearOfTransport(WindowContext& context, const xaml::FrameworkElement& song, const xaml::UIElement& parent) {
    xaml::FrameworkElement transport = context.transport.get();
    double width = song.Width();
    double current = song.MaxWidth();
    bool ours = context.songMaxWidth && std::abs(current - context.songLimit) < 0.5;  // not rewritten since
    bool held = ours && context.songLimitedFor == width;                                // a new width starts again
    double limit = std::numeric_limits<double>::infinity();
    if (!std::isnan(width) && transport && transport.ActualWidth() > 0 && song.ActualWidth() > 0) {
        double songLeft = song.TransformToVisual(parent).TransformPoint({0, 0}).X;
        double transportLeft = transport.TransformToVisual(parent).TransformPoint({0, 0}).X;
        double room = std::max(kSongMinWidth, transportLeft - kSongClearance - songLeft);
        bool overlaps = songLeft + song.ActualWidth() > transportLeft;
        if (transportLeft > songLeft && room < width) {
            if (overlaps) limit = room;
            else if (held) limit = std::max(current, room);  // widens with the room, never narrows without an overlap
        }
    }
    bool binding = song.ActualWidth() >= current - 0.5;  // the song stands at its maximum now
    if (std::isinf(limit)) {
        if (!context.songMaxWidth) return false;
        if (ours) song.MaxWidth(*context.songMaxWidth);  // a value someone else wrote since stays
        context.songMaxWidth.reset();
        return ours && binding;
    }
    if (!ours) context.songMaxWidth = current;
    context.songLimitedFor = width;
    context.songLimit = limit;
    if (std::abs(current - limit) < 0.5) return false;
    song.MaxWidth(limit);
    return limit < song.ActualWidth() - 0.5 || binding;
}

// A layout that spans the song over the toggles' column (floating-panels) leaves its end to Ampify: Apple's columns
// left the song a third of the bar, with an empty stretch before the volume, and the title cut in a narrow window. The
// song ends a clear space short of the toggles, whatever their number. A layout that sizes the song itself (a width,
// or another alignment) keeps it; a single column gets Apple's margin back.
// True when it gave the song a new margin, which the next layout pass lays out.
bool FitSongRoom(WindowContext& context) {
    xaml::FrameworkElement song = context.songColumn.get();
    xaml::FrameworkElement toggles = context.songToggles.get();
    auto parent = song ? media::VisualTreeHelper::GetParent(song).try_as<xaml::UIElement>() : nullptr;
    if (!song || !toggles || !parent) return false;
    bool ours = controls::Grid::GetColumnSpan(song) >= 2 && std::isnan(song.Width()) &&
                song.HorizontalAlignment() == xaml::HorizontalAlignment::Stretch;
    xaml::Thickness margin = song.Margin();
    if (!ours) {
        bool restored = context.songMargin.has_value();
        if (restored) song.Margin(*context.songMargin);
        context.songMargin.reset();
        return KeepSongClearOfTransport(context, song, parent) || restored;
    }
    if (KeepSongClearOfTransport(context, song, parent)) return true;  // gives Apple's maximum back
    if (!context.songMargin) context.songMargin = margin;
    if (song.ActualWidth() <= 0 || toggles.ActualWidth() <= 0) return false;
    double songLeft = song.TransformToVisual(parent).TransformPoint({0, 0}).X;
    double togglesLeft = toggles.TransformToVisual(parent).TransformPoint({0, 0}).X;
    double slotRight = songLeft + song.ActualWidth() + margin.Right;
    double right = std::max(context.songMargin->Right, slotRight - (togglesLeft - kSongClearance));
    if (std::abs(right - margin.Right) < 0.5) return false;
    song.Margin({margin.Left, margin.Top, right, margin.Bottom});
    return true;
}

constexpr double kPlaybarClearance = 16;       // Ampify's buttons stay that far from what stands left of them
constexpr double kDisplayRoom = 340;           // a display narrower cuts a title short (Apple keeps ~390 when narrow)
constexpr double kUnfoldMargin = 8;            // room past that before a folded button comes back: no back and forth
constexpr char kMoreGlyph[] = "\xEE\x9C\x92";  // U+E712, "More" in Segoe Fluent Icons


double LeftOf(const xaml::FrameworkElement& element) {
    return element.TransformToVisual(nullptr).TransformPoint({0, 0}).X;
}

// The toggles' spare room, negative when they lack some: their slot in the bar less their width (a bar too narrow cuts
// them at its edge), their distance less kPlaybarClearance to what stands left of them (the transport, and Apple's
// display unless Ampify fits the song itself: FitSongRoom keeps that one clear), and, while the display takes what
// the toggles leave it (no width from the theme), its width less kDisplayRoom.
std::optional<double> PlaybarRoom(const WindowContext& context, const xaml::FrameworkElement& toggles) {
    if (toggles.ActualWidth() <= 0) return std::nullopt;
    xaml::Thickness margin = toggles.Margin();
    double room = std::numeric_limits<double>::infinity();
    if (toggles.HorizontalAlignment() != xaml::HorizontalAlignment::Stretch) {  // stretched, they fill their slot
        room = primitives::LayoutInformation::GetLayoutSlot(toggles).Width -
               (toggles.ActualWidth() + margin.Left + margin.Right);
    }
    double togglesLeft = LeftOf(toggles);
    auto measure = [&](const xaml::FrameworkElement& element) {
        if (!element || element.ActualWidth() <= 0 || element.Visibility() != xaml::Visibility::Visible) return;
        double left = LeftOf(element);
        if (left >= togglesLeft) return;  // a layout put it right of the toggles: not in their way
        room = std::min(room, togglesLeft - (left + element.ActualWidth()) - kPlaybarClearance);
    };
    measure(context.transport.get());
    if (!context.songMargin) measure(context.songDisplay.get());
    xaml::FrameworkElement display = context.songDisplay.get();
    xaml::FrameworkElement song = context.songColumn.get();
    if (display && song && display.ActualWidth() > 0 && std::isnan(display.Width()) && std::isnan(song.Width())) {
        double wanted = std::min({kDisplayRoom, display.MaxWidth(), song.MaxWidth()});
        room = std::min(room, display.ActualWidth() - wanted);
    }
    if (std::isinf(room)) return std::nullopt;
    return room;
}

// Ampify's clickable buttons among the toggles, in their order (PlacedBefore).
std::vector<PlacedControl*> PlaybarButtons(WindowContext& context, const controls::Panel& toggles) {
    std::vector<PlacedControl*> buttons;
    for (auto& [key, placed] : context.placed) {
        std::uint32_t index = 0;
        if (!placed.button || !placed.clickable || placed.elementId.empty()) continue;
        if (toggles.Children().IndexOf(placed.button, index)) buttons.push_back(&placed);
    }
    std::sort(buttons.begin(), buttons.end(),
              [](const PlacedControl* a, const PlacedControl* b) { return PlacedBefore(*a, b->elementId, b->id); });
    return buttons;
}

// The entries of the "More" menu: the folded buttons in their order, each doing what its button does.
void FillPlaybarMenu(const controls::MenuFlyout& menu, WindowContext& context, const UiEventSink& sink) {
    menu.Items().Clear();
    auto toggles = context.songToggles.get().try_as<controls::Panel>();
    if (!toggles) return;
    for (PlacedControl* placed : PlaybarButtons(context, toggles)) {
        if (!placed->folded) continue;
        controls::ToggleMenuFlyoutItem item;
        item.Text(Wide(placed->label));
        item.IsChecked(placed->active);
        if (auto icon = MakeIconElement(placed->icon, 16, L"TextFillColorPrimaryBrush").try_as<controls::IconElement>()) {
            item.Icon(icon);
        }
        std::string elementId = placed->elementId;
        int id = placed->id;
        item.Click([sink, elementId, id](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
            try {
                if (sink) sink(UiEvent{elementId, id, true, {}});
            } catch (...) {
            }
        });
        menu.Items().Append(item);
    }
}

// The "More" button, sized and styled like Ampify's button it follows (which took Apple's neighbour's look).
controls::Button MakePlaybarMore(const controls::Button& model, const UiEventSink& sink, const WeakContext& weakContext) {
    controls::Button more;
    if (model.Style()) more.Style(model.Style());
    more.Background(model.Background());
    more.BorderThickness(model.BorderThickness());
    more.Width(model.Width());
    more.Height(model.Height());
    more.MinWidth(model.MinWidth());
    more.MinHeight(model.MinHeight());
    more.Padding(model.Padding());
    more.Margin(model.Margin());
    more.VerticalAlignment(model.VerticalAlignment());
    more.Content(MakeIconElement(kMoreGlyph, 16, L"TextFillColorPrimaryBrush"));
    std::string label = i18n::Text("ui.more");
    controls::ToolTipService::SetToolTip(more, winrt::box_value(Wide(label)));
    Name(more, uirender::ElementName("ampify", "playbar-more"), label);
    controls::MenuFlyout menu;
    // The menu comes as the sender: captured, it would hold itself through its own handler.
    menu.Opening([sink, weakContext](IInspectable const& sender, IInspectable const&) noexcept {
        try {
            if (auto live = weakContext.lock()) FillPlaybarMenu(sender.as<controls::MenuFlyout>(), *live, sink);
        } catch (...) {
        }
    });
    more.Flyout(menu);
    return more;
}

// Victor's ask of 05/11: in a narrow window, Ampify's buttons go one by one into a "More" button, the last one first,
// and come back as the room does, so the bar keeps room for the song and the transport. Apple's own buttons stay. One
// step per call: the layout that follows calls again (SizeChanged). Folding a single button would gain nothing.
void FoldPlaybarButtons(const UiEventSink& sink, const std::shared_ptr<WindowContext>& context) {
    xaml::FrameworkElement togglesElement = context->songToggles.get();
    auto toggles = togglesElement.try_as<controls::Panel>();
    if (!toggles) return;
    std::vector<PlacedControl*> buttons = PlaybarButtons(*context, toggles);
    auto shown = std::find_if(buttons.rbegin(), buttons.rend(), [](const PlacedControl* p) { return !p->folded; });
    auto folded = std::find_if(buttons.begin(), buttons.end(), [](const PlacedControl* p) { return p->folded; });
    auto countFolded = [&buttons]() {
        return static_cast<std::size_t>(
            std::count_if(buttons.begin(), buttons.end(), [](const PlacedControl* p) { return p->folded; }));
    };
    std::size_t foldedCount = countFolded();
    std::optional<double> room = PlaybarRoom(*context, togglesElement);
    auto fold = [](PlacedControl& placed) {
        placed.width = placed.button.ActualWidth() + placed.button.Margin().Left + placed.button.Margin().Right;
        placed.folded = true;
        placed.button.Visibility(xaml::Visibility::Collapsed);
    };
    auto unfold = [](PlacedControl& placed) {
        placed.folded = false;
        placed.button.Visibility(xaml::Visibility::Visible);
    };
    if (room && *room < 0 && shown != buttons.rend()) {
        if (foldedCount == 0) {
            if (buttons.size() < 2) return;
            fold(**shown);  // the "More" button takes the first one's place: a second one goes with it
            ++shown;
        }
        fold(**shown);
    } else if (foldedCount > 0 && room) {
        // The first folded button comes back, needing its width; the last two come back together, in place of the
        // "More" button, which needs about the same.
        double needed = (*folded)->width;
        if (*room >= needed + kUnfoldMargin) {
            unfold(**folded);
            if (foldedCount == 2) unfold(**std::next(folded));
        }
    }
    if (countFolded() == 0) {
        if (context->playbarMore) context->playbarMore.Visibility(xaml::Visibility::Collapsed);
        return;
    }
    if (!context->playbarMore) context->playbarMore = MakePlaybarMore(buttons.front()->button, sink, context);
    std::uint32_t index = 0;
    if (!toggles.Children().IndexOf(context->playbarMore, index)) {
        RemoveFromParent(context->playbarMore);
        toggles.Children().Append(context->playbarMore);
    }
    context->playbarMore.Visibility(xaml::Visibility::Visible);
}

// Fits the song and folds the buttons once the layout pass is over, a single time for all the sizes it changed: within
// the pass, the widths read are not yet those of the step just taken, and folding on them went back and forth until
// WinUI ended Apple Music for a layout cycle. Each step lays out again, which queues the next one.
void QueueSongRoom(const UiEventSink& sink, const std::shared_ptr<WindowContext>& context) noexcept {
    try {
        if (context->songRoomQueued || !context->dispatcher) return;
        WeakContext weakContext = context;
        context->songRoomQueued =
            context->dispatcher.TryEnqueue(dispatching::DispatcherQueuePriority::Low, [weakContext, sink]() noexcept {
                try {
                    auto live = weakContext.lock();
                    if (!live) return;
                    live->songRoomQueued = false;
                    // A new margin is laid out first: the widths read now would be those before it.
                    if (!FitSongRoom(*live)) FoldPlaybarButtons(sink, live);
                } catch (...) {
                }
            });
    } catch (...) {
    }
}

void FollowSongRoom(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    const std::vector<ProbeNode>& nodes = context->probe;
    std::optional<int> display = uirender::FindByName(nodes, "LCD");
    std::optional<int> toggles = uirender::FindByName(nodes, "NowPlayingToggleButtons");
    if (!display || !toggles) return;
    auto displayElement = context->probeObjects[*display].get().try_as<xaml::FrameworkElement>();
    auto togglesElement = context->probeObjects[*toggles].get().try_as<xaml::FrameworkElement>();
    auto song = displayElement ? media::VisualTreeHelper::GetParent(displayElement).try_as<xaml::FrameworkElement>()
                               : nullptr;
    if (!song || !togglesElement || context->songColumn.get() == song) return;
    context->songColumn = winrt::make_weak(song);
    context->songToggles = winrt::make_weak(togglesElement);
    context->songMargin.reset();
    context->songMaxWidth.reset();
    WeakContext weakContext = context;
    std::optional<int> transport = uirender::FindByName(nodes, "TransportControl");
    context->songDisplay = winrt::make_weak(displayElement);
    if (transport) {
        if (auto element = context->probeObjects[*transport].get().try_as<xaml::FrameworkElement>()) {
            context->transport = winrt::make_weak(element);
        }
    }
    UiEventSink sink = shared.sink;
    auto update = [weakContext, sink]() noexcept {
        if (auto live = weakContext.lock()) QueueSongRoom(sink, live);
    };
    auto sized = [update](IInspectable const&, xaml::SizeChangedEventArgs const&) noexcept { update(); };
    song.SizeChanged(sized);
    togglesElement.SizeChanged(sized);
    // The window growing moves the toggles without resizing them: the bar's own size says so.
    if (auto bar = media::VisualTreeHelper::GetParent(togglesElement).try_as<xaml::FrameworkElement>()) {
        bar.SizeChanged(sized);
    }
    for (const xaml::DependencyProperty& property : {controls::Grid::ColumnSpanProperty(),
                                                     xaml::FrameworkElement::WidthProperty(),
                                                     xaml::FrameworkElement::HorizontalAlignmentProperty()}) {
        song.RegisterPropertyChangedCallback(
            property, [update](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept { update(); });
    }
    FitSongRoom(*context);
    shared.Once("song-room", "Interface: the playback bar's song follows the room the toggles leave it");
}

// Keeps the page the host leaves in the history of the visit. A visit starts again after Apple Music's navigation
// closed the host, and a link to the page just left (the Marketplace's list after an uninstall) goes back instead.
void RememberRoute(WindowContext& context, const std::pair<std::string, std::string>& next) {
    auto& history = context.routeHistory;
    if (!context.route || context.pageClosed) {
        history.clear();
        return;
    }
    if (*context.route == next) return;
    if (!history.empty() && history.back() == next) {
        history.pop_back();
        return;
    }
    history.push_back(*context.route);
    if (history.size() > kMaxRouteHistory) history.erase(history.begin());
}

// Ampify's settings page, in the host of the app pages while the route points to it.
void MountAmpifyPage(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                     const xaml::DependencyObject& content) {
    if (!context->route || context->route->second != kAmpifyPagePath || context->pageClosed) return;
    controls::Border slot = EnsurePageHost(shared, context, content);
    if (!slot) return;
    if (!context->ampifyPage || context->ampifyPageStale) {
        context->ampifyPage = BuildAmpifyPage(shared);
        context->ampifyPageStale = false;
        shared.journal.Write("Settings: Ampify page built");
    }
    if (slot.Child() != context->ampifyPage) slot.Child(context->ampifyPage);
    if (context->pageTitle && context->pageTitle.Text() != L"Ampify") context->pageTitle.Text(L"Ampify");
    ShowPageHost(*context, true);
    CoverApplePage(context);
}

void ApplySurfaces(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                   const xaml::DependencyObject& content) {
    std::vector<Surface> surfaces;
    std::optional<std::pair<std::string, std::string>> route;
    bool applePlayer = false;
    bool playerCheck = false;
    uirender::PlayerMode playerMode;
    bool main = context->property == L"MainWindow";
    bool closeToTray = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        surfaces = shared.surfaces;
        route = std::exchange(shared.pendingRoute, std::nullopt);
        applePlayer = main && std::exchange(shared.applePlayerPending, false);
        playerCheck = main && std::exchange(shared.playerCheckPending, false);
        playerMode = shared.playerMode;
        closeToTray = shared.closeToTray;
    }
    if (main) FollowCloseRule(context->window.get(), closeToTray, shared.journal);
    if (applePlayer) OpenApplePlayer(context->window.get(), shared.journal, playerMode);
    else if (playerCheck) FollowApplePlayer(context->window.get(), shared.journal, playerMode);
    if (route) {
        context->backPending = false;  // a navigation came after the click: it wins
        RememberRoute(*context, *route);
    } else if (std::exchange(context->backPending, false) && !context->routeHistory.empty()) {
        route = context->routeHistory.back();
        context->routeHistory.pop_back();
    }
    if (route) {
        context->route = route;
        context->pageClosed = false;
        if (route->second == kAmpifyPagePath) context->ampifyPageStale = true;  // what is installed may have changed
        if (context->pageScroll) {
            context->pageScroll.ChangeView(nullptr, winrt::Windows::Foundation::IReference<double>(0.0), nullptr, true);
        }
        ShowPageHost(*context, false);
        for (auto& [key, mounted] : context->surfaces) {
            if (mounted.surface.kind == SurfaceKind::Page) mounted.surface.revision = 0;  // render again for the new route
        }
    }

    std::set<std::string> present;
    for (const Surface& surface : surfaces) present.insert(surface.key);
    for (auto it = context->surfaces.begin(); it != context->surfaces.end();) {
        if (present.count(it->first)) {
            ++it;
            continue;
        }
        if (shared.goneReports++ < 5) {
            shared.journal.Write("Interface: \"" + it->first + "\" is no longer in the extension model, removed");
        }
        Unmount(*context, it->second);
        context->closedPanels.erase(it->first);
        it = context->surfaces.erase(it);
    }
    for (auto it = context->closedPanels.begin(); it != context->closedPanels.end();) {
        it = present.count(*it) ? std::next(it) : context->closedPanels.erase(it);  // the element stopped: shown again next time
    }

    for (const Surface& surface : surfaces) {
        try {
            switch (surface.kind) {
                case SurfaceKind::Panel: MountPanel(shared, context, content, surface); break;
                case SurfaceKind::Page: MountPage(shared, context, content, surface); break;
                case SurfaceKind::Popup: MountPopup(shared, context, content, surface); break;
                case SurfaceKind::Fullscreen: MountFullscreen(shared, context, content, surface); break;
                case SurfaceKind::Control: break;
            }
        } catch (const winrt::hresult_error& error) {
            shared.journal.Write("Interface: render of \"" + SurfaceLabel(surface) + "\" failed (" +
                                 HresultText(error.code().value) + ")");
        } catch (const std::exception& error) {
            shared.journal.Write("Interface: render of \"" + SurfaceLabel(surface) + "\" failed (" + error.what() + ")");
        }
    }
    try {
        MountAmpifyPage(shared, context, content);
    } catch (const winrt::hresult_error& error) {
        shared.journal.Write("Settings: the Ampify page failed (" + HresultText(error.code().value) + ")");
    }

    bool anyPanel = false;
    for (const auto& [key, mounted] : context->surfaces) {
        anyPanel = anyPanel || (mounted.surface.kind == SurfaceKind::Panel && mounted.panelFrame);
    }
    if (context->panelHost) context->panelHost.Visibility(anyPanel ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    // Mounting returns early when nothing changed: the overlays follow the window here, on every refresh. A Popup is
    // outside the window's tree, so it takes the light or dark mode of the window by hand (a light theme left the
    // panels dark).
    if (auto element = content.try_as<xaml::FrameworkElement>(); element && element.XamlRoot()) {
        if (context->panelPopup && context->panelHost) PlacePanelPopup(*context, element.XamlRoot());
        PlacePageHost(*context, element.XamlRoot());
        PlaceFullscreen(*context, element.XamlRoot());
        xaml::ElementTheme mode = element.ActualTheme();
        for (xaml::FrameworkElement overlay : {xaml::FrameworkElement(context->panelHost), xaml::FrameworkElement(context->pageHost),
                                               xaml::FrameworkElement(context->fullscreenHost)}) {
            if (overlay && overlay.RequestedTheme() != mode) overlay.RequestedTheme(mode);
        }
        // Another overlay opened after the fullscreen view: the view opens again, on top of it.
        if (context->fullscreenPopup && context->fullscreenPopup.IsOpen() &&
            context->fullscreenRaisedAt != context->overlayOpens) {
            context->fullscreenPopup.IsOpen(false);
            context->fullscreenPopup.IsOpen(true);
            context->fullscreenRaisedAt = context->overlayOpens;
            if (context->fullscreenClose) context->fullscreenClose.Focus(xaml::FocusState::Programmatic);
        }
    }

    try {
        PlaceControls(shared, context, surfaces);
    } catch (...) {
        shared.Once("controls-failed", "Interface: button placement failed");
    }
}

// ---- One refresh of one window, on its interface thread ---------------------------------------------------------

void Refresh(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    if (context->ticket.Closed() || !context->dispatcher.HasThreadAccess()) return;
    xaml::Window window = context->window.get();
    if (!window) {
        context->ticket.Close();
        ReleaseXaml(*context);
        return;
    }
    if (AppWindow(context->property) != window) {
        // The property holds another window, or none for a moment (GetAppProperty failed): this work is stale. The
        // ticket stays open, as in AMExclusive, so a passing failure does not end the window; the context and its XAML
        // objects go once the window object is gone (DiscoverWindows, DropContext).
        return;
    }
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = context;
    if (!context->observed) {
        context->observed = true;
        window.Closed([weakContext](IInspectable const&, xaml::WindowEventArgs const&) noexcept {
            try {
                if (auto live = weakContext.lock()) {
                    live->ticket.Close();
                    ReleaseXaml(*live);
                }
            } catch (...) {
            }
        });
        shared.journal.Write("Interface: window \"" + WindowLabel(*context) +
                             "\" bound, first refresh run by its DispatcherQueue (thread " +
                             std::to_string(GetCurrentThreadId()) + ")");
    }

    auto content = window.Content().try_as<xaml::DependencyObject>();
    if (!content) return;
    if (content != context->root.get()) {
        ClearWatches(*context);
        if (auto old = context->root.get().try_as<xaml::FrameworkElement>(); old && context->rootLayoutActive) {
            old.LayoutUpdated(context->rootLayout);
            old.SizeChanged(context->rootSize);
        }
        context->root = winrt::make_weak(content);
        std::uint64_t revision = ++context->rootRevision;
        context->discoverDirty = true;
        context->surfaces.clear();
        context->placed.clear();
        UncoverApplePage(*context);
        try {
            ReleasePages(*context);  // the old content's grid: the new pages make room again when a panel shows
            if (context->panelPopup) context->panelPopup.IsOpen(false);  // the overlays of the old content
            if (context->pagePopup) context->pagePopup.IsOpen(false);
            if (context->fullscreenPopup) context->fullscreenPopup.IsOpen(false);
        } catch (...) {
        }
        ForgetFullscreenHost(*context);
        context->panelPopup = nullptr;
        context->panelHost = nullptr;
        context->panelStack = nullptr;
        context->pagePopup = nullptr;
        context->pageHost = nullptr;
        context->pageSlot = nullptr;
        context->pageScroll = nullptr;
        context->pageTitle = nullptr;
        context->pageBackground = nullptr;
        context->ampifyPage = nullptr;
        context->routeHistory.clear();
        context->rootLayoutActive = false;
        // LayoutUpdated until the content is mounted: the first pages appear a few layout passes later.
        if (auto element = content.try_as<xaml::FrameworkElement>()) {
            context->rootLayout = element.LayoutUpdated([weakShared, weakContext, revision](IInspectable const&,
                                                                                           IInspectable const&) noexcept {
                try {
                    auto live = weakContext.lock();
                    if (live && live->ticket.OnLayout(NowMs())) Wake(weakShared, weakContext, revision, true);
                } catch (...) {
                }
            });
            // The overlays (panels, app pages) follow the window's size.
            context->rootSize = element.SizeChanged([weakShared, weakContext, revision](IInspectable const&,
                                                                                       xaml::SizeChangedEventArgs const&) noexcept {
                Wake(weakShared, weakContext, revision, false);
            });
            context->rootLayoutActive = true;
            context->ticket.ArmLayout(NowMs());
        }
    }

    if (context->discoverDirty.exchange(false)) Discover(shared, context, content);
    WatchSettingsPage(shared, context);
    if (context->property == L"MainWindow") {
        WatchSettingsEntry(shared, context);
        FollowFoldedLogo(shared, context);
        FollowSongRoom(shared, context);
        ApplySurfaces(shared, context, content);
    }
}

// Asks AMP.Utils for the windows, keeps one context per live window, and queues a refresh of each.
void DiscoverWindows(UiService::Shared& shared) {
    if (!AppPropertyAvailable()) {
        shared.Once("no-amp-utils", "Interface: AMP.Utils.dll or GetAppProperty not available yet, the XAML window "
                                    "cannot be reached (retrying every 5 s)");
        return;
    }
    std::vector<std::shared_ptr<WindowContext>> toRefresh;
    std::vector<std::shared_ptr<WindowContext>> dropped;
    for (const wchar_t* property : {L"MainWindow", L"MiniPlayerWindow"}) {
        xaml::Window window = AppWindow(property);
        if (!window) continue;
        dispatching::DispatcherQueue dispatcher{nullptr};
        try {
            dispatcher = window.DispatcherQueue();  // documented as callable from any thread
        } catch (...) {
        }
        if (!dispatcher) continue;
        std::shared_ptr<WindowContext> context;
        bool created = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto stale = std::stable_partition(shared.windows.begin(), shared.windows.end(),
                                               [](const std::shared_ptr<WindowContext>& entry) {
                                                   // A closed window stays listed while its object lives (closed
                                                   // ticket: no more work), so it is not taken up again as new.
                                                   return static_cast<bool>(entry->window.get());
                                               });
            dropped.insert(dropped.end(), std::make_move_iterator(stale), std::make_move_iterator(shared.windows.end()));
            shared.windows.erase(stale, shared.windows.end());
            for (const auto& entry : shared.windows) {
                if (entry->window.get() == window) context = entry;
            }
            if (!context) {
                context = std::make_shared<WindowContext>();
                context->property = property;
                context->window = winrt::make_weak(window);
                context->dispatcher = dispatcher;
                shared.windows.push_back(context);
                created = true;
            }
        }
        if (created) {
            shared.journal.Write("Interface: window \"" + winrt::to_string(property) +
                                 "\" obtained through AMP.Utils (GetAppProperty), work sent to its DispatcherQueue");
        }
        context->discoverDirty = true;
        toRefresh.push_back(context);
    }
    for (auto& context : dropped) DropContext(std::move(context));
    dropped.clear();
    if (toRefresh.empty()) shared.Once("no-window", "Interface: AMP.Utils does not give a \"MainWindow\" window yet");
    for (const auto& context : toRefresh) QueueRefresh(shared.weak_from_this(), context);
}

// Reads the Apple Music part of the Ampify page again when settings.txt changed: the notification rule applies at
// once, the player mode at the next opening of the full-screen player.
void ReadAppleTweaks(UiService::Shared& shared, std::optional<std::filesystem::file_time_type>& stamp) {
    std::error_code error;
    auto written = std::filesystem::last_write_time(shared.kit / "settings.txt", error);
    if (error || (stamp && *stamp == written)) return;
    stamp = written;
    Settings settings;
    settings.Load((shared.kit / "settings.txt").string());
    uirender::AppleTweaks tweaks = uirender::ReadAppleTweaks(settings);
    bool closeChanged = false;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.playerMode = tweaks.player;
        closeChanged = std::exchange(shared.closeToTray, tweaks.closeToTray) != tweaks.closeToTray;
    }
    SetNotificationRule(tweaks.notifications, shared.journal);
    SetPlayerControlsShown(tweaks.playerControls);
    SetSharpArtwork(tweaks.sharpArtwork);
    SetTextMarquee(tweaks.textMarquee);
    const bool inHeader = uirender::ReadEntryInHeader(settings);
    const bool entryMoved = shared.entryInHeader.exchange(inHeader) != inHeader;

    std::string active = uirender::ActiveThemeId(shared.kit);
    std::vector<std::shared_ptr<WindowContext>> windows;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        bool themeChanged = active != shared.cardTheme;
        // The close rule and Ampify's entry reach the window at its next refresh, queued here.
        if (!themeChanged && !closeChanged && !entryMoved) return;
        if (themeChanged) {
            shared.cardTheme = active;
            shared.cardStale = true;
        }
        windows = shared.windows;
    }
    for (const auto& context : windows) {
        if (context->property == L"MainWindow") QueueRefresh(shared.weak_from_this(), context);
    }
}

// A window showed: the next refresh of the main window looks for Apple's full-screen player to lay it out.
void QueuePlayerCheck(const std::shared_ptr<UiService::Shared>& shared) {
    std::vector<std::shared_ptr<WindowContext>> windows;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->playerCheckPending = true;
        windows = shared->windows;
    }
    for (const auto& context : windows) {
        if (context->property == L"MainWindow") QueueRefresh(shared, context);
    }
}

}  // namespace

UiService::UiService(Journal& journal, std::filesystem::path kit, UiModel& model, UiEventSink sink)
    : m_shared(std::make_shared<Shared>(journal, std::move(kit), std::move(sink), model)), m_model(model) {}

UiService::~UiService() {
    Stop();
}

void UiService::Start() {
    if (m_shared->running.exchange(true)) return;
    m_worker = std::thread([this] { Run(); });
}

void UiService::Stop() {
    m_shared->running = false;
    if (m_worker.joinable()) m_worker.join();
    ShowMainWindowAgain();  // never leave Apple Music without its window
    ReleaseCloseToTray();
}

void UiService::Run() {
    EnsureMta();
    // Out of context: the callback runs on this thread, from its message queue, pumped below.
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, WindowEvent,
                                         GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNTHREAD);
    m_shared->journal.Write(std::string("Interface: service started (WinEvent hook ") + (hook ? "set" : "refused") +
                            ", polling the windows every 5 s, the model every 250 ms)");

    uirender::SurfaceTracker tracker;
    auto nextDiscovery = std::chrono::steady_clock::now();
    auto nextTweaks = nextDiscovery;
    std::optional<std::filesystem::file_time_type> tweaksStamp;
    while (m_shared->running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        try {
            auto now = std::chrono::steady_clock::now();
            if (g_windowEvent.exchange(false) || now >= nextDiscovery) {
                DiscoverWindows(*m_shared);
                nextDiscovery = now + kDiscoveryPoll;
            }
            if (now >= nextTweaks) {
                ReadAppleTweaks(*m_shared, tweaksStamp);
                nextTweaks = now + kTweaksPoll;
            }
            if (g_windowShown.exchange(false)) QueuePlayerCheck(m_shared);
            // An element whose fullscreen view another element took hears it as a close (the sink only queues it).
            for (const auto& [elementId, session] : m_model.TakeDisplacedFullscreens()) {
                if (m_shared->sink) m_shared->sink(FullscreenClosed(elementId, std::to_string(session)));
            }
            uirender::SurfaceChanges changes = tracker.Poll(m_model);
            if (!changes.Empty()) {
                std::vector<std::shared_ptr<WindowContext>> windows;
                {
                    std::lock_guard<std::mutex> lock(m_shared->mutex);
                    m_shared->surfaces = tracker.Current();
                    if (changes.route) m_shared->pendingRoute = changes.route;
                    if (changes.applePlayer) m_shared->applePlayerPending = true;
                    windows = m_shared->windows;
                }
                for (const auto& context : windows) {
                    if (context->property == L"MainWindow") QueueRefresh(m_shared, context);
                }
            }
        } catch (const winrt::hresult_error& error) {
            m_shared->Once("worker-error", "Interface: error on the watching thread (" + HresultText(error.code().value) + ")");
        } catch (...) {
            m_shared->Once("worker-error", "Interface: error on the watching thread (unknown exception)");
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(kModelPoll.count()), QS_ALLINPUT);
    }
    if (hook) UnhookWinEvent(hook);
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    for (const auto& context : m_shared->windows) context->ticket.Close();
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
