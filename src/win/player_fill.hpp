#pragma once

// The button Ampify puts beside the close button of the full-screen player it laid out as a window
// (apple_player.cpp): it fills the screen with the player, then gives the window back. It takes the place of Apple's
// shrink button, which Ampify hides there, with that button's look.

#if defined(AMPIFY_WITH_XAML)

#include <windows.h>  // before the projection, as in the PCH: one macro state for the whole file (the CI builds without it)
#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <winrt/Microsoft.UI.Xaml.h>

#include <functional>

namespace ampify::win {

// Shows the button, or updates it: `filled` turns it into the way back to a window. `press` runs on its click.
void ShowFillButton(const winrt::Microsoft::UI::Xaml::UIElement& root, bool filled, std::function<void()> press) noexcept;

// Takes it away: Apple's own full screen keeps Apple's buttons only.
void RemoveFillButton(const winrt::Microsoft::UI::Xaml::UIElement& root) noexcept;

}  // namespace ampify::win

#endif
