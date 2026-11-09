// The stretch a play bar button marks on the progress line of the playback bar (Ampify.ui.playbar.mark): a tick at
// each end and a light shade between them, laid in the track of Apple's scrubber, over the line, and taking no click.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include "../core/progress_mark.hpp"

namespace ampify::win::ui_service_detail {

namespace {

constexpr double kTick = 2;            // width of a tick
constexpr double kTickOverhang = 3;    // how far a tick stands above and below the line
constexpr double kShadeOpacity = 0.3;  // the shade lightens the line, played or not, without hiding it
constexpr int kFadeMs = 180;
constexpr int kSearchLimit = 400;      // the display holds about a hundred elements
const wchar_t* const kMarkBrush = L"TextFillColorPrimaryBrush";  // reads on the played part (accent) and the rest

xaml::FrameworkElement FindBelow(const xaml::DependencyObject& root, std::wstring_view name) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < kSearchLimit; i++) {
        if (auto element = queue[i].try_as<xaml::FrameworkElement>(); element && element.Name() == name) return element;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

// The track of the playback bar's progress line: the Grid of the scrubber's template that holds its rectangles.
controls::Grid FindTrack(const WindowContext& context) {
    xaml::FrameworkElement display = context.songDisplay.get();
    if (!display) {
        if (std::optional<int> lcd = uirender::FindByName(context.probe, "LCD")) {
            display = context.probeObjects[*lcd].get().try_as<xaml::FrameworkElement>();
        }
    }
    xaml::FrameworkElement scrubber = display ? FindBelow(display, L"LCDScrubber") : nullptr;
    xaml::FrameworkElement track = scrubber ? FindBelow(scrubber, L"HorizontalTemplate") : nullptr;
    return track ? track.try_as<controls::Grid>() : nullptr;
}

controls::Border MakePart(double opacity) {
    controls::Border part;
    part.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    part.VerticalAlignment(xaml::VerticalAlignment::Top);
    part.CornerRadius(xaml::CornerRadius{1, 1, 1, 1});
    part.Opacity(opacity);
    if (auto brush = ThemeResource(kMarkBrush).try_as<media::Brush>()) part.Background(brush);
    return part;
}

// Ticks and shade where the stretch lies now: Apple's line (its track rectangle) gives their height, a theme or a
// snippet may have made it thicker or moved it.
void LayOut(const controls::Grid& layer, double from, double to) {
    auto children = layer.Children();
    if (children.Size() != 3) return;
    auto shade = children.GetAt(0).as<controls::Border>();
    auto first = children.GetAt(1).as<controls::Border>();
    auto second = children.GetAt(2).as<controls::Border>();
    double height = layer.ActualHeight();
    double lineTop = 0;
    double lineHeight = height;
    if (auto track = layer.Parent().try_as<xaml::FrameworkElement>()) {
        if (xaml::FrameworkElement rect = FindBelow(track, L"HorizontalTrackRect"); rect && rect.ActualHeight() > 0) {
            lineTop = rect.TransformToVisual(layer).TransformPoint({0, 0}).Y;
            lineHeight = rect.ActualHeight();
        }
    }
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
    controls::Grid::SetColumnSpan(layer, 8);  // every column of the track: the stretch may lie in the part not played
    layer.Children().Append(MakePart(kShadeOpacity));
    layer.Children().Append(MakePart(1));
    layer.Children().Append(MakePart(1));
    layer.Opacity(0);
    xaml::ScalarTransition fade;
    fade.Duration(std::chrono::milliseconds(kFadeMs));
    layer.OpacityTransition(fade);
    layer.SizeChanged([weakContext](IInspectable const& sender, xaml::SizeChangedEventArgs const&) noexcept {
        try {
            auto live = weakContext.lock();
            if (auto grid = sender.try_as<controls::Grid>(); live && grid) LayOut(grid, live->markFrom, live->markTo);
        } catch (...) {
        }
    });
    return layer;
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
        track = FindTrack(*context);
        if (!track) {
            shared.Once("mark-no-track", "Interface: the progress line to mark was not found");
            return;
        }
        RemoveFromParent(layer);
        if (!layer) layer = MakeLayer(context);
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

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
