#pragma once

// Built-in CSS for the scroll bars and the centred playback bar. Kept out of builtins.cpp, which would pass its
// 800-line ceiling. Used by the table of built-ins there.
#include <string_view>

namespace ampify::theme {

// Scroll bars that sit inside their panel: off its edge, clear of its rounded corners, without the arrows Windows adds
// with "Always show scroll bars" (they stuck out at both ends). Part of floating-panels; alone for the other layouts.
inline constexpr std::string_view kScrollbars = R"css(
ScrollBar RepeatButton#VerticalSmallDecrease,
ScrollBar RepeatButton#VerticalSmallIncrease,
ScrollBar RepeatButton#HorizontalSmallDecrease,
ScrollBar RepeatButton#HorizontalSmallIncrease {
    visibility: collapsed;
}

/* The scroll bar's expanded state animates the arrows' visibility back: no room and no ink keeps them gone. */
ScrollBar RepeatButton#VerticalSmallDecrease,
ScrollBar RepeatButton#VerticalSmallIncrease {
    height: 0;
    opacity: 0;
}

ScrollBar RepeatButton#HorizontalSmallDecrease,
ScrollBar RepeatButton#HorizontalSmallIncrease {
    width: 0;
    opacity: 0;
}

/* Out of sight until the pointer reaches it: the wheel scrolls without it, and it no longer cuts into the panels. */
.scrollbar {
    background: transparent;
    opacity: 0;
    transition: opacity 160ms;
}

.scrollbar:hover {
    opacity: 1;
}

ScrollBar#VerticalScrollBar {
    margin: var(--panel-radius, 8px) 3 var(--panel-radius, 8px) 0;
}

ScrollBar#HorizontalScrollBar {
    margin: 0 var(--panel-radius, 8px) 3 var(--panel-radius, 8px);
}
)css";

// The playback bar with the buttons centred and the progress line under them, the times at its ends. Pairs with
// bottom-bar (import it first). The line and the times move out of the song's display into the centre cell, which
// has no rows: the buttons stand at its top, the line at its foot, so both share the cell.
inline constexpr std::string_view kCentreBar = R"css(
.transport {
    vertical-alignment: top;
    margin: 4 0 0 0;
}

.scrubber {
    move-to: "Grid#TransportBar > Grid:nth-child(1)";
    grid-row: 0;
    grid-column: 0;
    grid-column-span: 4;
    vertical-alignment: bottom;
    horizontal-alignment: stretch;
    height: 6;
    margin: 0 calc(var(--line-inset, 240) + var(--time-room, 44)) 8 calc(var(--line-inset, 240) + var(--time-room, 44));
}

/* The times stand at the ends of the line, in its own cell. Their opacity, not their visibility, which Apple Music binds
   to the pointer. Written after floating-panels, so they show on narrow windows too. */
.current-time,
.duration {
    move-to: "Grid#TransportBar > Grid:nth-child(1)";
    grid-row: 0;
    grid-column: 0;
    grid-column-span: 4;
    vertical-alignment: bottom;
    margin: 0 0 4 0;
    opacity: 1;
}

.current-time {
    horizontal-alignment: left;
    margin: 0 0 4 var(--line-inset, 240);
}

.duration {
    horizontal-alignment: right;
    margin: 0 var(--line-inset, 240) 4 0;
}
)css";

}  // namespace ampify::theme
