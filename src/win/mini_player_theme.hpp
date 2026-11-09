#pragma once

// Themes and snippets in Apple's mini player. That window has an interface thread of its own
// (knowledge/apple-music-native-views.md), and the theme engine only walks and writes a tree from the thread that owns
// it, so the mini player gets a second copy of the engine: its own XamlTreeBackend and ThemeSession, on its thread,
// fed the very request the theme service loads for the main window. The theme, its scheme, the snippets and the
// artwork's colours are read and parsed once, and the same selectors (`.mini-player`, `.mini-lcd`... in
// docs/themes/names.md) style both windows. Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include "../core/journal.hpp"
#include "../core/theme/session.hpp"

namespace ampify::win {

// The theme service's worker thread, on each tick: the request the main window gets now. While the mini player is
// open, it gets the same, on its own thread; once it closes, its copy of the engine goes with it.
void OfferMiniPlayerTheme(const theme::SessionRequest& request, Journal& journal);

// Any thread: the mini player window appeared (the interface service saw it). The last request applies at once
// instead of at the next tick, so the window does not show Apple's colours first.
void MiniPlayerOpened() noexcept;

// The theme service stops: the mini player's copy is let go on its thread, and no request reaches it any more.
void StopMiniPlayerTheme() noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
