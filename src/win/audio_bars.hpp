#pragma once

// The bars of a theme's `content: bars(n)`: a Grid of thin rounded Borders that rise and fall with the music, from
// the analysis AMPLibraryAgent.exe shares (audio_share.hpp). One timer of the interface thread moves every set of
// bars, 30 times a second, and only while one is on screen; it asks for the analysis as it goes, so the engine
// analyses nothing once the bars are gone. Built with the Windows App SDK headers only (AMPIFY_WITH_XAML); every
// member runs on the interface thread.

#if defined(AMPIFY_WITH_XAML)

#include <chrono>
#include <memory>
#include <vector>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include "../core/audio/audio_share.hpp"

namespace ampify::win {

class AudioBars {
public:
    AudioBars() = default;
    AudioBars(const AudioBars&) = delete;
    AudioBars& operator=(const AudioBars&) = delete;

    // A new set of `count` bars in the system accent colour until SetBrush, named `name`. It takes no pointer and
    // screen readers skip it, like a decorative image. Held strongly until Drop: a weak reference to an element
    // Ampify made never resolves (docs/development/xaml-lessons.md).
    winrt::Microsoft::UI::Xaml::Controls::Grid Make(int count, const wchar_t* name);
    // Every bar of the set takes the brush (the decor's `color`).
    void SetBrush(const winrt::Microsoft::UI::Xaml::Controls::Grid& bars,
                  const winrt::Microsoft::UI::Xaml::Media::Brush& brush);
    // The set left its panel: it stops moving and is released here. Anything else is ignored.
    void Drop(const winrt::Microsoft::UI::Xaml::FrameworkElement& element);

private:
    struct Set {
        winrt::Microsoft::UI::Xaml::Controls::Grid grid{nullptr};
        std::vector<winrt::Microsoft::UI::Xaml::Controls::Border> bars;
        audio::BarMeter meter;
    };

    void Tick();
    bool MotionAllowed();

    std::vector<Set> m_sets;
    // The timer's handler holds a weak pointer to this: once the owner is destroyed (maybe off the interface thread,
    // when the timer cannot be stopped), a tick does nothing.
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{nullptr};
    std::chrono::steady_clock::time_point m_last{};
    winrt::Windows::UI::ViewManagement::UISettings m_settings{nullptr};
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
