#pragma once
// The cross-fade of the song's artwork: a layer over the element that draws the artwork shows the previous cover and
// fades it out while the new one is drawn underneath. The theme's `content: artwork` and the kit's image node with
// src "artwork" use it. No fade in eco glass or with Windows' animation effects off (theme::ArtworkFadeMs).

#if defined(AMPIFY_WITH_XAML)

#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace ampify::win {

// Held strongly by its owner, and released on the interface thread with it (docs/development/xaml-lessons.md).
struct ArtworkFade {
    winrt::Microsoft::UI::Xaml::Controls::Border layer{nullptr};    // over the artwork, the size of its parent
    winrt::Microsoft::UI::Xaml::Media::ImageBrush brush{nullptr};   // the previous cover
    winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard story{nullptr};
};

// A fade whose layer is not shown yet, its picture stretched like `stretch`. The caller puts `layer` over the artwork.
ArtworkFade MakeArtworkFade(winrt::Microsoft::UI::Xaml::Media::Stretch stretch);

// The artwork goes from `previous` to a new cover: the layer, rounded like the artwork (`corner`), shows `previous`
// and fades out. Nothing when there was no previous cover or when the fade is off. Interface thread only.
void FadeFromPrevious(ArtworkFade& fade, const winrt::Microsoft::UI::Xaml::Media::ImageSource& previous,
                      winrt::Microsoft::UI::Xaml::CornerRadius corner);

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
