// What Ampify draws on the progress line of the playback bar, laid in the track of Apple's scrubber, over the line,
// and taking no click: the stretch a play bar button marks (Ampify.ui.playbar.mark), a tick at each end and a light
// shade between them; and the readout under the pointer (Ampify.ui.playbar.readout), a thin marker and the time of the
// song at that point, which follows the pointer at its own pace with no trip to the element.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include <cstdio>

#include <winrt/Microsoft.UI.Input.h>

#include "../core/hover_readout.hpp"
#include "../core/progress_mark.hpp"

namespace ampify::win::ui_service_detail {

namespace {

constexpr double kTick = 2;            // width of a tick, and of the readout's marker
constexpr double kTickOverhang = 3;    // how far a tick stands above and below the line
constexpr double kShadeOpacity = 0.3;  // the shade lightens the line, played or not, without hiding it
constexpr int kFadeMs = 180;
constexpr int kHoverFadeMs = 120;      // the readout comes and goes quicker than the marks: it follows a hand
constexpr double kLabelGap = 6;        // between the readout's label and the line
constexpr auto kHoverReport = std::chrono::milliseconds(50);  // how often onHover hears of a pointer that moves
constexpr int kSearchLimit = 400;      // the display holds about a hundred elements

// Theme resources in markup, so each part follows the light or dark mode of the bar it sits in (a brush looked up in
// the application's resources keeps the mode it was looked up in). The text brush reads on the played part (accent)
// and on the rest.
constexpr wchar_t kPartXaml[] =
    L"<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' HorizontalAlignment='Left' "
    L"VerticalAlignment='Top' CornerRadius='1' Background='{ThemeResource TextFillColorPrimaryBrush}'/>";
constexpr wchar_t kLabelXaml[] =
    L"<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' CornerRadius='4' Padding='6,2,6,3' "
    L"BorderThickness='1' Background='{ThemeResource ToolTipBackground}' BorderBrush='{ThemeResource "
    L"ToolTipBorderBrush}'><TextBlock FontSize='12' TextAlignment='Center' MinWidth='28' "
    L"Foreground='{ThemeResource TextFillColorPrimaryBrush}'/></Border>";

xaml::FrameworkElement FindBelow(const xaml::DependencyObject& root, std::wstring_view name) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < kSearchLimit; i++) {
        if (auto element = queue[i].try_as<xaml::FrameworkElement>(); element && element.Name() == name) return element;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

// The playback bar's scrubber: Apple's slider in the song's display.
xaml::FrameworkElement FindScrubber(const WindowContext& context) {
    xaml::FrameworkElement display = context.songDisplay.get();
    if (!display) {
        if (std::optional<int> lcd = uirender::FindByName(context.probe, "LCD")) {
            display = context.probeObjects[*lcd].get().try_as<xaml::FrameworkElement>();
        }
    }
    return display ? FindBelow(display, L"LCDScrubber") : nullptr;
}

// The track of the progress line: the Grid of the scrubber's template that holds its rectangles.
controls::Grid FindTrack(const xaml::FrameworkElement& scrubber) {
    xaml::FrameworkElement track = scrubber ? FindBelow(scrubber, L"HorizontalTemplate") : nullptr;
    return track ? track.try_as<controls::Grid>() : nullptr;
}

// Over every row and column of the track (the stretch may lie in the part not played): Apple's line sits in a row of
// its own, and a layer left in the first row
// was cut at that row's foot (only the tips of the ticks showed above the line).
void SpanTrack(const xaml::FrameworkElement& layer, const controls::Grid& track) {
    controls::Grid::SetRowSpan(layer, std::max<int32_t>(1, static_cast<int32_t>(track.RowDefinitions().Size())));
    controls::Grid::SetColumnSpan(layer, std::max<int32_t>(8, static_cast<int32_t>(track.ColumnDefinitions().Size())));
}

// Where Apple's line (its track rectangle) lies in `layer`: a theme or a snippet may have made it thicker or moved it.
std::pair<double, double> LineIn(const xaml::FrameworkElement& layer) {
    double top = 0;
    double height = layer.ActualHeight();
    if (auto track = layer.Parent().try_as<xaml::FrameworkElement>()) {
        if (xaml::FrameworkElement rect = FindBelow(track, L"HorizontalTrackRect"); rect && rect.ActualHeight() > 0) {
            top = rect.TransformToVisual(layer).TransformPoint({0, 0}).Y;
            height = rect.ActualHeight();
        }
    }
    return {top, height};
}

controls::Border MakePart(double opacity) {
    controls::Border part{nullptr};
    try {
        part = xaml::Markup::XamlReader::Load(kPartXaml).as<controls::Border>();
    } catch (...) {
        // A WinUI without that resource: the brush of the application's resources, in the mode it has now.
        part = controls::Border();
        part.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        part.VerticalAlignment(xaml::VerticalAlignment::Top);
        part.CornerRadius(xaml::CornerRadius{1, 1, 1, 1});
        if (auto brush = ThemeResource(L"TextFillColorPrimaryBrush").try_as<media::Brush>()) part.Background(brush);
    }
    part.Opacity(opacity);
    return part;
}

void FadeWith(const xaml::UIElement& element, int ms) {
    xaml::ScalarTransition fade;
    fade.Duration(std::chrono::milliseconds(ms));
    element.OpacityTransition(fade);
}

// Ticks and shade where the stretch lies now.
void LayOut(const controls::Grid& layer, double from, double to) {
    auto children = layer.Children();
    if (children.Size() != 3) return;
    auto shade = children.GetAt(0).as<controls::Border>();
    auto first = children.GetAt(1).as<controls::Border>();
    auto second = children.GetAt(2).as<controls::Border>();
    double height = layer.ActualHeight();
    auto [lineTop, lineHeight] = LineIn(layer);
    double tickTop = std::max(0.0, lineTop - kTickOverhang);
    double tickHeight = std::min(height - tickTop, lineHeight + 2 * kTickOverhang);
    ProgressMark mark = LayOutProgressMark(layer.ActualWidth(), from, to, kTick);
    auto place = [](const controls::Border& part, bool shown, double left, double top, double width, double tall) {
        part.Visibility(shown ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
        if (!shown) return;
        part.Margin(xaml::Thickness{left, top, 0, 0});
        part.Width(std::max(0.0, width));
        part.Height(std::max(0.0, tall));
    };
    place(first, mark.start, mark.startLeft, tickTop, kTick, tickHeight);
    place(second, mark.end, mark.endLeft, tickTop, kTick, tickHeight);
    place(shade, mark.end, mark.shadeLeft, lineTop, mark.shadeWidth, lineHeight);
}

controls::Grid MakeLayer(const WeakContext& weakContext) {
    controls::Grid layer;
    Name(layer, "ampify-progress-mark");
    layer.IsHitTestVisible(false);
    layer.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    layer.VerticalAlignment(xaml::VerticalAlignment::Stretch);
    layer.Children().Append(MakePart(kShadeOpacity));
    layer.Children().Append(MakePart(1));
    layer.Children().Append(MakePart(1));
    layer.Opacity(0);
    FadeWith(layer, kFadeMs);
    layer.SizeChanged([weakContext](IInspectable const& sender, xaml::SizeChangedEventArgs const&) noexcept {
        try {
            auto live = weakContext.lock();
            if (auto grid = sender.try_as<controls::Grid>(); live && grid) LayOut(grid, live->markFrom, live->markTo);
        } catch (...) {
        }
    });
    return layer;
}

// --- The readout under the pointer -------------------------------------------------------------------------------

controls::Border MakeLabel() {
    try {
        return xaml::Markup::XamlReader::Load(kLabelXaml).as<controls::Border>();
    } catch (...) {
        return nullptr;  // a WinUI without the tooltip's resources: the marker shows alone
    }
}

// A Canvas, which clips nothing: the label stands above the line, outside the track's own box.
controls::Canvas MakeHoverLayer() {
    controls::Canvas layer;
    Name(layer, "ampify-progress-readout");
    layer.IsHitTestVisible(false);
    controls::Border marker = MakePart(1);
    marker.RenderTransform(media::TranslateTransform());
    layer.Children().Append(marker);
    if (controls::Border label = MakeLabel()) {
        label.RenderTransform(media::TranslateTransform());
        layer.Children().Append(label);
    }
    layer.Opacity(0);
    FadeWith(layer, kHoverFadeMs);
    return layer;
}

// The marker and the label at `x` (in the layer), the label naming the time there; above the line, or below it when
// the bar sits at the very top of the window.
void PlaceHover(WindowContext& context, double x) {
    controls::Canvas& layer = context.hoverLayer;
    auto children = layer.Children();
    if (children.Size() == 0) return;
    auto marker = children.GetAt(0).as<controls::Border>();
    controls::Border label = children.Size() > 1 ? children.GetAt(1).as<controls::Border>() : nullptr;
    controls::TextBlock text = label ? label.Child().try_as<controls::TextBlock>() : nullptr;
    double width = layer.ActualWidth();
    if (text) {
        double part = width > 0 ? std::clamp(x / width, 0.0, 1.0) : 0;
        winrt::hstring clock = winrt::to_hstring(ClockText(part * context.hoverTotal));
        if (text.Text() != clock) text.Text(clock);
        label.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
    }
    double labelWidth = label ? label.DesiredSize().Width : 0;
    HoverReadout readout = LayOutHoverReadout(width, x, kTick, labelWidth);
    context.hoverPart = readout.shown ? readout.part : -1;
    if (!readout.shown) {
        layer.Opacity(0);
        return;
    }
    auto [lineTop, lineHeight] = LineIn(layer);
    double tickTop = lineTop - kTickOverhang;
    marker.Width(kTick);
    marker.Height(lineHeight + 2 * kTickOverhang);
    controls::Canvas::SetTop(marker, tickTop);
    marker.RenderTransform().as<media::TranslateTransform>().X(readout.markerLeft);
    if (label) {
        double labelHeight = label.DesiredSize().Height;
        double above = tickTop - kLabelGap - labelHeight;
        double inWindow = layer.TransformToVisual(nullptr).TransformPoint({0, static_cast<float>(above)}).Y;
        double top = inWindow >= 0 ? above : lineTop + lineHeight + kTickOverhang + kLabelGap;
        controls::Canvas::SetTop(label, top);
        label.RenderTransform().as<media::TranslateTransform>().X(readout.labelLeft);
    }
    layer.Opacity(context.hoverTotal > 0 ? 1 : 0);
}

// Tells the listening elements where the pointer is (`part`, or -1 for "left the line"), once per change.
void Report(const UiEventSink& sink, WindowContext& context, double part) {
    if (!sink || part == context.hoverSentPart) return;
    context.hoverSentPart = part;
    char payload[48];
    if (part < 0) std::snprintf(payload, sizeof payload, "{\"part\":null}");
    else std::snprintf(payload, sizeof payload, "{\"part\":%.4f}", part);
    for (const std::string& elementId : context.hoverListeners) {
        sink(UiEvent{elementId, 0, false, payload, dispatch::kPlaybarHover});
    }
}

// The pointer events of the scrubber, heard even when the slider handles them (a drag): the readout follows the
// pointer at once, the listeners hear of it at most every kHoverReport.
void FollowPointer(const UiEventSink& sink, const std::shared_ptr<WindowContext>& context,
                   const xaml::FrameworkElement& scrubber) {
    WeakContext weakContext = context;
    xaml::Input::PointerEventHandler moved = [weakContext](IInspectable const&,
                                                          xaml::Input::PointerRoutedEventArgs const& args) noexcept {
        try {
            auto live = weakContext.lock();
            if (!live || !live->hoverLayer) return;
            PlaceHover(*live, args.GetCurrentPoint(live->hoverLayer).Position().X);
            if (!live->hoverListeners.empty() && live->hoverTimer && !live->hoverTimer.IsRunning()) {
                live->hoverTimer.Start();
            }
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    };
    xaml::Input::PointerEventHandler left = [weakContext, sink](IInspectable const&,
                                                               xaml::Input::PointerRoutedEventArgs const&) noexcept {
        try {
            auto live = weakContext.lock();
            if (!live || !live->hoverLayer) return;
            live->hoverPart = -1;
            live->hoverLayer.Opacity(0);
            if (live->hoverTimer) live->hoverTimer.Stop();
            Report(sink, *live, -1);
        } catch (...) {
        }
    };
    context->hoverMoved = winrt::box_value(moved);
    context->hoverLeft = winrt::box_value(left);
    scrubber.AddHandler(xaml::UIElement::PointerMovedEvent(), context->hoverMoved, true);
    scrubber.AddHandler(xaml::UIElement::PointerExitedEvent(), context->hoverLeft, true);
    scrubber.AddHandler(xaml::UIElement::PointerCanceledEvent(), context->hoverLeft, true);
    if (!context->hoverTimer) {
        context->hoverTimer = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        context->hoverTimer.Interval(kHoverReport);
        context->hoverTimer.IsRepeating(false);
        context->hoverTimer.Tick([weakContext, sink](dispatching::DispatcherQueueTimer const&, IInspectable const&) noexcept {
            try {
                if (auto live = weakContext.lock(); live && live->hoverPart >= 0) Report(sink, *live, live->hoverPart);
            } catch (...) {
            }
        });
    }
    context->hoverScrubber = winrt::make_weak(scrubber);
}

void StopFollowingPointer(WindowContext& context) {
    if (auto scrubber = context.hoverScrubber.get()) {
        if (context.hoverMoved) scrubber.RemoveHandler(xaml::UIElement::PointerMovedEvent(), context.hoverMoved);
        if (context.hoverLeft) {
            scrubber.RemoveHandler(xaml::UIElement::PointerExitedEvent(), context.hoverLeft);
            scrubber.RemoveHandler(xaml::UIElement::PointerCanceledEvent(), context.hoverLeft);
        }
    }
    context.hoverScrubber = nullptr;
    context.hoverMoved = nullptr;
    context.hoverLeft = nullptr;
    if (context.hoverTimer) context.hoverTimer.Stop();
}

}  // namespace

// Follows the first play bar button that marks the line. Cheap when nothing changed: no search while the layer is
// still in the track and the stretch is the same.
void FollowPlaybarMark(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                       const std::vector<Surface>& surfaces) {
    if (context->role != WindowRole::Main) return;  // the mini player has no play bar buttons
    double from = -1;
    double to = -1;
    for (const Surface& surface : surfaces) {
        if (surface.kind != SurfaceKind::Control || surface.control.placement != "playbar") continue;
        if (surface.control.markFrom < 0) continue;
        from = surface.control.markFrom;
        to = surface.control.markTo;
        break;
    }
    controls::Grid& layer = context->markLayer;
    if (from < 0) {
        if (layer) layer.Opacity(0);  // fades out; it stays in the track for the next mark
        context->markFrom = context->markTo = -1;
        return;
    }
    controls::Grid track = context->markTrack.get();
    bool placed = layer && track && layer.Parent() == track;
    if (!placed) {
        track = FindTrack(FindScrubber(*context));
        if (!track) {
            shared.Once("mark-no-track", "Interface: the progress line to mark was not found");
            return;
        }
        RemoveFromParent(layer);
        if (!layer) layer = MakeLayer(context);
        SpanTrack(layer, track);
        track.Children().Append(layer);
        context->markTrack = winrt::make_weak(track);
        shared.Once("mark-placed", "Interface: a stretch of the progress line is marked");
    }
    if (placed && from == context->markFrom && to == context->markTo && layer.Opacity() == 1) return;
    context->markFrom = from;
    context->markTo = to;
    LayOut(layer, from, to);
    layer.Opacity(1);
}

void ReleasePlaybarMark(WindowContext& context) noexcept {
    try {
        RemoveFromParent(context.markLayer);
    } catch (...) {
    }
    context.markLayer = nullptr;
    context.markTrack = nullptr;
    context.markFrom = context.markTo = -1;
}

// Puts the readout's layer in the track and follows the scrubber's pointer while an element wants a readout or hears
// the pointer; takes both away when none does. Cheap when nothing changed: no search while the layer is still in the
// track of the scrubber it follows.
void FollowPlaybarHover(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    if (context->role != WindowRole::Main) return;
    UiPlaybarHover hover;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        hover = shared.playbarHover;
    }
    if (hover.totalMs <= 0 && hover.listeners.empty()) {
        if (context->hoverLayer || context->hoverScrubber.get()) ReleasePlaybarHover(*context);
        return;
    }
    context->hoverTotal = hover.totalMs;
    context->hoverListeners = hover.listeners;
    if (hover.totalMs <= 0 && context->hoverLayer) context->hoverLayer.Opacity(0);
    xaml::FrameworkElement scrubber = context->hoverScrubber.get();
    if (context->hoverLayer && scrubber && scrubber.IsLoaded() && context->hoverLayer.Parent()) return;
    scrubber = FindScrubber(*context);
    controls::Grid track = FindTrack(scrubber);
    if (!track) {
        shared.Once("readout-no-track", "Interface: the progress line to read out was not found");
        return;
    }
    StopFollowingPointer(*context);
    RemoveFromParent(context->hoverLayer);
    if (!context->hoverLayer) context->hoverLayer = MakeHoverLayer();
    SpanTrack(context->hoverLayer, track);
    track.Children().Append(context->hoverLayer);
    FollowPointer(shared.sink, context, scrubber);
    shared.Once("readout-placed", "Interface: the progress line reads out the time under the pointer");
}

void ReleasePlaybarHover(WindowContext& context) noexcept {
    try {
        StopFollowingPointer(context);
        RemoveFromParent(context.hoverLayer);
    } catch (...) {
    }
    context.hoverLayer = nullptr;
    context.hoverTimer = nullptr;
    context.hoverPart = context.hoverSentPart = -1;
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
