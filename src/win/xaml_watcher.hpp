#pragma once

#if defined(AMPIFY_WITH_XAML)

#include <windows.h>

#include "../core/journal.hpp"

namespace ampify::win {

// Trial of XAML Diagnostics in Apple Music (setting xaml.watcher = 1, off by default): asks WinUI to tell Ampify of
// every element added to or removed from the tree, instead of finding them by walking it. For now it only logs what
// it is told, to measure whether it works and holds; the themes still follow pages as before. Call it once, from a
// thread that is not the interface thread, after the main window exists.
void StartXamlWatcher(Journal& journal, HINSTANCE kernel);

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
