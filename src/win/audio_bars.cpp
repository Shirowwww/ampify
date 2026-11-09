#if defined(AMPIFY_WITH_XAML)

#include "audio_bars.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>

#include "audio/audio_sharing.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace shapes = xaml::Shapes;
using winrt::Windows::Foundation::Numerics::float4x4;

constexpr auto kFrame = std::chrono::milliseconds(33);
constexpr float kLowest = 0.08f;  // a bar never vanishes: the set keeps its shape while the music pauses
constexpr float kStill = 0.3f;    // every bar, with Windows' animation effects turned off
constexpr double kGap = 1.5;      // on each side of a bar
// A bar stays this thin, centred in its column: across a wide decor (the whole display, the play bar) ten bars of a
// hundred pixels read as grey blocks over the text.
constexpr double kMaxBarWidth = 6.0;
constexpr double kStroke = 2.0;   // the line's thickness
constexpr double kWidthEase = 0.08;  // time constant, in seconds, of a new bar width easing in (settled in ~0.3 s)

double MaxBarWidth(bool thick) {
    return thick ? std::numeric_limits<double>::infinity() : kMaxBarWidth;
}

controls::Border NewBar(const media::Brush& brush) {
    controls::Border bar;
    bar.Background(brush);
    bar.Scale({1.0f, kLowest, 1.0f});
    return bar;
}

}  // namespace

controls::Grid AudioBars::Make(const theme::BarsContent& content, const wchar_t* name) {
    controls::Grid grid;
    grid.Name(name);
    grid.IsHitTestVisible(false);
    xaml::Automation::AutomationProperties::SetAccessibilityView(grid, xaml::Automation::Peers::AccessibilityView::Raw);
    if (!m_settings) m_settings = winrt::Windows::UI::ViewManagement::UISettings();
    media::SolidColorBrush accent(m_settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent));

    const auto count = static_cast<size_t>(content.count);
    const bool mirror = content.shape == theme::BarShape::Mirror;
    Set set{grid, content.shape, static_cast<float>(content.reach) / 100.0f, {}, nullptr,
            audio::BarMeter(mirror ? audio::MirrorBands(count) : count)};
    set.levels.resize(count);
    if (content.shape == theme::BarShape::Line) {
        shapes::Polyline line;
        line.Stroke(accent);
        line.StrokeThickness(kStroke);
        line.StrokeLineJoin(media::PenLineJoin::Round);
        line.StrokeStartLineCap(media::PenLineCap::Round);
        line.StrokeEndLineCap(media::PenLineCap::Round);
        line.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        line.VerticalAlignment(xaml::VerticalAlignment::Top);
        grid.Children().Append(line);
        set.line = line;
    } else {
        const bool ring = content.shape == theme::BarShape::Circle;
        for (size_t i = 0; i < count; i++) {
            controls::Border bar = NewBar(accent);
            if (ring) {
                // Laid at the top left, sized once the ring knows its room (ShowRing), placed by its matrix.
                bar.HorizontalAlignment(xaml::HorizontalAlignment::Left);
                bar.VerticalAlignment(xaml::VerticalAlignment::Top);
                bar.Width(0);
                bar.Height(0);
                bar.Scale({1.0f, 1.0f, 1.0f});
            } else {
                grid.ColumnDefinitions().Append(controls::ColumnDefinition());
                bar.Margin({kGap, 0, kGap, 0});
                bar.MaxWidth(MaxBarWidth(content.thick));
                bar.CornerRadius({kGap, kGap, kGap, kGap});
                controls::Grid::SetColumn(bar, static_cast<int32_t>(i));
            }
            grid.Children().Append(bar);
            set.bars.push_back(bar);
        }
    }
    m_sets.push_back(std::move(set));

    if (!m_timer) {
        m_timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        m_timer.Interval(kFrame);
        // An exception out of a timer's handler is a failed HRESULT for the dispatcher, which fails fast the whole
        // process ("stowed exception" in CoreMessaging): nothing may leave it.
        m_timer.Tick([this, alive = std::weak_ptr<int>(m_alive)](auto&&, auto&&) noexcept {
            try {
                if (alive.lock()) Tick();
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
    }
    if (!m_timer.IsRunning()) {
        m_last = std::chrono::steady_clock::now();
        m_timer.Start();
    }
    return grid;
}

void AudioBars::SetBrush(const controls::Grid& bars, const media::Brush& brush) {
    for (const Set& set : m_sets) {
        if (set.grid != bars) continue;
        for (const controls::Border& bar : set.bars) bar.Background(brush);
        if (set.line) set.line.Stroke(brush);
    }
}

void AudioBars::Drop(const xaml::FrameworkElement& element) {
    if (!element) return;
    std::erase_if(m_sets, [&](const Set& set) { return set.grid == element; });
    if (m_sets.empty() && m_timer) m_timer.Stop();
}

void AudioBars::SetThick(const xaml::FrameworkElement& element, bool thick) {
    for (Set& set : m_sets) {
        if (set.grid != element || set.bars.empty() || set.shape == theme::BarShape::Circle) continue;
        const double before = set.bars.front().ActualWidth() * set.widthScale;
        const double count = static_cast<double>(set.bars.size());
        const double column = set.grid.ActualWidth() / count - 2 * kGap;
        const double after = std::min(column, MaxBarWidth(thick));
        for (controls::Border& bar : set.bars) bar.MaxWidth(MaxBarWidth(thick));
        // Laid out at the new width at once, drawn at the old one, then eased to 1 by Show.
        if (before > 0 && after > 0) set.widthScale = static_cast<float>(before / after);
    }
}

void AudioBars::Tick() {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_last).count();
    m_last = now;
    // Only the bars on screen move, and only then is the analysis asked for: loaded, in a window that shows (not one
    // hidden in the notification area or minimized).
    const auto onScreen = [](const Set& set) {
        if (!set.grid.IsLoaded()) return false;
        auto root = set.grid.XamlRoot();
        return !root || root.IsHostVisible();
    };
    if (std::none_of(m_sets.begin(), m_sets.end(), onScreen)) return;

    const bool motion = MotionAllowed();
    std::optional<audio::AnalysisFrame> frame;
    audio::SharedAudio* block = motion ? SharedAudioBlock() : nullptr;  // standing still needs no analysis
    if (block) {
        int64_t nowMs = SharedAudioNowMs();
        audio::DemandShared(*block, nowMs);
        audio::SharedAudioReading reading;
        if (audio::ReadShared(*block, nowMs, reading)) frame = std::move(reading.frame);
    }
    for (Set& set : m_sets) {
        if (!set.grid.IsLoaded()) continue;
        set.meter.Update(frame ? &frame->bandsDb : nullptr, elapsed);
        Show(set, motion, elapsed);
    }
}

// The levels of this frame, in the order the bars stand, then written in the set's shape.
void AudioBars::Show(Set& set, bool motion, double elapsed) {
    const std::vector<float>& meter = set.meter.Levels();
    const bool mirror = set.shape == theme::BarShape::Mirror;
    const size_t count = set.levels.size();
    for (size_t i = 0; i < count; i++) {
        size_t source = mirror ? audio::MirrorSource(i, count) : i;
        float level = source < meter.size() ? meter[source] : 0.0f;
        set.levels[i] = std::max(kLowest, (motion ? level : kStill) * set.reach);
    }
    const auto width = static_cast<float>(set.grid.ActualWidth());
    const auto height = static_cast<float>(set.grid.ActualHeight());
    if (width <= 0 || height <= 0) return;
    if (set.shape == theme::BarShape::Circle) return ShowRing(set, width, height);
    if (set.shape == theme::BarShape::Line) return ShowLine(set, width, height);
    // A new width (thin or thick) eases in, at once without animation effects.
    const float ease = motion ? static_cast<float>(std::min(1.0, elapsed / kWidthEase)) : 1.0f;
    set.widthScale += (1.0f - set.widthScale) * ease;
    if (std::abs(set.widthScale - 1.0f) < 0.01f) set.widthScale = 1.0f;
    // Scaled from the bottom, or from the middle for the mirror: the centre follows the bar's size, which the theme
    // may change at any time.
    for (size_t i = 0; i < set.bars.size() && i < count; i++) {
        controls::Border& bar = set.bars[i];
        float foot = static_cast<float>(bar.ActualHeight()) * (mirror ? 0.5f : 1.0f);
        bar.CenterPoint({static_cast<float>(bar.ActualWidth() / 2), foot, 0.0f});
        bar.Scale({set.widthScale, set.levels[i], 1.0f});
    }
}

void AudioBars::ShowRing(Set& set, float width, float height) {
    const size_t count = set.bars.size();
    audio::Ring ring = audio::LayRing(width, height, count);
    if (width != set.width || height != set.height) {
        set.width = width;
        set.height = height;
        double radius = ring.thickness / 2.0;
        for (controls::Border& bar : set.bars) {
            bar.Width(ring.thickness);
            bar.Height(ring.length);
            bar.CornerRadius({radius, radius, radius, radius});
        }
    }
    for (size_t i = 0; i < count; i++) {
        audio::Affine m = audio::RingBar(ring, i, count, set.levels[i]);
        set.bars[i].TransformMatrix(float4x4{m.m11, m.m12, 0, 0, m.m21, m.m22, 0, 0, 0, 0, 1, 0, m.dx, m.dy, 0, 1});
    }
}

void AudioBars::ShowLine(Set& set, float width, float height) {
    if (width != set.width || height != set.height) {
        // A fixed size: new points then measure the line alone, never the panel around it.
        set.width = width;
        set.height = height;
        set.line.Width(width);
        set.line.Height(height);
    }
    audio::TraceLine(set.levels, width, height, static_cast<float>(kStroke), set.curve);
    set.points.resize(set.curve.size());
    for (size_t i = 0; i < set.curve.size(); i++) set.points[i] = {set.curve[i].x, set.curve[i].y};
    set.line.Points().ReplaceAll(set.points);
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
