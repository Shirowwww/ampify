#pragma once

// Shuffle and repeat in Apple's full-screen player. Its transport has only previous, play and next; Ampify adds the
// main window's two buttons around them, with their style, icon and dot, sized like their neighbours. A click acts on
// the main window's own button (its automation peer), so Apple Music keeps a single state, and the copies follow
// that state while the player is open. Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/journal.hpp"

namespace ampify::win {

// Call on the interface thread, once the player's content exists. Does nothing when the buttons are there already,
// or when either window lacks what it needs (another Apple Music version): the log says which.
void AddPlayerControls(const winrt::Microsoft::UI::Xaml::UIElement& playerRoot,
                       const winrt::Microsoft::UI::Xaml::UIElement& mainRoot, Journal& journal) noexcept;

}  // namespace ampify::win

#endif
