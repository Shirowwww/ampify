#pragma once

// Ampify's mini player window (src/core/mini_player.hpp holds what it shows). It lives on a thread of its own, with its
// own DispatcherQueue, so that a slow frame of the mini player never holds Apple Music's interface thread. Compiled only
// with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <filesystem>
#include <string>

#include "../core/journal.hpp"

namespace ampify::win::miniwin {

// Call once, on the kernel's extension thread, before the first request. `kit` is the Ampify folder (the cover is
// written under kit\cache); `savedPlacement` is the mini.position setting. Nothing is created until the first request:
// the thread and the window start when the mini player is first opened.
void Install(Journal& journal, std::filesystem::path kit, std::string savedPlacement);

}  // namespace ampify::win::miniwin

#endif
