#pragma once
// apple.albumClick = expand (issue #7): in Library > Albums, a click on an album opens its track list right in the
// grid, as iTunes did. The rows below slide down to open a gap, and Ampify's panel (win/album_panel.hpp) shows in it,
// moving with the grid as it scrolls. The track list comes from the library file (src/core/library/album_tracks.hpp);
// a track plays through Apple's own library playback (knowledge/apple-music-queue.md). Apple's click (the grid's
// ItemClick, which opens the album's page) is held back while the setting is on, and the panel's Open album button
// raises it again. Off: the grid is given back as Apple made it.

#if defined(AMPIFY_WITH_XAML)

#include "../core/journal.hpp"

namespace ampify::win {

// On (expand) or off (Apple's page), applied at the next tick of the main window's tweaks; `colors` paints the panel
// in its album's colours (apple.albumClick = colors), from the next album opened. Any thread.
void SetAlbumExpand(bool expand, bool colors) noexcept;

// Finds the library's album grid in the main window and takes or gives back its click. Called at each tick of the
// main window's tweaks; cheap while nothing changes.
void KeepAlbumExpand(Journal& journal) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
