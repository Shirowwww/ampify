#pragma once
// `content: immersion`: the song's artwork as a slow, soft, moving wash behind the pages, drawn by Ampify on the
// compositor. The cover is decoded tiny (kWashPixels) and stretched over the window, which smooths it into colour
// fields at no cost per frame; two copies turn slowly in opposite directions, a few steps a second, so the colours
// drift, only while a song plays and a window is seen. Apple's own ImmersionLockup, used before 1.3.3, drew an almost
// black layer outside its full-screen player.
// Interface thread only; every layer is held strongly and released on the interface thread.

#if defined(AMPIFY_WITH_XAML)

#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "presence_watch.hpp"

namespace ampify::win {

class MovingArtworks {
public:
    // A new wash named `name`, showing the cover last followed. Null when the compositor cannot be reached.
    winrt::Microsoft::UI::Xaml::FrameworkElement Make(const wchar_t* name);

    // The cover of the song playing (a file or https address): every wash takes it.
    void Follow(const std::string& address);

    // Lets go of the washes `held` no longer answers true for (the theme was removed, the decor replaced).
    template <typename Held>
    void Sweep(Held held) {
        std::erase_if(m_washes, [&](const Wash& wash) {
            if (held(wash.host)) return false;
            Release(wash);
            return true;
        });
        StopWhenEmpty();
    }

private:
    struct Wash {
        winrt::Microsoft::UI::Xaml::Controls::Grid host{nullptr};
        winrt::Microsoft::UI::Composition::ContainerVisual root{nullptr};
        winrt::Microsoft::UI::Composition::CompositionSurfaceBrush brush{nullptr};
        std::vector<winrt::Microsoft::UI::Composition::SpriteVisual> layers;
    };

    void Show(Wash& wash) const;
    void Turn();
    void Step();
    void StopWhenEmpty() noexcept;
    static void Release(const Wash& wash) noexcept;

    std::vector<Wash> m_washes;
    std::string m_address;
    SeenTimer m_timer;    // steps the drift while a wash is shown and a window is seen
    double m_turned = 0;  // seconds of drift so far: every wash shows the same angles
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
