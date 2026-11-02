#pragma once

// Apple Music's notifications (the one of each new song, among others), held back by Ampify's rule: never, or while
// Apple Music is the window in front. Apple Music shows them with AppNotificationManager.Show of the Windows App SDK;
// the first rule other than Apple's own replaces that method's vtable entry, and the replacement then follows every
// later rule. Compiled only with AMPIFY_WITH_XAML (AppleMusic.exe).

#if defined(AMPIFY_WITH_XAML)

#include "../core/journal.hpp"
#include "../core/ui_render/apple_tweaks.hpp"

namespace ampify::win {

// Any thread. A failed hook is logged once and not tried again.
void SetNotificationRule(uirender::NotificationRule rule, Journal& journal);

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
