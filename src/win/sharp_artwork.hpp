#pragma once
// Apple's full-screen player stretches its cover over up to 640 points: a song of the library shows the library's
// cover cache (often 600 px), soft at 125 % and more. Shown larger than it is, the cover gets a sharper one in its place
// once that has loaded: the same image decoded at the size shown, Apple's catalog image asked at that size, or for the
// library's cache, the catalog's cover of the song. Apple's next song puts its own back, and so on.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>

namespace ampify {
class Journal;
}

namespace ampify::win {

// Follows the cover of the player whose content is `root`. Cheap when called again: the tree is walked only until
// Apple has built its cover's image, which it does once a song plays. Interface thread only.
void SharpenPlayerArtwork(const winrt::Microsoft::UI::Xaml::UIElement& root, Journal& journal) noexcept;

// apple.artwork: off, the next look puts Apple's cover back and the player is left as Apple shows it. Any thread.
void SetSharpArtwork(bool enabled) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
