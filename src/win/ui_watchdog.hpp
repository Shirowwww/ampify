#pragma once

// The freeze watch of the interface on Windows (logic in src/core/ui_watchdog.*): a heartbeat is queued to the
// interface thread every 200 ms, and a monitor thread reads it every 100 ms. Turned on by apple.watchdog. It is never
// stopped: it lives as long as Apple Music's process.

#include "../core/journal.hpp"

namespace ampify::win {

// Starts the watch in the calling process. It waits for the interface thread to be attached before it counts.
void StartUiWatchdog(Journal& journal);

}  // namespace ampify::win
