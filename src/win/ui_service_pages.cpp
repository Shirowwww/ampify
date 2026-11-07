// The app pages beside Apple Music's frame, the popups (dialogs) and the fullscreen view, and the mounting of every
// surface of the model in one window (ApplySurfaces).

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include "dialog_fonts.hpp"

namespace ampify::win::ui_service_detail {

namespace {

constexpr std::size_t kMaxRouteHistory = 16;

// The frame of Apple Music's pages, from the last walk of the window.
controls::Frame ContentFrame(const WindowContext& context) {
    std::optional<int> index = uirender::FindByName(context.probe, "ContentFrame");
    if (!index || *index >= static_cast<int>(context.probeObjects.size())) return nullptr;
    return context.probeObjects[*index].get().try_as<controls::Frame>();
}

// Puts the host in the grid of Apple Music's frame, in its cell and just above it: the app page lives in the layer of
// Apple's pages, under the sidebar's pane when it opens over them (a Popup drew over the pane). Put back if Apple
// Music takes it out.
bool AttachPageHost(WindowContext& context) {
    controls::Frame frame = ContentFrame(context);
    auto grid = frame ? media::VisualTreeHelper::GetParent(frame).try_as<controls::Panel>() : nullptr;
    if (!grid) return false;
    if (media::VisualTreeHelper::GetParent(context.pageHost) != grid) {
        RemoveFromParent(context.pageHost);
        std::uint32_t at = 0;
        grid.Children().IndexOf(frame, at);
        grid.Children().InsertAt(at + 1, context.pageHost);
    }
    controls::Grid::SetRow(context.pageHost, controls::Grid::GetRow(frame));
    controls::Grid::SetColumn(context.pageHost, controls::Grid::GetColumn(frame));
    controls::Grid::SetRowSpan(context.pageHost, controls::Grid::GetRowSpan(frame));
    controls::Grid::SetColumnSpan(context.pageHost, controls::Grid::GetColumnSpan(frame));
    // Apple's modal state moves the frame's z-index: the host follows it, so Apple's modal still covers both.
    int z = controls::Canvas::GetZIndex(frame);
    if (controls::Canvas::GetZIndex(context.pageHost) != z) controls::Canvas::SetZIndex(context.pageHost, z);
    return true;
}

// Keeps the host clear of Apple Music's see-through play bar, which the frame runs under, above or below it.
void PlacePageHost(WindowContext& context, const xaml::XamlRoot& root) {
    if (!context.pageHost || !media::VisualTreeHelper::GetParent(context.pageHost)) return;
    std::optional<winrt::Windows::Foundation::Rect> frame = NamedBounds(context, "ContentFrame");
    if (!frame) return;
    ContentArea area = FindContentArea(context, root.Size().Height);
    double top = std::max(0.0, area.top - frame->Y);
    double bottom = area.height > 0 ? std::max(0.0, frame->Y + frame->Height - (area.top + area.height)) : 0.0;
    xaml::Thickness margin{0, top, 0, bottom};
    if (context.pageHost.Margin() != margin) context.pageHost.Margin(margin);
}

}  // namespace

// Shows Apple Music's page again, and gives the host its own background back. Safe to call when nothing is covered.
// The sidebar's selection goes back as it was, unless Apple Music's navigation closed the page (it selects for itself)
// or the host only hides while the route changes (Keep: the next cover finds the selection already cleared).
void UncoverApplePage(WindowContext& context, Selection selection) noexcept {
    try {
        if (auto frame = std::exchange(context.coveredFrame, nullptr)) {
            frame.Navigated(context.coveredNavigated);
            frame.Navigating(context.coveredNavigating);
            frame.Opacity(context.coveredOpacity);
        }
        if (auto sidebar = std::exchange(context.coveredSidebar, nullptr)) {
            sidebar.ItemInvoked(context.coveredInvoked);
            if (selection == Selection::GiveBack && context.coveredSelection && !sidebar.SelectedItem()) {
                sidebar.SelectedItem(context.coveredSelection);
            }
        }
        if (selection != Selection::Keep) context.coveredSelection = nullptr;
        if (context.pageHost && context.pageBackground) context.pageHost.Background(context.pageBackground);
    } catch (...) {
    }
}

// Takes the watch of WatchSelectionBars off and lets the bars go. Interface thread only, as the bars are XAML.
void ForgetSelectionBars(WindowContext& context) noexcept {
    try {
        if (context.barsSidebar && context.barsToken) context.barsSidebar.SelectionChanged(context.barsToken);
    } catch (...) {
    }
    context.barsSidebar = nullptr;
    context.barsToken = {};
    context.barsSelection = nullptr;
    context.hiddenBars.clear();
}

namespace {

// True when the host is shown: not before Apple's frame is found, which the next refresh tries again.
bool ShowPageHost(WindowContext& context, bool visible, Selection selection = Selection::GiveBack) {
    if (!context.pageHost) return false;
    if (visible && !AttachPageHost(context)) return false;
    context.pageHost.Visibility(visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    if (visible) return true;
    UncoverApplePage(context, selection);
    RemoveFromParent(context.pageHost);  // Apple's grid holds only its own children while no app page shows
    return false;
}

constexpr int kPaneDepth = 12;          // the pane's grid sits 7 levels under the NavigationView
constexpr int kSelectionBarDepth = 26;  // and the bars 14 to 20 levels under that grid (1.1792, nested groups)

// The first element named `name` under `parent`, depth first, no deeper than `depth` levels.
xaml::DependencyObject FindNamedBelow(const xaml::DependencyObject& parent, std::wstring_view name, int depth) {
    if (depth <= 0) return nullptr;
    int32_t count = media::VisualTreeHelper::GetChildrenCount(parent);
    for (int32_t i = 0; i < count; i++) {
        xaml::DependencyObject child = media::VisualTreeHelper::GetChild(parent, i);
        auto element = child.try_as<xaml::FrameworkElement>();
        if (element && element.Name() == name) return child;
        if (auto found = FindNamedBelow(child, name, depth - 1)) return found;
    }
    return nullptr;
}

// The NavigationView puts out only the bar it lit itself: an item lit before its row was realized (the page Apple
// Music opens on, in the folded column) kept its bar once the selection was cleared. Every lit bar of the pane goes
// out here, kept with its opacity for WatchSelectionBars.
void HideSelectionBars(WindowContext& context, const xaml::DependencyObject& parent, int depth) {
    if (depth <= 0) return;
    int32_t count = media::VisualTreeHelper::GetChildrenCount(parent);
    for (int32_t i = 0; i < count; i++) {
        xaml::DependencyObject child = media::VisualTreeHelper::GetChild(parent, i);
        auto element = child.try_as<xaml::FrameworkElement>();
        if (element && element.Name() == L"SelectionIndicator") {
            if (element.Opacity() != 0) {
                context.hiddenBars.emplace_back(element, element.Opacity());
                element.Opacity(0);
            }
            continue;
        }
        HideSelectionBars(context, child, depth - 1);
    }
}

// WinUI, which did not light those bars, does not light them again when their item comes back either: the next
// selection of the sidebar puts them back when it is the item they were lit for. Another item lights its own bar.
void WatchSelectionBars(const std::shared_ptr<WindowContext>& owner, const controls::NavigationView& sidebar) {
    if (owner->hiddenBars.empty() || owner->barsSidebar) return;
    WeakContext weakContext = owner;
    owner->barsSidebar = sidebar;
    owner->barsSelection = owner->coveredSelection;
    using ChangedArgs = controls::NavigationViewSelectionChangedEventArgs;
    owner->barsToken = sidebar.SelectionChanged(
        [weakContext](controls::NavigationView const& sender, ChangedArgs const&) noexcept {
            try {
                auto live = weakContext.lock();
                IInspectable selected = live ? sender.SelectedItem() : nullptr;
                if (!selected) return;  // Ampify's own clearing, or nothing selected yet
                if (selected == live->barsSelection) {
                    for (auto& [bar, opacity] : live->hiddenBars) bar.Opacity(opacity);
                }
                ForgetSelectionBars(*live);
            } catch (...) {
            }
        });
}

// Apple Music lights the sidebar item of its page: while an app page shows, none of Apple's items is lit (Ampify's
// entry is), as on Apple's own Settings page. The item comes back when the app page closes (UncoverApplePage).
void ClearSidebarSelection(const std::shared_ptr<WindowContext>& owner, const controls::NavigationView& sidebar) {
    if (auto selected = sidebar.SelectedItem()) {
        owner->coveredSelection = selected;
        sidebar.SelectedItem(nullptr);
    }
    if (auto pane = FindNamedBelow(sidebar, L"PaneContentGrid", kPaneDepth)) {
        HideSelectionBars(*owner, pane, kSelectionBarDepth);
        WatchSelectionBars(owner, sidebar);
    }
}

// The app page's own back: to the page shown before in this visit (a detail view of the Marketplace to its list), or,
// from the first page of the visit, the host closes on Apple Music's page under it.
void GoBackInPage(const WeakShared& weakShared, const std::shared_ptr<WindowContext>& context) {
    if (!context->routeHistory.empty()) {
        context->backPending = true;
        QueueRefresh(weakShared, context);
        return;
    }
    context->pageClosed = true;
    ShowPageHost(*context, false);
}

// Hides Apple Music's page under the shown app page: the host turns see-through, so the app page sits on the theme's
// panel (its colour, acrylic, corners) as Apple Music's pages do. A navigation of Apple Music (sidebar, back arrow,
// search) closes the app page, as leaving a Spicetify app does.
void CoverApplePage(UiService::Shared& shared, const std::shared_ptr<WindowContext>& owner) {
    WindowContext& context = *owner;
    controls::Frame frame = ContentFrame(context);
    if (!frame || !context.pageHost || frame == context.coveredFrame) return;
    UncoverApplePage(context, Selection::Keep);
    WeakContext weakContext = owner;
    WeakShared weakShared = shared.weak_from_this();
    // Apple's back (its arrow, the top bar's, the mouse's back button) would leave the page under the app page, a step
    // too far: it does what the app page's arrow does instead, so every back arrow agrees.
    using NavigatingArgs = xaml::Navigation::NavigatingCancelEventArgs;
    context.coveredNavigating = frame.Navigating([weakShared, weakContext](IInspectable const&,
                                                                            NavigatingArgs const& args) noexcept {
        try {
            auto live = weakContext.lock();
            if (!live || args.NavigationMode() != xaml::Navigation::NavigationMode::Back) return;
            args.Cancel(true);
            GoBackInPage(weakShared, live);
        } catch (...) {
        }
    });
    auto leave = [weakContext]() noexcept {
        try {
            if (auto live = weakContext.lock()) {
                live->pageClosed = true;
                ShowPageHost(*live, false, Selection::Drop);
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
            ClearSidebarSelection(owner, sidebar);
        }
    }
    context.coveredFrame = frame;
    context.coveredOpacity = frame.Opacity();
    frame.Opacity(0);
    if (!context.pageBackground) context.pageBackground = context.pageHost.Background();
    context.pageHost.Background(media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));  // still takes clicks
}

// The host of an app page: a grid beside Apple Music's frame while the route points to the page (AttachPageHost).
// Apple Music's own frame is not replaced.
controls::Border EnsurePageHost(UiService::Shared& shared, const std::shared_ptr<WindowContext>& owner,
                                const xaml::DependencyObject& content) {
    WindowContext& context = *owner;
    auto rootElement = content.try_as<xaml::UIElement>();
    xaml::XamlRoot xamlRoot = rootElement ? rootElement.XamlRoot() : nullptr;
    if (!xamlRoot) return nullptr;
    if (context.pageSlot) return context.pageSlot;
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

    // The handler holds the window context weakly, not the host: the host holds the button that holds the handler.
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = owner;
    back.Click([weakShared, weakContext](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            if (auto live = weakContext.lock()) GoBackInPage(weakShared, live);
        } catch (...) {
        }
    });
    context.pageHost = host;
    context.pageSlot = slot;
    context.pageScroll = scroll;
    context.pageTitle = title;
    shared.Once("page-host", "Interface: app page host made, beside Apple Music's frame");
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
    if (first || mounted.surface.revision != surface.revision) {
        if (first) mounted.renderer.reset();
        mounted.slot = slot;
        mounted.title = context->pageTitle;
        uirender::PatchStats stats = RenderInto(shared, mounted, surface);
        mounted.mounted = true;
        JournalRender(shared, "page", surface, stats, first);
    }
    // On every refresh: the host waits for Apple's frame, and goes back beside it if Apple Music took it out.
    if (!context->pageClosed && ShowPageHost(*context, true)) CoverApplePage(shared, context);
}

// The dialog of a popup. Close, alone, stands for the default button: Apple's plain style with Ampify's accent
// template, as Ampify's other primary buttons (Apple's AccentButtonStyle draws text in WebDings). Its font, and that of
// the content's buttons: GiveDialogButtonsTextFont, once open.
controls::ContentDialog MakePopupDialog(UiService::Shared& shared, const Surface& surface,
                                        const xaml::FrameworkElement& root, const xaml::XamlRoot& xamlRoot) {
    controls::ContentDialog dialog;
    if (auto style = ThemeResource(L"DefaultContentDialogStyle").try_as<xaml::Style>()) dialog.Style(style);
    dialog.XamlRoot(xamlRoot);
    dialog.Title(winrt::box_value(Wide(surface.title.empty() ? surface.elementId : surface.title)));
    dialog.Content(root);
    dialog.CloseButtonText(Wide(i18n::Text("ui.close")));
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
    dialog.Opened([](controls::ContentDialog const& sender, controls::ContentDialogOpenedEventArgs const&) noexcept {
        try {
            GiveDialogButtonsTextFont(sender);
        } catch (...) {
        }
    });
    return dialog;
}

// A named popup closed by the user leaves the element's model (entry "popupClose"), so rendering the same name opens
// it again; until the model drops it, `dismissed` keeps it shut. Unnamed ones ("#n") are new at each open.
void WatchPopupClose(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context, const Surface& surface,
                     const controls::ContentDialog& dialog) {
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = context;
    std::string key = surface.key;
    std::optional<UiEvent> closedEvent;
    if (!surface.id.empty() && surface.id[0] != '#') {
        closedEvent = UiEvent{surface.elementId, 0, false, "{\"popup\":" + Json::Quote(surface.id) + "}", "popupClose"};
    }
    dialog.Closed([weakShared, weakContext, key, closedEvent](controls::ContentDialog const& sender,
                                                              controls::ContentDialogClosedEventArgs const&) noexcept {
        try {
            auto live = weakContext.lock();
            if (!live) return;
            live->dialogOpen = false;
            auto found = live->surfaces.find(key);
            // Still this surface's dialog: the user closed it. One the model removed is gone from `surfaces` already.
            bool byUser = found != live->surfaces.end() && found->second.dialog == sender;
            if (SharedPtr owner = weakShared.lock(); byUser && closedEvent && owner && owner->sink) owner->sink(*closedEvent);
            if (found != live->surfaces.end()) {
                found->second.dismissed = true;
                found->second.dialog = nullptr;
            }
            QueueRefresh(weakShared, live);  // the next popup, if one waits
        } catch (...) {
        }
    });
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
        GiveDialogButtonsTextFont(dialog);  // the buttons of new content too
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
    controls::ContentDialog dialog = MakePopupDialog(shared, surface, root, rootElement.XamlRoot());
    WatchPopupClose(shared, context, surface, dialog);
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

}  // namespace

// ---- The fullscreen view ------------------------------------------------------------------------------------------
// A Popup of the window that covers all of its content, above Apple Music and the other overlays, with a Close button
// at the top right. Escape closes it too. It lives outside Apple Music's pages, so their navigation leaves it alone.

UiEvent FullscreenClosed(const std::string& elementId, const std::string& session) {
    return UiEvent{elementId, 0, false, "{\"session\":" + session + "}", "fullscreenClose"};
}

namespace {

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

constexpr double kFullscreenCloseBand = 64;  // the Close button's margin (16) and height (38), and a gap

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
    // The view starts under the Close button's band, so its first line is never drawn beneath the button.
    slot.Margin(xaml::Thickness{0, kFullscreenCloseBand, 0, 0});
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
    if (ShowPageHost(*context, true)) CoverApplePage(shared, context);
}

}  // namespace

void ApplySurfaces(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                   const xaml::DependencyObject& content) {
    std::vector<Surface> surfaces;
    std::optional<std::pair<std::string, std::string>> route;
    bool applePlayer = false;
    bool playerCheck = false;
    uirender::PlayerMode playerMode;
    bool main = context->role == WindowRole::Main;
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
        ShowPageHost(*context, false, Selection::Keep);
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
        for (xaml::FrameworkElement overlay : {xaml::FrameworkElement(context->panelHost),
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

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
