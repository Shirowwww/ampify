#pragma once

// Apple Music's own full-screen player, opened from Ampify (Ampify.ui.applePlayer.open) or by Apple's own button. The
// kernel calls AMP.UI.Views.FullScreenPlayer.Instance().Show(), as the full-screen button of Apple's mini player does,
// then lays the player's window (Apple's "FullScreenPlayerWindow") where the setting apple.fullscreen says
// (uirender::PlayerMode): over the main window (same place and size, no title bar, edges that resize, a background
// that moves the window when dragged), in place of it (the main window hides until the player closes), on the screen
// above the taskbar, or on the whole screen as Apple shows it. Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/journal.hpp"
#include "../core/ui_render/apple_tweaks.hpp"

namespace ampify::win {

// Call on the main window's interface thread. Apple creates the window after Show returns: a timer of that thread
// waits for it (3 s at most), then follows it until it closes. Every outcome goes to the log.
void OpenApplePlayer(const winrt::Microsoft::UI::Xaml::Window& mainWindow, Journal& journal,
                     uirender::PlayerMode mode) noexcept;

// Lays out the player Apple Music opened itself, when it is on screen and not yet laid out. Call on the main window's
// interface thread when a window of the process shows.
void FollowApplePlayer(const winrt::Microsoft::UI::Xaml::Window& mainWindow, Journal& journal,
                       uirender::PlayerMode mode) noexcept;

// Shows the main window again when the player still replaces it. Any thread: for when Ampify stops.
void ShowMainWindowAgain() noexcept;

}  // namespace ampify::win

#endif
