#if defined(AMPIFY_WITH_XAML)

#include "audio_bars.hpp"

#include <algorithm>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>

#include "audio/audio_sharing.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;

constexpr auto kFrame = std::chrono::milliseconds(33);
constexpr float kLowest = 0.08f;  // a bar never vanishes: the set keeps its shape while the music pauses
constexpr float kStill = 0.3f;    // every bar, with Windows' animation effects turned off
constexpr double kGap = 1.5;      // on each side of a bar

}  // namespace

controls::Grid AudioBars::Make(int count, const wchar_t* name) {
    controls::Grid grid;
    grid.Name(name);
    grid.IsHitTestVisible(false);
    xaml::Automation::AutomationProperties::SetAccessibilityView(grid, xaml::Automation::Peers::AccessibilityView::Raw);
    if (!m_settings) m_settings = winrt::Windows::UI::ViewManagement::UISettings();
    media::SolidColorBrush accent(m_settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent));

    Set set{grid, {}, audio::BarMeter(static_cast<size_t>(count))};
    for (int i = 0; i < count; i++) {
        grid.ColumnDefinitions().Append(controls::ColumnDefinition());
        controls::Border bar;
        bar.Margin({kGap, 0, kGap, 0});
        bar.CornerRadius({kGap, kGap, kGap, kGap});
        bar.Background(accent);
        bar.Scale({1.0f, kLowest, 1.0f});
        controls::Grid::SetColumn(bar, i);
        grid.Children().Append(bar);
        set.bars.push_back(bar);
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
    }
}

void AudioBars::Drop(const xaml::FrameworkElement& element) {
    if (!element) return;
    std::erase_if(m_sets, [&](const Set& set) { return set.grid == element; });
    if (m_sets.empty() && m_timer) m_timer.Stop();
}

bool AudioBars::MotionAllowed() {
    if (!m_settings) m_settings = winrt::Windows::UI::ViewManagement::UISettings();
    return m_settings.AnimationsEnabled();
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
        const std::vector<float>& levels = set.meter.Levels();
        for (size_t i = 0; i < set.bars.size() && i < levels.size(); i++) {
            controls::Border& bar = set.bars[i];
            float level = motion ? std::max(kLowest, levels[i]) : kStill;
            // Scaled from the bottom: the centre follows the bar's size, which the theme may change at any time.
            bar.CenterPoint({static_cast<float>(bar.ActualWidth() / 2), static_cast<float>(bar.ActualHeight()), 0.0f});
            bar.Scale({1.0f, level, 1.0f});
        }
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
