#pragma once

// Apple's mini player as Ampify drives it: opened, closed or toggled (Ampify.ui.miniPlayer, the tray icon's menu) with
// Apple's own objects, and its window made see-through when an element asks (Ampify.ui.miniPlayer.set). Apple opens
// the mini player with AMP.UI.Views.MiniPlayer (the artwork of the playback bar does the same); this module closes it
// with the window's close message, as Alt+F4 does, so Apple Music runs its own closing and shows its main window.
// Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/journal.hpp"
#include "../core/ui_model.hpp"

struct HWND__;

namespace ampify::win {

// Open, close or toggle, on the main window's interface thread. Open brings a mini player already open to the front;
// toggle closes one on screen and opens it otherwise. Never throws; the log says what happened.
void RunMiniPlayerAction(MiniPlayerAction action, Journal& journal) noexcept;

// What Ampify changed in the mini player's window, to give it back.
struct MiniWindowState {
    double opacity = 1.0;    // the one applied, 1 when Apple's own
    bool madeLayered = false;  // WS_EX_LAYERED was added by Ampify (Apple's window does not have it)
};

// The window's opacity, on the mini player's interface thread: below 1 the window becomes layered with that alpha; at
// 1 Ampify's change is taken back. Returns the state to keep for the next call; `journal` may be null. Never throws.
MiniWindowState FollowMiniOpacity(HWND__* window, double opacity, MiniWindowState state, Journal* journal) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
