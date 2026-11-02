#pragma once

// apple.close = tray (the Ampify card, "Closing the window"): closing Apple Music's main window hides it behind an
// icon of the notification area instead of quitting, and the music goes on. A click on the icon brings the window back;
// its menu shows it or quits. This icon is Apple Music's, shown only while the window hides, apart from Ampify's own
// (tray.hpp). The main window's WM_CLOSE is caught by a subclass, before WinUI turns it into AppWindow.Closing, so
// Apple Music never starts to shut down. Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/journal.hpp"

namespace ampify::win {

// Call on the main window's interface thread, at each refresh of that window. Nothing is touched until the rule is
// first on; turning it off while the window is hidden shows the window again.
void FollowCloseRule(const winrt::Microsoft::UI::Xaml::Window& mainWindow, bool toTray, Journal& journal) noexcept;

// Any thread, for when Ampify stops: removes the icon and shows the window if it is hidden.
void ReleaseCloseToTray() noexcept;

}  // namespace ampify::win

#endif
