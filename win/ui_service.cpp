// NOT RUN YET IN APPLE MUSIC: see ui_service.hpp. Written against the C++/WinRT projection of Windows App SDK 1.8.
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
#include <chrono>
#include <iterator>
#include <map>
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
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "../core/ui_render/mods.hpp"
#include "../core/ui_render/probe.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "../core/ui_render/surfaces.hpp"
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
constexpr std::size_t kDiscoveryLimit = 512;
constexpr std::size_t kPageLimit = 4000;
constexpr std::size_t kMaxWatches = 64;
constexpr double kPanelWidth = 380;

std::atomic<bool> g_windowEvent{true};  // set by the WinEvent hook, read by the worker
int g_modsUpdating = 0;                 // interface thread only: > 0 while the Mods card sets its toggles itself

std::string Utf8(const winrt::hstring& text) {
    return winrt::to_string(text);
}

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
    // Everything the kit makes is held strongly: in Apple Music a weak reference to a XAML object the kit made never
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
    controls::Button button{nullptr};  // strong, like the panels: see MountedSurface
    bool reported = false;
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

    // Main window only.
    // Strong, like the Popup: the panels stay ours even when Apple Music rebuilds the children of its page (in the
    // bench, a host added to MainPage's Grid went away within seconds and the panel was mounted again and again).
    controls::Grid panelHost{nullptr};
    controls::StackPanel panelStack{nullptr};
    int panelHostsMade = 0;
    // Strong: nothing else holds a Popup that is outside the tree, and its panels went with it (mounted again on every
    // refresh, never on screen). Released on the interface thread (ReleaseXaml, DropContext).
    primitives::Popup panelPopup{nullptr};
    primitives::Popup pagePopup{nullptr};  // app pages, over the content area
    controls::Grid pageHost{nullptr};      // strong, like the panels: see MountedSurface
    controls::Border pageSlot{nullptr};
    controls::TextBlock pageTitle{nullptr};
    std::map<std::string, MountedSurface> surfaces;
    std::map<std::string, PlacedControl> placed;
    std::set<std::string> closedPanels;
    std::optional<std::pair<std::string, std::string>> route;
    bool pageClosed = false;
    bool dialogOpen = false;
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

    // Interface thread only.
    int detachedReports = 0;  // journal lines about panels taken out of their host, at most 5
    int goneReports = 0;      // journal lines about surfaces gone from the model, at most 5
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
                                                   shared->journal.Write("Interface : rafraîchissement de « " +
                                                                         WindowLabel(*live) + " » en échec (" +
                                                                         HresultText(error.code().value) + ")");
                                               } catch (...) {
                                                   shared->journal.Write("Interface : rafraîchissement de « " +
                                                                         WindowLabel(*live) +
                                                                         " » en échec (exception inconnue)");
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

// Drops what the window holds of XAML: the watches, the panel popup, the renderers and their controls, the placed
// buttons. Interface thread only: XAML objects must not be released from the worker, which drops the contexts of
// closed or replaced windows (DiscoverWindows).
void ReleaseXaml(WindowContext& context) noexcept {
    try {
        ClearWatches(context);
        if (context.panelPopup) context.panelPopup.IsOpen(false);
        if (context.pagePopup) context.pagePopup.IsOpen(false);
    } catch (...) {
    }
    try {
        context.panelPopup = nullptr;
        context.pagePopup = nullptr;
        context.panelHost = nullptr;
        context.panelStack = nullptr;
        context.pageHost = nullptr;
        context.pageSlot = nullptr;
        context.pageTitle = nullptr;
        context.surfaces.clear();
        context.placed.clear();
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

// Bounded breadth-first walk of the window content (512 nodes), never into item lists, buttons or the kit's
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
    shared.Once("discover|" + WindowLabel(*context) + "|" + std::to_string(revision),
                "Interface : découverte de « " + WindowLabel(*context) + " » : " +
                    std::to_string(context->probe.size()) + " nœud(s) parcouru(s), " + std::to_string(pages) +
                    " page(s), " + std::to_string(context->watches.size()) + " conteneur(s) suivi(s)");
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
    labels.Children().Append(MakeText(Wide(mod.name).c_str(), "body", false, &shared.probe));
    if (!mod.description.empty()) labels.Children().Append(MakeText(Wide(mod.description).c_str(), "caption", true, &shared.probe));
    row.Children().Append(labels);

    controls::ToggleSwitch toggle;
    if (shared.probe.toggle) toggle.Style(shared.probe.toggle);
    toggle.MinWidth(0);
    toggle.VerticalAlignment(xaml::VerticalAlignment::Center);
    toggle.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    Name(toggle, "AMPIFY_ModsToggle_" + mod.id, mod.name);
    automation::AutomationProperties::SetHelpText(toggle, L"Active ou coupe cette extension tout de suite");
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
                journal->Write("Réglages : extension « " + id + " » " + (on ? "activée" : "coupée") +
                               " depuis la carte Mods (" + (on ? "start " : "stop ") + id + " demandé)");
            } else {
                journal->Write("Réglages : impossible de changer l'extension « " + id + " » : " + error);
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

controls::Border BuildModsCard(UiService::Shared& shared, const std::vector<uirender::ModEntry>& mods,
                               const xaml::FrameworkElement& templateRow) {
    controls::Border card = MakeCardBorder(shared.probe);
    Name(card, "AMPIFY_ModsCard", "Mods (Ampify)");
    if (templateRow) {
        card.Margin(templateRow.Margin());
        card.HorizontalAlignment(templateRow.HorizontalAlignment());
        card.MaxWidth(templateRow.MaxWidth());
    } else if (shared.probe.cardMargin) {
        card.Margin(*shared.probe.cardMargin);
    } else {
        card.Margin(xaml::Thickness{0, 4, 0, 0});
    }

    controls::StackPanel body;
    body.Spacing(12);

    controls::Grid header;
    header.ColumnSpacing(16);
    controls::ColumnDefinition text;
    text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition button;
    button.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    header.ColumnDefinitions().Append(text);
    header.ColumnDefinitions().Append(button);
    controls::StackPanel titles;
    titles.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::TextBlock title = MakeText(L"Mods (Ampify)", "bodyStrong", false, &shared.probe);
    Name(title, "AMPIFY_ModsTitle");
    titles.Children().Append(title);
    titles.Children().Append(MakeText(L"Les extensions installées dans le kit. Un changement s'applique tout de suite.",
                                      "caption", true, &shared.probe));
    header.Children().Append(titles);
    controls::Button open;
    if (shared.probe.button) open.Style(shared.probe.button);
    open.Content(winrt::box_value(L"Ouvrir le dossier du kit"));
    open.VerticalAlignment(xaml::VerticalAlignment::Center);
    Name(open, "AMPIFY_ModsOpenFolder", "Ouvrir le dossier du kit");
    std::filesystem::path kit = shared.kit;
    open.Click([kit](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            OpenKitFolder(kit);
        } catch (...) {
        }
    });
    // The Marketplace page (apps/marketplace): thèmes, schémas, extensions, in the app.
    controls::Button market;
    if (auto accent = ThemeResource(L"AccentButtonStyle").try_as<xaml::Style>()) market.Style(accent);
    market.Content(winrt::box_value(L"Ouvrir la Marketplace"));
    market.VerticalAlignment(xaml::VerticalAlignment::Center);
    Name(market, "AMPIFY_ModsOpenMarketplace", "Ouvrir la Marketplace");
    UiModel* model = &shared.model;
    Journal* journal = &shared.journal;
    market.Click([model, journal](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            model->Navigate("marketplace", "/marketplace");
            journal->Write("Réglages : Marketplace ouverte depuis la carte Mods");
        } catch (...) {
        }
    });
    controls::StackPanel actions;
    actions.Orientation(controls::Orientation::Horizontal);
    actions.Spacing(8);
    actions.Children().Append(market);
    actions.Children().Append(open);
    controls::Grid::SetColumn(actions, 1);
    header.Children().Append(actions);
    body.Children().Append(header);

    controls::StackPanel list;
    list.Spacing(10);
    Name(list, "AMPIFY_ModsList");
    if (mods.empty()) {
        list.Children().Append(MakeText(L"Aucune extension installée pour l'instant. Ajoute-en avec « ampify install », puis "
                                        L"rouvre cette page.",
                                        "caption", true, &shared.probe));
    }
    for (const uirender::ModEntry& mod : mods) {
        list.Children().Append(MakeDivider());
        list.Children().Append(ModRow(shared, mod));
    }
    body.Children().Append(list);
    card.Child(body);
    return card;
}

// Adds the card at the end of the StackPanel of the page that holds the most ToggleSwitch, or rebuilds it there when
// it is already present (so the list follows what is installed). AMExclusive's cards are never touched.
bool MountModsCard(UiService::Shared& shared, const xaml::FrameworkElement& page) {
    std::vector<ProbeNode> nodes;
    std::vector<xaml::DependencyObject> objects;
    CollectPage(page, nodes, objects);
    std::optional<int> anchor = uirender::ChooseSettingsAnchor(nodes, 0);
    if (!anchor) {
        shared.Once("mods-no-anchor", "Réglages : page Lecture trouvée, mais sans liste de réglages à interrupteurs (" +
                                          std::to_string(nodes.size()) + " nœuds) : carte Mods non ajoutée");
        return false;
    }
    auto stack = objects[*anchor].try_as<controls::StackPanel>();
    if (!stack) return false;

    StyleProbe probe;
    CaptureStyleProbe(page, probe, "PlaybackSettingsPage");
    shared.probe = probe;

    xaml::FrameworkElement templateRow{nullptr};
    if (std::optional<int> row = uirender::ChooseTemplateRow(nodes, *anchor)) templateRow = objects[*row].try_as<xaml::FrameworkElement>();

    std::vector<uirender::ModEntry> mods = uirender::ListMods(shared.kit);
    controls::Border card = BuildModsCard(shared, mods, templateRow);

    auto children = stack.Children();
    for (std::uint32_t index = 0; index < children.Size(); index++) {
        auto existing = children.GetAt(index).try_as<xaml::FrameworkElement>();
        if (existing && existing.Name() == L"AMPIFY_ModsCard") {
            children.SetAt(index, card);
            shared.journal.Write("Réglages : carte « Mods (Ampify) » rafraîchie (" +
                                 std::to_string(mods.size()) + " extension(s))");
            return true;
        }
    }
    children.Append(card);
    shared.journal.Write("Réglages : carte « Mods (Ampify) » ajoutée à la page Lecture (" +
                         std::to_string(mods.size()) + " extension(s), " + std::to_string(probe.Found()) +
                         " valeur(s) de style natif copiée(s))");
    return true;
}

void WatchSettingsPage(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
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
            shared.journal.Write("Réglages : page PlaybackSettingsPage repérée, en attente de son chargement");
        }
    }
    if (page && !context->settingsMounted && page.IsLoaded()) {
        if (MountModsCard(shared, page)) {
            context->settingsMounted = true;
            context->ticket.StopLayout();
        }
    }
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
    Name(close, frameName + "_fermer", "Fermer le panneau");
    controls::ToolTipService::SetToolTip(close, winrt::box_value(L"Fermer"));
    controls::Grid::SetColumn(close, 1);
    header.Children().Append(close);
    body.Children().Append(header);
    slot = controls::Border();
    body.Children().Append(slot);
    frame.Child(body);
    return frame;
}

// The part of the window under the play bar and right of the sidebar, in the window's coordinates, from the names the
// discovery found (Apple Music 1.1792: Grid#TransportBar at the top, Grid#PaneRoot on the left).
struct ContentArea {
    double left = 0;
    double top = 56;
};

ContentArea FindContentArea(const WindowContext& context) {
    ContentArea area;
    auto edge = [&context](std::string_view name, bool right) -> std::optional<double> {
        std::optional<int> index = uirender::FindByName(context.probe, name);
        if (!index || *index >= static_cast<int>(context.probeObjects.size())) return std::nullopt;
        auto element = context.probeObjects[*index].get().try_as<xaml::FrameworkElement>();
        if (!element || element.ActualWidth() <= 0 || element.ActualHeight() <= 0) return std::nullopt;
        auto origin = element.TransformToVisual(nullptr).TransformPoint({0, 0});
        return right ? origin.X + element.ActualWidth() : origin.Y + element.ActualHeight();
    };
    try {
        if (auto bottom = edge("TransportBar", false)) area.top = std::max(0.0, *bottom);
        if (auto right = edge("PaneRoot", true)) area.left = std::max(0.0, *right);
    } catch (...) {
        // An element leaving the tree while it is measured: the defaults hold until the next refresh.
    }
    return area;
}

// Places the Popup of the panels on the right of the window, under the play bar. Called on every refresh and when
// the window changes size, so the panels follow it.
void PlacePanelPopup(const WindowContext& context, const xaml::XamlRoot& root) {
    auto size = root.Size();
    ContentArea area = FindContentArea(context);
    double top = area.top + 8;
    double height = std::max(200.0, static_cast<double>(size.Height) - top - 16.0);
    double left = std::max(area.left, static_cast<double>(size.Width) - kPanelWidth - 16.0);
    if (context.panelHost.Height() != height) context.panelHost.Height(height);
    if (context.panelPopup.HorizontalOffset() != left) context.panelPopup.HorizontalOffset(left);
    if (context.panelPopup.VerticalOffset() != top) context.panelPopup.VerticalOffset(top);
    if (!context.panelPopup.IsOpen()) context.panelPopup.IsOpen(true);
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
        shared.journal.Write("Interface : hôte des panneaux recréé (n° " + std::to_string(context.panelHostsMade) + ")");
    }
    controls::Grid host;
    Name(host, "AMPIFY_PanelHost", "Panneaux des extensions");
    host.Width(kPanelWidth);
    host.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    host.VerticalAlignment(xaml::VerticalAlignment::Stretch);
    controls::ScrollViewer scroll;
    scroll.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
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
    shared.Once("panel-host", "Interface : hôte des panneaux ouvert dans un Popup de la fenêtre, à droite (" +
                                  Utf8(winrt::get_class_name(content)) + ")");
    return stack;
}

std::string SurfaceLabel(const Surface& surface) {
    return surface.elementId + "/" + surface.id;
}

// Renders a surface into its slot; returns the stats for the journal.
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
        shared.journal.Write(std::string("Interface : ") + what + " « " + SurfaceLabel(surface) + " » monté (" +
                             std::to_string(stats.created) + " élément(s), AutomationId " +
                             uirender::ElementName(surface.elementId, surface.nodeId) + ")");
    } else if (!stats.Unchanged()) {
        shared.journal.Write(std::string("Interface : ") + what + " « " + SurfaceLabel(surface) +
                             " » mis à jour sur place (créés " + std::to_string(stats.created) + ", modifiés " +
                             std::to_string(stats.updated) + ", reconstruits " + std::to_string(stats.replaced) +
                             ", retirés " + std::to_string(stats.removed) + ")");
    }
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
        // Something took the frame out of the host: put back, and said, so the journal shows who does it and when.
        stack.Children().Append(mounted.panelFrame);
        if (shared.detachedReports++ < 5) {
            shared.journal.Write("Interface : panneau « " + SurfaceLabel(surface) + " » sorti de son hôte, remis en place");
        }
    }
    bool first = !alive;
    if (first) {
        controls::TextBlock title{nullptr};
        controls::Border slot{nullptr};
        controls::Button close{nullptr};
        std::string frameName = uirender::ElementName(surface.elementId, surface.nodeId) + "_panneau";
        controls::Border frame = MakeSurfaceFrame(frameName, surface.title.empty() ? surface.id : surface.title, title, slot, close);
        WeakContext weakContext = context;
        std::string key = surface.key;
        std::string label = SurfaceLabel(surface);
        Journal* journal = &shared.journal;
        close.Click([weakContext, key, label, journal](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
            try {
                auto live = weakContext.lock();
                if (!live) return;
                live->closedPanels.insert(key);
                auto found = live->surfaces.find(key);
                if (found != live->surfaces.end()) {
                    RemoveFromParent(found->second.panelFrame);
                    live->surfaces.erase(found);
                }
                journal->Write("Interface : panneau « " + label + " » fermé (il revient au prochain démarrage de "
                               "l'extension)");
                (void)sender;
            } catch (...) {
            }
        });
        stack.Children().Append(frame);
        mounted.renderer.reset();
        mounted.panelFrame = frame;
        mounted.slot = slot;
        mounted.title = title;
    }
    uirender::PatchStats stats = RenderInto(shared, mounted, surface);
    JournalRender(shared, "panneau", surface, stats, first);
}

// Covers the content area (under the play bar, right of the sidebar) with the page host, or hides it.
void PlacePageHost(WindowContext& context, const xaml::XamlRoot& root) {
    if (!context.pagePopup || !context.pageHost) return;
    auto size = root.Size();
    ContentArea area = FindContentArea(context);
    double width = std::max(200.0, static_cast<double>(size.Width) - area.left);
    double height = std::max(200.0, static_cast<double>(size.Height) - area.top);
    if (context.pageHost.Width() != width) context.pageHost.Width(width);
    if (context.pageHost.Height() != height) context.pageHost.Height(height);
    if (context.pagePopup.HorizontalOffset() != area.left) context.pagePopup.HorizontalOffset(area.left);
    if (context.pagePopup.VerticalOffset() != area.top) context.pagePopup.VerticalOffset(area.top);
}

void ShowPageHost(WindowContext& context, bool visible) {
    if (!context.pagePopup || !context.pageHost) return;
    context.pageHost.Visibility(visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    if (context.pagePopup.IsOpen() != visible) context.pagePopup.IsOpen(visible);
}

// The host of an app page: a Popup of the window over the content area while the route points to the page (Apple
// Music takes out what the kit adds to its own panels). Apple Music's own frame is not replaced.
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
    Name(host, "AMPIFY_PageHost", "Page d'une app du kit");
    host.Padding(xaml::Thickness{32, 16, 32, 16});
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
    controls::Button back;
    back.Content(winrt::box_value(L"Retour"));
    Name(back, "AMPIFY_PageHost_retour", "Retour");
    controls::TextBlock title = MakeText(L"", "title", false);
    title.VerticalAlignment(xaml::VerticalAlignment::Center);
    header.Children().Append(back);
    header.Children().Append(title);
    host.Children().Append(header);

    controls::ScrollViewer scroll;
    controls::Grid::SetRow(scroll, 1);
    controls::Border slot;
    scroll.Content(slot);
    host.Children().Append(scroll);

    host.Visibility(xaml::Visibility::Collapsed);
    primitives::Popup popup;
    popup.XamlRoot(xamlRoot);
    popup.Child(host);

    // The handler holds the window context weakly, not the host: the host holds the button that holds the handler.
    WeakContext weakContext = owner;
    back.Click([weakContext](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            if (auto live = weakContext.lock()) {
                live->pageClosed = true;
                ShowPageHost(*live, false);
            }
        } catch (...) {
        }
    });
    context.pagePopup = popup;
    context.pageHost = host;
    context.pageSlot = slot;
    context.pageTitle = title;
    PlacePageHost(context, xamlRoot);
    shared.Once("page-host", "Interface : hôte des pages des apps ouvert dans un Popup de la fenêtre");
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
    if (!context->pageClosed) ShowPageHost(*context, true);
    JournalRender(shared, "page", surface, stats, first);
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
    dialog.CloseButtonText(L"Fermer");
    dialog.DefaultButton(controls::ContentDialogButton::Close);
    Name(dialog, uirender::ElementName(surface.elementId, surface.nodeId) + "_dialogue",
         surface.title.empty() ? surface.elementId : surface.title);

    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = context;
    std::string key = surface.key;
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

void Unmount(WindowContext& context, MountedSurface& mounted) {
    try {
        if (auto dialog = mounted.dialog) dialog.Hide();
        if (mounted.surface.kind == SurfaceKind::Panel) RemoveFromParent(mounted.panelFrame);
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

void PlaceControls(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                   const std::vector<Surface>& surfaces) {
    std::set<std::string> present;
    for (const Surface& surface : surfaces) {
        if (surface.kind != SurfaceKind::Control) continue;
        present.insert(surface.key);
        const UiControl& control = surface.control;
        if (control.placement != "topbar" && control.placement != "playbar") {
            shared.Once("menu-" + control.placement, "Interface : les entrées de menu (" + control.placement +
                                                         ") ne sont pas encore affichées (provisoire)");
            continue;
        }
        PlacedControl& placed = context->placed[surface.key];
        if (placed.button && placed.button.Parent()) continue;  // taken out by Apple Music: placed again

        std::optional<uirender::Insertion> insertion = control.placement == "playbar"
                                                           ? uirender::ChoosePlaybarInsertion(context->probe)
                                                           : uirender::ChooseTopbarInsertion(context->probe);
        controls::StackPanel parent{nullptr};
        if (insertion) parent = context->probeObjects[insertion->parent].get().try_as<controls::StackPanel>();
        if (!parent) {
            if (!placed.reported) {
                placed.reported = true;
                shared.journal.Write("Interface : emplacement « " + control.placement + " » introuvable pour le bouton « " +
                                     control.label + " » de " + control.elementId + " (repères provisoires)");
            }
            continue;
        }

        controls::Button button;
        if (insertion->styleFrom >= 0) {
            auto neighbour = context->probeObjects[insertion->styleFrom].get();
            if (!neighbour.try_as<controls::Button>()) {
                // A ToggleButton (Apple Music's lyrics or queue button): its style does not fit a Button, so the kit
                // button stays flat like it, and takes its size below.
                button.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
                button.BorderThickness(xaml::Thickness{0, 0, 0, 0});
            }
            if (auto native = neighbour.try_as<controls::Control>()) {
                if (auto style = native.Style(); style && neighbour.try_as<controls::Button>()) button.Style(style);
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
        if (icon) button.Content(icon);
        else button.Content(winrt::box_value(Wide(control.label)));
        controls::ToolTipService::SetToolTip(button, winrt::box_value(Wide(control.label)));
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
        auto children = parent.Children();
        auto after = insertion->after >= 0 ? context->probeObjects[insertion->after].get().try_as<xaml::UIElement>() : nullptr;
        if (after && children.IndexOf(after, index)) children.InsertAt(index + 1, button);
        else children.Append(button);
        placed.button = button;
        shared.journal.Write("Interface : bouton « " + control.label + " » de " + control.elementId + " placé dans « " +
                             control.placement + " » (repères provisoires)");
    }
    for (auto it = context->placed.begin(); it != context->placed.end();) {
        if (present.count(it->first)) {
            ++it;
            continue;
        }
        RemoveFromParent(it->second.button);
        it = context->placed.erase(it);
    }
}

void ApplySurfaces(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                   const xaml::DependencyObject& content) {
    std::vector<Surface> surfaces;
    std::optional<std::pair<std::string, std::string>> route;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        surfaces = shared.surfaces;
        route = std::exchange(shared.pendingRoute, std::nullopt);
    }
    if (route) {
        context->route = route;
        context->pageClosed = false;
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
            shared.journal.Write("Interface : « " + it->first + " » n'est plus dans le modèle des extensions, retiré");
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
                case SurfaceKind::Control: break;
            }
        } catch (const winrt::hresult_error& error) {
            shared.journal.Write("Interface : rendu de « " + SurfaceLabel(surface) + " » en échec (" +
                                 HresultText(error.code().value) + ")");
        } catch (const std::exception& error) {
            shared.journal.Write("Interface : rendu de « " + SurfaceLabel(surface) + " » en échec (" + error.what() + ")");
        }
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
        xaml::ElementTheme mode = element.ActualTheme();
        for (xaml::FrameworkElement overlay : {xaml::FrameworkElement(context->panelHost), xaml::FrameworkElement(context->pageHost)}) {
            if (overlay && overlay.RequestedTheme() != mode) overlay.RequestedTheme(mode);
        }
    }

    try {
        PlaceControls(shared, context, surfaces);
    } catch (...) {
        shared.Once("controls-failed", "Interface : placement des boutons en échec");
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
        shared.journal.Write("Interface : fenêtre « " + WindowLabel(*context) +
                             " » liée, premier rafraîchissement exécuté par sa DispatcherQueue (fil " +
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
        try {
            if (context->panelPopup) context->panelPopup.IsOpen(false);  // the overlays of the old content
            if (context->pagePopup) context->pagePopup.IsOpen(false);
        } catch (...) {
        }
        context->panelPopup = nullptr;
        context->panelHost = nullptr;
        context->panelStack = nullptr;
        context->pagePopup = nullptr;
        context->pageHost = nullptr;
        context->pageSlot = nullptr;
        context->pageTitle = nullptr;
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
    if (context->property == L"MainWindow") ApplySurfaces(shared, context, content);
}

// Asks AMP.Utils for the windows, keeps one context per live window, and queues a refresh of each.
void DiscoverWindows(UiService::Shared& shared) {
    if (!AppPropertyAvailable()) {
        shared.Once("no-amp-utils", "Interface : AMP.Utils.dll ou GetAppProperty indisponible pour l'instant, la fenêtre "
                                    "XAML n'est pas accessible (nouvel essai toutes les 5 s)");
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
            shared.journal.Write("Interface : fenêtre « " + winrt::to_string(property) +
                                 " » obtenue par AMP.Utils (GetAppProperty), travail envoyé à sa DispatcherQueue");
        }
        context->discoverDirty = true;
        toRefresh.push_back(context);
    }
    for (auto& context : dropped) DropContext(std::move(context));
    dropped.clear();
    if (toRefresh.empty()) shared.Once("no-window", "Interface : AMP.Utils ne donne pas encore de fenêtre « MainWindow »");
    for (const auto& context : toRefresh) QueueRefresh(shared.weak_from_this(), context);
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
}

void UiService::Run() {
    EnsureMta();
    // Out of context: the callback runs on this thread, from its message queue, pumped below.
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, WindowEvent,
                                         GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNTHREAD);
    m_shared->journal.Write(std::string("Interface : service démarré (crochet WinEvent ") + (hook ? "posé" : "refusé") +
                            ", sondage des fenêtres toutes les 5 s, du modèle toutes les 250 ms)");

    uirender::SurfaceTracker tracker;
    auto nextDiscovery = std::chrono::steady_clock::now();
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
            uirender::SurfaceChanges changes = tracker.Poll(m_model);
            if (!changes.Empty()) {
                std::vector<std::shared_ptr<WindowContext>> windows;
                {
                    std::lock_guard<std::mutex> lock(m_shared->mutex);
                    m_shared->surfaces = tracker.Current();
                    if (changes.route) m_shared->pendingRoute = changes.route;
                    windows = m_shared->windows;
                }
                for (const auto& context : windows) {
                    if (context->property == L"MainWindow") QueueRefresh(m_shared, context);
                }
            }
        } catch (const winrt::hresult_error& error) {
            m_shared->Once("worker-error", "Interface : erreur du fil de surveillance (" + HresultText(error.code().value) + ")");
        } catch (...) {
            m_shared->Once("worker-error", "Interface : erreur du fil de surveillance (exception inconnue)");
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(kModelPoll.count()), QS_ALLINPUT);
    }
    if (hook) UnhookWinEvent(hook);
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    for (const auto& context : m_shared->windows) context->ticket.Close();
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
