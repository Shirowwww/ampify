#pragma once
// apple.lyricsBefore and apple.lyricsAfter: in Apple's full-screen player, only so many lyrics lines above and below
// the one being sung, the others faded out, and the block moved to stand centred on the sung line
// (src/core/lyrics_window.hpp). Apple's own lines are kept: duets, background vocals and word-by-word lyrics read as
// before, and Apple still scrolls them. Only their containers' opacity and the list's translation change.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>

namespace ampify::win {

// Follows the lyrics of the full-screen player whose content is `root`: called on each tick of its placement timer.
// Cheap once the lyrics list is found (a search at most every second until then). Interface thread only.
void FollowLyricsWindow(const winrt::Microsoft::UI::Xaml::UIElement& root) noexcept;

// The player closed: every line Ampify faded is shown again. Interface thread only.
void ReleaseLyricsWindow() noexcept;

// The lines to keep above and below the sung one (kAllLines for all), applied at the next FollowLyricsWindow. Any
// thread.
void SetLyricsWindow(int before, int after) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
