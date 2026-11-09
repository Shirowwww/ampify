#pragma once
// `content: immersion`: Apple Music's own full-screen background, AMP.UI.Controls.ImmersionLockup, a SwapChainPanel
// that draws the song's artwork as a slow, blurred, moving wash on the graphics card. Apple's full-screen player and
// mini player use it; a theme puts one behind the pages with a ::before decor. Interface thread only; every lockup is
// held strongly and released on the interface thread (docs/development/xaml-lessons.md).

#if defined(AMPIFY_WITH_XAML)

#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>

namespace ampify::win {

class ImmersionBackgrounds {
public:
    // A new lockup named `name`, showing the cover last followed; null when Apple Music has no such control (another
    // version), so the decor is refused.
    winrt::Microsoft::UI::Xaml::FrameworkElement Make(const wchar_t* name);

    // The cover of the song playing (a file or https address, as Apple gives its own lockups): every lockup takes it.
    void Follow(const std::string& address);

    // Lets go of the lockups `held` no longer answers true for (the theme was removed, the decor replaced).
    template <typename Held>
    void Sweep(Held held) {
        std::erase_if(m_lockups, [&](const winrt::Microsoft::UI::Xaml::FrameworkElement& lockup) { return !held(lockup); });
    }

private:
    std::vector<winrt::Microsoft::UI::Xaml::FrameworkElement> m_lockups;
    std::string m_address;
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
