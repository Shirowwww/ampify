#pragma once
// Apple's artwork (AMP.UI.Views.ArtworkImage) caps its own size with ArtworkMaxSize, 270 by 270 at the top of an
// album page, and Apple also sets the element's MaxHeight to 270: a theme's larger width drew it wider but cut at 270
// high. Each time a theme writes or clears an artwork's width or height, the cap and the element's own maximums grow
// to fit (theme::LiftedMaxSize, theme::LiftedBound), and go back to Apple's when they no longer need to.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>

namespace ampify::win {

// Nothing for an element that is not an ArtworkImage. Interface thread only.
void FollowArtworkCap(const winrt::Microsoft::UI::Xaml::FrameworkElement& element) noexcept;

// Once per theme pass: an artwork whose cap Ampify lifted and Apple set back (a new picture, a layout change) gets the
// lifted cap again and loads its picture at it, a few times at most. Apple's cap left in place made it ask a picture
// 270 wide for a 360 square: a portrait drawn small between bands. Interface thread only.
void KeepArtworkCaps() noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
