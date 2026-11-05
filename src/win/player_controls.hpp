#pragma once

// Shuffle and repeat in Apple's full-screen player. Its transport has only previous, play and next; Ampify adds the
// main window's two buttons around them, with their style, icon and dot, sized like their neighbours. A click acts on
// the main window's own button (its automation peer), so Apple Music keeps a single state, and the copies follow
// that state while the player is open. Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <windows.h>  // before the projection, as in the PCH: one macro state for the whole file (the CI builds without it)
#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <string>

#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/journal.hpp"
#include "player_buttons.hpp"

namespace ampify::win {

// Call on the interface thread, once the player's content exists. Does nothing when the buttons are there already,
// or when either window lacks what it needs (another Apple Music version): the log says which.
void AddPlayerControls(const winrt::Microsoft::UI::Xaml::UIElement& playerRoot,
                       const winrt::Microsoft::UI::Xaml::UIElement& mainRoot, Journal& journal) noexcept;

// apple.playerControls: off, the next opening adds nothing and an open player loses the two buttons. Any thread.
void SetPlayerControlsShown(bool shown) noexcept;

// The main window's shuffle and repeat buttons, for Ampify.player.shuffle() and the others (src/win/player_buttons.hpp).
// Interface thread only. Read: -1 for a button not found. Press: the button's own command, true when it was invoked.
MainPlayerModes ReadMainPlayerModes() noexcept;
bool PressMainPlayerButton(bool shuffle) noexcept;

// Apple's volume: the main window's volume control gets the slider position of `gain` (0 to 1), and Apple's own code
// sets the player volume and draws its slider. Interface thread only; true once the control holds the position.
bool SetMainPlayerVolume(double gain) noexcept;
// The start in ms of the line that the main window's lyrics view lights up (src/win/player_buttons.hpp), -1 while
// that view or its current line is not there. Interface thread only.
long long ReadLyricsLineStart() noexcept;

// Why the last SetMainPlayerVolume failed, once (empty when it did not). Any thread.
std::string TakeMainPlayerVolumeProblem();

}  // namespace ampify::win

#endif
