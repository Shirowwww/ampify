#pragma once

// The bars of a theme's `content: bars(n, shape)`: a Grid of rounded Borders (side by side, mirrored, in a ring) or of
// one Polyline, moved with the analysis AMPLibraryAgent.exe shares (audio/audio_sharing.hpp). One interface timer moves
// every set 30 times a second while one is on screen, by composition transforms (no layout) or the line's points inside
// a fixed size; the engine analyses nothing once the bars are gone. AMPIFY_WITH_XAML only, interface thread only.

#if defined(AMPIFY_WITH_XAML)

#include <chrono>
#include <memory>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include "../core/audio/bar_meter.hpp"
#include "../core/audio/bar_shapes.hpp"
#include "../core/theme/tree.hpp"
#include "presence_watch.hpp"

namespace ampify::win {

class AudioBars {
public:
    AudioBars() = default;
    AudioBars(const AudioBars&) = delete;
    AudioBars& operator=(const AudioBars&) = delete;

    // A new set of bars in the system accent colour until SetBrush, named `name`. It takes no pointer and screen
    // readers skip it, like a decorative image. Held strongly until Drop: a weak reference to an element Ampify made
    // never resolves (docs/development/xaml-lessons.md).
    winrt::Microsoft::UI::Xaml::Controls::Grid Make(const theme::BarsContent& content, const wchar_t* name);
    // Every bar of the set (or its line) takes the brush (the decor's `color`).
    void SetBrush(const winrt::Microsoft::UI::Xaml::Controls::Grid& bars,
                  const winrt::Microsoft::UI::Xaml::Media::Brush& brush);
    // The set left its panel: it stops moving and is released here. Anything else is ignored.
    void Drop(const winrt::Microsoft::UI::Xaml::FrameworkElement& element);
    // The set's side bars turn thin or thick in place, their width easing over a few frames (a setting switched).
    void SetThick(const winrt::Microsoft::UI::Xaml::FrameworkElement& element, bool thick);

private:
    struct Set {
        winrt::Microsoft::UI::Xaml::Controls::Grid grid{nullptr};
        theme::BarShape shape = theme::BarShape::Bars;
        float reach = 1.0f;
        std::vector<winrt::Microsoft::UI::Xaml::Controls::Border> bars;  // every shape but the line
        winrt::Microsoft::UI::Xaml::Shapes::Polyline line{nullptr};
        audio::BarMeter meter;
        float width = -1, height = -1;  // the size the ring or the line was last laid out for
        std::vector<float> levels;      // what the bars show this frame, reused
        std::vector<audio::Point> curve;
        std::vector<winrt::Windows::Foundation::Point> points;
        float widthScale = 1.0f;  // drawn width over laid-out width: below or above 1 while a new width eases in
    };

    void Tick();
    static void Show(Set& set, bool motion, double elapsed);
    static void ShowRing(Set& set, float width, float height);
    static void ShowLine(Set& set, float width, float height);

    std::vector<Set> m_sets;
    // The timer's handler holds a weak pointer to this: once the owner is destroyed (maybe off the interface thread,
    // when the timer cannot be stopped), a tick does nothing.
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
    SeenTimer m_timer;  // a frame of the bars, while some show and the window is seen
    std::chrono::steady_clock::time_point m_last{};
    winrt::Windows::UI::ViewManagement::UISettings m_settings{nullptr};
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
