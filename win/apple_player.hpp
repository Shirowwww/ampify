#pragma once

// Apple Music's own full-screen player, opened from Ampify (Ampify.ui.applePlayer.open). The kernel calls
// AMP.UI.Views.FullScreenPlayer.Instance().Show(), as the full-screen button of Apple's mini player does, then lays
// the player's window (Apple's "FullScreenPlayerWindow") over the main window: same place and size, no title bar,
// edges that resize, a background that moves the window when dragged. Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/journal.hpp"

namespace ampify::win {

// Call on the main window's interface thread. Apple creates the window after Show returns: a timer of that thread
// waits for it (3 s at most). Every outcome goes to the log.
void OpenApplePlayer(const winrt::Microsoft::UI::Xaml::Window& mainWindow, Journal& journal) noexcept;

}  // namespace ampify::win

#endif
