#pragma once
// apple.lyricsFill (src/core/lyrics_fill.hpp): Apple's word-by-word lyrics light each row of the line being sung with a
// gradient whose edge it moves in steps. Ampify swaps that gradient for one of its own, the same colours, whose edge
// the compositor moves at the screen's pace along the words' times, and puts Apple's back when the setting goes off.
// A row Ampify cannot match to a line of the song, or that is not drawn as expected, stays Apple's.

#if defined(AMPIFY_WITH_XAML)

#include "../core/journal.hpp"

namespace ampify::win {

// On (smooth) or off (Apple's own steps), with Apple's crisp edge or a soft one, applied at the next tick of each
// window's follower. Any thread.
void SetLyricsFill(bool smooth, bool softEdge) noexcept;

// Starts the follower of the main window's thread, which also follows the full-screen player and starts the mini
// player's on that window's own thread. Called at each tick of the main window's tweaks; cheap once started.
void KeepLyricsFill(Journal& journal) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
