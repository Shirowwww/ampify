#pragma once

// Ampify's mini player window (src/core/mini_player.hpp holds what it shows). It lives on Apple Music's interface
// thread, as WinUI documents secondary windows, and closes with the main window. Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <filesystem>
#include <string>

#include "../core/journal.hpp"

namespace ampify::win::miniwin {

// Call once, on the kernel's extension thread, before the first request. `kit` is the Ampify folder (the cover is
// written under kit\cache); `savedPlacement` is the mini.position setting. Nothing is created until the first request:
// the window is made when the mini player is first opened.
void Install(Journal& journal, std::filesystem::path kit, std::string savedPlacement);

}  // namespace ampify::win::miniwin

#endif
