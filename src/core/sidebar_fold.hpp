#pragma once
// Where Apple Music folds its sidebar, a fact of Apple's layout that the theme engine's media rules
// (theme/css.hpp), the hover sidebar's rules (sidebar_hold.hpp) and the side panels all follow.

namespace ampify {

// Apple Music folds its sidebar to its icons at this window width and less, in device-independent pixels: themes write
// (max-width: 1324px) for "folded" and (min-width: 1325px) for "open".
inline constexpr double kSidebarFoldsAt = 1324;

// Apple's own right pane (lyrics, Up Next), from the resources of its MainPage (Apple Music 1.1792): 360 pixels wide
// (RightContentPaneWidth), beside the pages from a 1440-pixel window (RightContentPaneSnapPoint) and over them in a
// narrower one. While it is open, Apple folds its sidebar below 1685 (NormalWindowWithRightContentPaneSnapPoint): its
// layout goes by the window less the pane.
inline constexpr double kApplePaneWidth = 360;
inline constexpr double kApplePaneBesideAt = 1440;

// Whether a column of side panels `room` wide stands beside the pages in a window `windowWidth` wide, with Apple's own
// pane taking `applePane` beside them too (0 while it is closed or over the pages): from where Apple lets its own pane
// stand there, so the pages keep what Apple's layout gives them. A column already there stays down to that width; a
// new one waits for `margin` more, so it does not come and go at the edge.
inline bool ColumnStandsBesidePages(double windowWidth, double room, double applePane, bool standing, double margin) {
    const double least = kApplePaneBesideAt - kApplePaneWidth + room + applePane;
    return windowWidth >= least + (standing ? 0.0 : margin);
}

}  // namespace ampify
