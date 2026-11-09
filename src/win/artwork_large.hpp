#pragma once
// Apple decodes its images at the size it shows them: the play bar's cover is a thumbnail of about 54 pixels, which a
// brush over the whole window blew up (the Artwork and Glass backgrounds, the animated-artwork snippet: soft, blocky).
// LargeArtwork loads the cover FindLargerCover finds (larger_cover.hpp, as for the full-screen player) in a hidden image
// beside Apple's, since WinUI decodes only what the window holds, and hands it over once it has opened. Interface
// thread only.

#if defined(AMPIFY_WITH_XAML)

#include <functional>
#include <memory>
#include <string>

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace ampify::win {

// The hidden image's name: the theme engine's walk leaves it out, so that no selector meets it.
inline constexpr char kLargeArtworkLoaderName[] = "AmpifyArtworkLoader";

class LargeArtwork {
public:
    LargeArtwork();

    // The cover to paint for Apple's `apple` image, whose source is `source`: the large one once it has opened, null
    // while it loads (keep what is painted, so a new song does not flash its thumbnail first), or `source` itself when
    // no large one can be had (no address, a failed load, too slow). `opened` runs when the large cover has opened or
    // failed, so that the caller asks again.
    winrt::Microsoft::UI::Xaml::Media::ImageSource For(const winrt::Microsoft::UI::Xaml::Controls::Image& apple,
                                                        const winrt::Microsoft::UI::Xaml::Media::ImageSource& source,
                                                        std::function<void()> opened);

    // Nothing paints the artwork any more: the hidden image leaves Apple's panel and the bitmaps are let go.
    void Drop();

    // A line for the log about the last cover loaded or refused, once; empty when there is nothing new.
    std::string TakeNote();

    struct State;

private:
    std::shared_ptr<State> m_state;
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
