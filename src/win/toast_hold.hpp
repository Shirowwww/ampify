#pragma once

// The notification of each new song does not come from AppleMusic.exe: AMPLibraryAgent.exe shows it with
// ToastNotifier.Show of Windows.UI.Notifications. The rule of the Ampify page (src/win/notifications.hpp covers the
// interface process) holds it back there too: the first rule other than Apple's own replaces that method's vtable
// entry in this process, and the replacement then follows every later rule.

#include <string>

#include "../core/journal.hpp"
#include "../core/ui_render/apple_tweaks.hpp"

namespace ampify::win {

// Any thread. A failed hook is logged once and not tried again.
void SetToastRule(uirender::NotificationRule rule, Journal& journal);

}  // namespace ampify::win
