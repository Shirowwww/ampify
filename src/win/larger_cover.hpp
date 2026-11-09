#pragma once
// Where to load the cover of the song playing at a given size, when Apple's image of it is smaller: the one decision
// behind every sharp cover Ampify draws (the full-screen player's, the themes' window backgrounds). In order: the same
// image decoded at that size when its file is large enough; Apple's catalog image asked at that size; the catalog's
// cover of the song (looked up once per song, by its catalog id); Apple's own loader's catalog address for a song with
// no id. Interface thread only.

#if defined(AMPIFY_WITH_XAML)

#include <functional>
#include <string>

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace ampify::win {

// A file or an image this much smaller than the size asked looks soft once drawn at that size.
inline constexpr double kSoftCoverRatio = 0.9;

// `found` gets the address to decode at `pixels`, or an empty one and why there is none (for the log). It runs at once,
// or on the interface thread once the catalog has answered; the caller drops it if Apple's cover changed meanwhile.
using CoverFound = std::function<void(const std::string& address, const std::string& why)>;
void FindLargerCover(const winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage& apple, double pixels,
                     CoverFound found);

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
