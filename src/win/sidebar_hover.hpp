#pragma once
// apple.sidebar = hover: Apple's sidebar stays folded to its column of icons and opens over the pages while the pointer
// is on it, like Spicetify's hover panels: the pages keep the whole width. Turned off, the sidebar goes back to Apple's
// own way (open when the window is wide enough, folded below).

#include <functional>

#if defined(AMPIFY_WITH_XAML)

namespace ampify {
class Journal;
}

namespace ampify::win {

// Folds the sidebar and puts the pointer handlers on it, or checks they are still there and closes a sidebar the
// pointer left while a menu held it open; turned off (SetSidebarOnHover), gives the sidebar back. Interface thread
// only, cheap once in place.
void KeepSidebarOnHover(Journal& journal) noexcept;

// apple.sidebar: on hover or open, applied at the next KeepSidebarOnHover. Any thread.
void SetSidebarOnHover(bool enabled) noexcept;

// True while Ampify keeps the sidebar folded: the themes' folded rules (@media (max-width: 1324px)) then hold at
// every width. False while the hamburger pins the pane open. Any thread.
bool SidebarKeptFolded() noexcept;

// Called on the interface thread when a click pins or unpins the pane, so the theme re-checks its media rules at once.
void SetSidebarChangedListener(std::function<void()> changed);

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
