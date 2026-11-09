#pragma once
// Every change Ampify makes to Apple Music's own behaviour that a running window follows (the "Apple Music" part of
// Ampify's settings page, core/ui_render/apple_tweaks.hpp) is one row of one list (apple_tweak.cpp): what it does
// with a new reading of settings.txt, and what it keeps in place on the theme tick. Adding a tweak is adding a row.
// Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include "../core/ui_render/apple_tweaks.hpp"

namespace ampify {
class Journal;
}

namespace ampify::win {

// The settings changed: each tweak takes its value. Any thread (the interface service's worker).
void ApplyAppleTweaks(const uirender::AppleTweaks& tweaks, Journal& journal);

// One look at the main window by each tweak that keeps something in it, in the list's order. Interface thread only,
// from the theme tick, which runs only while the main window is shown: the tweaks pause while it is hidden.
void KeepAppleTweaks(Journal& journal) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
