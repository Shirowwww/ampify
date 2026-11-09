#pragma once
// Where Apple Music folds its sidebar, a fact of Apple's layout that the theme engine's media rules
// (theme/css.hpp) and the hover sidebar's rules (sidebar_hold.hpp) both follow.

namespace ampify {

// Apple Music folds its sidebar to its icons at this window width and less, in device-independent pixels: themes write
// (max-width: 1324px) for "folded" and (min-width: 1325px) for "open".
inline constexpr double kSidebarFoldsAt = 1324;

}  // namespace ampify
