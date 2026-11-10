// The extensions' side panels: their frame, the popup that holds them, its place beside Apple Music's pages
// (docked in a theme's column, or pushing the pages aside), and their slide in and out.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

namespace ampify::win::ui_service_detail {

namespace {

constexpr double kPanelWidth = 380;

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
        // Nothing may escape into Apple Music's XAML frame.
    }
}

}  // namespace

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
                        // A handler runs inside Apple Music's XAML frame: nothing may escape it.
                    }
                });
                return;
            }
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
}

namespace {

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

}  // namespace

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

namespace {

// The bottom of the strip at the window's top that holds its buttons and takes the drags, 0 when it is not found.
double TitlebarBottom(const WindowContext& context) noexcept {
    try {
        if (auto titlebar = NamedBounds(context, "Titlebar")) return titlebar->Y + titlebar->Height;
    } catch (...) {
        // Leaving the tree while it is measured: the next refresh places the panels again.
    }
    return 0;
}

}  // namespace

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

// Opens one of the overlays under the fullscreen view, and counts it so the view goes back on top (ApplySurfaces).
void OpenOverlay(WindowContext& context, const primitives::Popup& popup) {
    if (popup.IsOpen()) return;
    popup.IsOpen(true);
    context.overlayOpens++;
}

namespace {

// Docked, the column is the card: each panel's own card (MakeSurfaceFrame) loses its background, border and padding,
// so a panel reads as part of the column. Floating again, the cards come back.
void DressPanelFrames(WindowContext& context, bool docked) {
    if (!context.panelStack) return;
    xaml::Thickness border = docked ? xaml::Thickness{} : xaml::Thickness{1, 1, 1, 1};
    // Docked, the column keeps no padding of its own: each card holds it, so a card filled with its panel's colour
    // (FillPanelFrame) reaches the column's edges.
    xaml::Thickness padding = docked ? xaml::Thickness{16, 16, 16, 16} : xaml::Thickness{16, 12, 16, 16};
    for (const xaml::UIElement& child : context.panelStack.Children()) {
        auto frame = child.try_as<controls::Border>();
        if (!frame || frame.BorderThickness() == border) continue;
        frame.BorderThickness(border);
        frame.Padding(padding);
        if (auto fill = frame.Tag().try_as<media::Brush>()) {
            frame.Background(fill);
        } else if (docked) {
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
    // Ampify's own room (PushPages), without a theme that leaves one: the column is part of Apple's flat layout, like
    // its sidebar, so it meets the play bar, the window's right edge and its bottom, and keeps one gap from the pages
    // only (Victor, 2026-11-10: a frame of grey showed above it and on its right). A theme's room keeps its gaps.
    controls::Grid parent = PagesParent(pages);
    const bool ours = parent && context.pushedPages == parent && parent.Padding().Right >= kPanelWidth + 8;
    // Without a theme, Apple's pages run under its see-through play bar at the window's top: the column starts below
    // the bar instead of over its buttons, a gap below it in a theme's room.
    const float barGap = ours ? 0.0f : 8.0f;
    if (auto bar = NamedBounds(context, "TransportBar"); bar && bar->Y < size.Height / 2) {
        float top = bar->Y + bar->Height + barGap;
        if (bounds.Y < top) {
            bounds.Height = std::max(0.0f, bounds.Height - (top - bounds.Y) - barGap);
            bounds.Y = top;
        }
    }
    // A theme's room is the pages' margin: Apple's lyrics and queue pane, when open, starts right after it, and the
    // pages' parent runs under that pane, so measuring up to the parent's edge would put the column over the pane.
    // Ampify's own room is the parent's padding (PushPages): that pane, a child of the same grid, opens inside it,
    // between the pages and the room, so the column takes the parent's right edge.
    double start = bounds.X + bounds.Width;
    double room = std::min(RoomRight(pages), static_cast<double>(size.Width) - start);
    if (double padding = parent ? parent.Padding().Right : 0; padding >= kPanelWidth + 8) {
        double parentRight = parent.TransformToVisual(nullptr).TransformPoint({0, 0}).X + parent.ActualWidth();
        room = std::min(padding, static_cast<double>(size.Width) - (parentRight - padding));
        start = parentRight - padding;
    }
    if (room < kPanelWidth + 8) return false;
    double gap = ours ? room - kPanelWidth : (room - kPanelWidth) / 2;  // ours: the gap on the pages' side only
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
    if (!context.panelDocked) context.panelHost.Padding(xaml::Thickness{});
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
// and a gap, and DockPanelPopup docks them there, against the window's right edge.
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
    context.pushedRight = context.pagesPadding.Right + kPanelWidth + kPushGap;  // the column meets the window's edge
    padding.Right = context.pushedRight;
    parent.Padding(padding);
    parent.UpdateLayout();  // so the column docks at once rather than floating for one refresh
}

}  // namespace

// The pages take their width back once no panel shows.
void ReleasePages(WindowContext& context) {
    if (!context.pushedPages) return;
    if (context.pushedPages.Padding().Right == context.pushedRight) context.pushedPages.Padding(context.pagesPadding);
    context.pushedPages = nullptr;
    context.pushedRight = -1;
}

namespace {

constexpr double kPagesMinWidth = 720;  // narrower, a playlist's header cut its buttons: the panels float instead
constexpr double kDockMargin = 8;       // room past that before the pages make room again: no back and forth

// Whether the pages keep kPagesMinWidth beside a column of panels, measured on the pages as they are, pushed or not.
bool PagesKeepRoom(const WindowContext& context) {
    controls::Grid pages = PagesGrid(context);
    if (!pages || pages.ActualWidth() <= 0) return true;
    double width = pages.ActualWidth();
    if (context.pushedPages) return width >= kPagesMinWidth;
    return width - (kPanelWidth + kPushGap) >= kPagesMinWidth + kDockMargin;
}

}  // namespace

// Places the Popup of the panels on the right of the window, under the play bar. Called on every refresh and when
// the window changes size, so the panels follow it.
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

namespace {

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

}  // namespace

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

namespace {

constexpr float kPanelSlide = 48;  // how far a panel comes from, on the right
constexpr auto kPanelIn = std::chrono::milliseconds(280);
constexpr auto kPanelOut = std::chrono::milliseconds(200);  // leaving is quicker than coming

// Runs on the compositor thread, once the panel is in the window.
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
}

}  // namespace

// A panel taken out slides back to the right and fades, then leaves the column; the column itself goes when it is
// empty, so a theme's docked column does not stay behind as an empty card. The pages take their room back only then:
// laying Apple's page out again as the slide starts held its first frames, and the panel jumped out instead.
void SlidePanelOut(WindowContext& context, const xaml::FrameworkElement& frame) {
    if (!frame) return;
    if (!frame.IsHitTestVisible()) return;  // already leaving
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
    WeakContext weakContext = context.weak_from_this();
    controls::StackPanel stack = context.panelStack;
    primitives::Popup popup = context.panelPopup;
    timer->Interval(kPanelOut);
    timer->IsRepeating(false);
    timer->Tick([timer, frame, stack, popup, weakContext](auto&&, auto&&) mutable noexcept {
        try {
            RemoveFromParent(frame);
            if (auto live = weakContext.lock(); live && !AnyPanelShown(*live)) ReleasePages(*live);
            if (stack && stack.Children().Size() == 0 && popup && popup.IsOpen()) popup.IsOpen(false);
            timer->Stop();
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
        timer.reset();  // the timer held this handler, which held the timer
    });
    timer->Start();
}

namespace {

constexpr double kFrameRadius = 8;  // a floating panel's corners while the theme gives its panels none

// The theme's --panel-radius (panel_radius.hpp): a floating frame takes it, and what the element draws inside keeps
// within it, so square corners reach the panels too. A docked column already takes the pages' corners.
void FollowPanelRadius(UiService::Shared& shared, const WindowContext& context, MountedSurface& mounted,
                       const Surface& surface) {
    std::optional<double> radius = PublishedPanelRadius();
    if (!mounted.renderer) mounted.renderer = std::make_unique<XamlRenderer>(surface.elementId, shared.sink);
    mounted.renderer->SetCornerLimit(radius ? *radius : std::numeric_limits<double>::infinity());
    if (context.panelDocked || !mounted.panelFrame) return;
    auto frame = mounted.panelFrame.try_as<controls::Border>();
    double corner = radius ? *radius : kFrameRadius;
    if (frame && frame.CornerRadius().TopLeft != corner) frame.CornerRadius(xaml::CornerRadius{corner, corner, corner, corner});
}

media::Brush BackgroundOf(const xaml::UIElement& element) {
    if (auto border = element.try_as<controls::Border>()) return border.Background();
    if (auto panel = element.try_as<controls::Panel>()) return panel.Background();
    if (auto control = element.try_as<controls::Control>()) return control.Background();
    return nullptr;
}

void ClearBackground(const xaml::UIElement& element) {
    if (auto border = element.try_as<controls::Border>()) border.Background(nullptr);
    else if (auto panel = element.try_as<controls::Panel>()) panel.Background(nullptr);
    else if (auto control = element.try_as<controls::Control>()) control.Background(nullptr);
}

// A panel whose root has a background fills its whole card with it, title and edges included: the song panel's
// colour stopped a margin short of the card, which stayed grey around it (and the column, docked). The root gives
// its brush to the card, so a see-through colour is not laid twice; the card keeps it in its Tag for DressPanelFrames.
// Docked, the card takes the column's corners.
void FillPanelFrame(const WindowContext& context, MountedSurface& mounted) {
    auto frame = mounted.panelFrame.try_as<controls::Border>();
    xaml::UIElement root = mounted.slot ? mounted.slot.Child() : nullptr;
    if (!frame || !root) return;
    if (media::Brush brush = BackgroundOf(root)) {
        ClearBackground(root);
        frame.Tag(brush);
        frame.Background(brush);
        mounted.filledRoot = root;
    } else if (frame.Tag() && root != mounted.filledRoot) {
        // A new root without a background (nothing playing): the card takes its own look back.
        frame.Tag(nullptr);
        mounted.filledRoot = nullptr;
        if (context.panelDocked) {
            frame.Background(media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        } else if (auto plain = ThemeResource(L"AcrylicInAppFillColorDefaultBrush").try_as<media::Brush>()) {
            frame.Background(plain);
        }
    }
    if (context.panelDocked && frame.Tag() && context.panelHost) {
        xaml::CornerRadius corners = context.panelHost.CornerRadius();
        if (frame.CornerRadius() != corners) frame.CornerRadius(corners);
    }
}

}  // namespace

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
        UiEvent closed{surface.elementId, 0, false, "{\"panel\":" + Json::Quote(surface.id) + "}", dispatch::kPanelClose};
        close.Click([weakContext, weakShared, key, label, journal, closed](IInspectable const& sender,
                                                                         xaml::RoutedEventArgs const&) noexcept {
            try {
                auto live = weakContext.lock();
                if (!live) return;
                if (SharedPtr owner = weakShared.lock(); owner && owner->sink) owner->sink(closed);
                live->closedPanels.insert(key);
                auto found = live->surfaces.find(key);
                if (found != live->surfaces.end()) {
                    SlidePanelOut(*live, found->second.panelFrame);  // as smooth as a panel its element takes out
                    live->surfaces.erase(found);
                }
                journal->Write("Interface: panel \"" + label + "\" closed by the user");
                (void)sender;
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
    FollowPanelRadius(shared, *context, mounted, surface);
    uirender::PatchStats stats = RenderInto(shared, mounted, surface);
    FillPanelFrame(*context, mounted);
    JournalRender(shared, "panel", surface, stats, first);
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
