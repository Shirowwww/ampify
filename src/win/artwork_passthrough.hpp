#pragma once
// With the playback bar at the top, Apple's window lets the pointer through its caption only over the regions Apple
// sets for its controls, from where they are drawn. Turned by a theme (Spinning artwork), the bar's artwork moved out of
// its own region, and a click on it fell to the caption (dragging the window) instead of opening the mini player.
// Ampify adds the artwork's place in the bar, which no turn changes, to the window's passthrough regions, and puts it
// back each time Apple sets its own regions again.

#if defined(AMPIFY_WITH_XAML)

#include <string>

#include <winrt/Microsoft.UI.Xaml.h>

namespace ampify {
class Journal;
}

namespace ampify::win {

// `artwork` is the play bar's HoverGrid#ThumbnailHoverGrid, or null when there is none (the region then goes). The
// sidebar's pane (Grid#PaneRoot) gets a region too, below the caption strip: Mirror's pane on the right lay under
// Apple's caption, and neither the pointer over it nor a click on its hamburger reached it. Once per theme pass;
// writes only when a region moved or Apple dropped it. Main window's interface thread only.
void KeepArtworkPassthrough(const winrt::Microsoft::UI::Xaml::FrameworkElement& artwork,
                            const winrt::Microsoft::UI::Xaml::FrameworkElement& pane) noexcept;

// Writes to the log, when it changes, where Apple's veil over that artwork (Grid#ArtworkHoverContent) and its parts
// stand against the picture (ArtworkImage#ActualArtwork) and its slot: a veil cut square at its right edge under some
// themes, then missing once Ampify sized it, needs these to be read on screen. The veil's Rectangle takes the
// picture's radius as it stands, a theme's @media or a snippet included. Once per theme pass, main window only.
void KeepVeilOnArtwork(const winrt::Microsoft::UI::Xaml::FrameworkElement& artwork) noexcept;

// The applied theme's --artwork-veil (0 to 1), the strength of that veil's shade, or nothing to keep Apple's opaque
// one. The shade is rebuilt beyond the theme engine's reach under the pointer: KeepVeilOnArtwork sets it on the live
// one, its icon (a sibling) untouched. Theme service, on the interface thread, when the applied theme changes.
void KeepArtworkVeil(const std::string* value) noexcept;

// Where KeepArtworkPassthrough (the window's regions) and KeepVeilOnArtwork write their lines, a few dozen per session
// at most. Called with the other keeps of Apple's window (apple_tweak.cpp).
void KeepBarArtworkJournal(Journal& journal) noexcept;

// A line of what `filter` does to an element (written, tried again, cut away, given back), in the same few lines.
void NoteArtworkFilter(const std::string& line) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
