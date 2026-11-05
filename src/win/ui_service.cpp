// Written against the C++/WinRT projection of Windows App SDK 1.8.
// Window discovery, page watching and the refresh tickets are adapted from AMExclusive (MIT,
// https://github.com/AntiOblivionis/AMExclusive, mod/src/UiAdapter.cpp: ScheduleUiDiscovery, QueueUiRefresh,
// ObserveUiHosts, RefreshUiWindow, AttachOrRefreshToggle, at 91f589d).

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"
#include "sidebar_hover.hpp"

namespace ampify::win {

namespace ui_service_detail {

namespace {

constexpr auto kModelPoll = std::chrono::milliseconds(250);
constexpr auto kDiscoveryPoll = std::chrono::seconds(5);
constexpr auto kTweaksPoll = std::chrono::seconds(1);  // settings.txt's date, for the Apple Music part of the card
constexpr std::size_t kDiscoveryLimit = 512;

constexpr std::size_t kMaxWatches = 64;

std::atomic<bool> g_windowEvent{true};  // set by the WinEvent hook, read by the worker
std::atomic<bool> g_windowShown{false};  // a top-level window showed: perhaps Apple's full-screen player

void CALLBACK WindowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG objectId, LONG childId, DWORD, DWORD) {
    if ((event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_SHOW) || objectId != OBJID_WINDOW ||
        childId != CHILDID_SELF || !window || GetAncestor(window, GA_ROOT) != window) {
        return;
    }
    // An HWND does not say which XAML window it belongs to: the worker asks AMP.Utils again.
    g_windowEvent = true;
    if (event == EVENT_OBJECT_SHOW) g_windowShown = true;
}

}  // namespace

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

namespace {

void Refresh(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context);

std::string WindowLabel(const WindowContext& context) {
    return winrt::to_string(context.property);
}

}  // namespace

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

namespace {

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
    SetSidebarOnHover(tweaks.sidebarOnHover);
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

}  // namespace ui_service_detail

using namespace ui_service_detail;

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
