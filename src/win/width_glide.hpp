#pragma once
// `transition: width <ms>`: the width Apple Music writes to an element glides there instead of jumping. Apple's
// progress line, for one, grows its played part once a second; with this, it moves on continuously (the smooth
// progress bar of Spicetify). XAML cannot animate a width on the compositor, and an animation of Width would hide
// Apple's next writes: Ampify watches each write and eases the shown width towards it frame by frame, over the time
// that write took to come (at most the transition's duration), so the line arrives as the next one is written.

#if defined(AMPIFY_WITH_XAML)

#include <chrono>
#include <cstdint>
#include <map>

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include "presence_watch.hpp"
#include "../core/theme/tree.hpp"

namespace ampify::win {

class WidthGlides {
public:
    WidthGlides();
    WidthGlides(const WidthGlides&) = delete;
    WidthGlides& operator=(const WidthGlides&) = delete;
    ~WidthGlides();

    // Makes the widths written to `element` glide for at most `ms`; 0 lets them jump again, the last width Apple
    // wrote back in place. Interface thread only.
    void Write(theme::NodeId id, const winrt::Microsoft::UI::Xaml::FrameworkElement& element, double ms) noexcept;

    // Forgets the elements that are gone. Interface thread only.
    void Sweep() noexcept;

private:
    using Clock = std::chrono::steady_clock;
    struct Glide {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> element;
        int64_t token = 0;      // the Width callback
        double capMs = 0;       // the transition's duration
        double target = 0;      // the last width Apple wrote
        double from = 0;        // where the glide started
        double shown = 0;       // what Ampify last wrote
        double durationMs = 0;  // of the glide on its way
        double step = 0.5;      // the least change worth a write: half a pixel of the screen, in XAML units
        Clock::time_point start{};
        Clock::time_point lastWrite{};  // Apple's last write
        bool known = false;     // a first width was seen
        bool moving = false;
    };

    void OnWidth(theme::NodeId id) noexcept;
    void Step() noexcept;
    void Show(Glide& glide, double width) noexcept;
    void Release(Glide& glide) noexcept;
    void Listen() noexcept;
    void Settle() noexcept;

    std::map<theme::NodeId, Glide> m_glides;
    bool m_writing = false;  // Ampify's own write, not Apple's
    winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering_revoker m_frames;
    SeenListener m_presence;  // hidden, the lines stand at Apple's width: no frame is drawn for nobody
};

}  // namespace ampify::win

#endif
