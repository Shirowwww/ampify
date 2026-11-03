#pragma once

#if defined(AMPIFY_WITH_XAML)

#include <windows.h>

#include <functional>

#include "../core/journal.hpp"

namespace ampify::win {

// XAML Diagnostics in Apple Music (setting xaml.watcher = 1): asks WinUI to tell Ampify of every element added to the
// tree, so a theme reaches it at once instead of at the next tick. Call it once, from a thread that is not the
// interface thread, after the main window exists.
void StartXamlWatcher(Journal& journal, HINSTANCE kernel);

// Called on the interface thread for each element added, once the watcher runs. Set it from that thread.
void SetXamlWatcherListener(std::function<void()> added);

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
