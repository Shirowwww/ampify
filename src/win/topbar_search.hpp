#pragma once
// apple.topbar = search: Apple's own search field and a back arrow in the empty strip at the top of the window, next to
// the caption buttons, as in Spotify. The field is Apple's (its presenter moves out of the sidebar, so typing and
// submitting stay Apple's own); the arrow presses Apple's back button. No Apple method is called.

#if defined(AMPIFY_WITH_XAML)

namespace ampify {
class Journal;
}

namespace ampify::win {

// Moves the field into the strip and keeps it laid out (a quarter-second timer follows the window's width, its DPI and
// the passthrough regions Apple sets), or gives everything back when turned off, when the window is too narrow, or
// when the playback bar is at the top. Off or too narrow, the strip's veil still darkens a theme's picture under the
// caption buttons. Interface thread only.
void KeepTopbarSearch(Journal& journal) noexcept;

// apple.topbar: search or off, applied at the next KeepTopbarSearch. Any thread.
void SetTopbarSearch(bool enabled) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
