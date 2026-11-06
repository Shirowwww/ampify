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
// has no rows: the buttons stand at its top, the line at its foot, so both share the cell. The line's template repeats
// its margin inside: half the room goes on each side, and a top or bottom margin would take its 6 pixels (none drew).
inline constexpr std::string_view kCentreBar = R"css(
.transport {
    vertical-alignment: top;
    margin: 4 0 0 0;
}

/* The line and the times stand 10 above the bar's edge; the buttons, at the top, do not move. */
.playbar-controls {
    padding: 0 0 10 0;
}

.scrubber {
    move-to: "Grid#TransportBar > Grid:nth-child(1)";
    grid-row: 0;
    grid-column: 0;
    grid-column-span: 4;
    vertical-alignment: bottom;
    horizontal-alignment: stretch;
    height: 6;
}

/* Apple Music shows the thumb while the pointer is over the song; out of it, the line shows it under the pointer. */
.scrubber:hover Thumb#HorizontalThumb {
    visibility: visible;
}

/* Always visible: out of the song, Apple's pointer states no longer hide them. The opacity is written after
   floating-panels, so they show on narrow windows too. Centred on the track: 6 under the cell's foot. */
.current-time,
.duration {
    move-to: "Grid#TransportBar > Grid:nth-child(1)";
    grid-row: 0;
    grid-column: 0;
    grid-column-span: 4;
    vertical-alignment: bottom;
    opacity: 1;
    visibility: visible;
}

.current-time {
    horizontal-alignment: left;
}

.duration {
    horizontal-alignment: right;
}

/* The line keeps clear of the song on the left, and as far from the right edge: centred under the buttons at every
   width. Each step follows the song's width in bottom-bar; --line-inset sets one inset for all of them. */
.scrubber {
    margin: 0 calc((var(--line-inset, calc(var(--song-width, 560) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2) 0 calc((var(--line-inset, calc(var(--song-width, 560) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2);
}

.current-time {
    margin: 0 0 -6 var(--line-inset, calc(var(--song-width, 560) + var(--gap, 8) + 16));
}

.duration {
    margin: 0 var(--line-inset, calc(var(--song-width, 560) + var(--gap, 8) + 16)) -6 0;
}

@media (max-width: 1600px) {
    .scrubber {
        margin: 0 calc((var(--line-inset, calc(var(--song-width-medium, 460) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2) 0 calc((var(--line-inset, calc(var(--song-width-medium, 460) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2);
    }

    .current-time {
        margin: 0 0 -6 var(--line-inset, calc(var(--song-width-medium, 460) + var(--gap, 8) + 16));
    }

    .duration {
        margin: 0 var(--line-inset, calc(var(--song-width-medium, 460) + var(--gap, 8) + 16)) -6 0;
    }
}

@media (max-width: 1324px) {
    .scrubber {
        margin: 0 calc((var(--line-inset, calc(var(--song-width-folded, 380) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2) 0 calc((var(--line-inset, calc(var(--song-width-folded, 380) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2);
    }

    .current-time {
        margin: 0 0 -6 var(--line-inset, calc(var(--song-width-folded, 380) + var(--gap, 8) + 16));
    }

    .duration {
        margin: 0 var(--line-inset, calc(var(--song-width-folded, 380) + var(--gap, 8) + 16)) -6 0;
    }
}

@media (max-width: 1100px) {
    .scrubber {
        margin: 0 calc((var(--line-inset, calc(var(--song-width-narrow, 340) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2) 0 calc((var(--line-inset, calc(var(--song-width-narrow, 340) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2);
    }

    .current-time {
        margin: 0 0 -6 var(--line-inset, calc(var(--song-width-narrow, 340) + var(--gap, 8) + 16));
    }

    .duration {
        margin: 0 var(--line-inset, calc(var(--song-width-narrow, 340) + var(--gap, 8) + 16)) -6 0;
    }
}

@media (max-width: 960px) {
    .scrubber {
        margin: 0 calc((var(--line-inset, calc(var(--song-width-small, 270) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2) 0 calc((var(--line-inset, calc(var(--song-width-small, 270) + var(--gap, 8) + 16)) + var(--time-room, 44)) / 2);
    }

    .current-time {
        margin: 0 0 -6 var(--line-inset, calc(var(--song-width-small, 270) + var(--gap, 8) + 16));
    }

    .duration {
        margin: 0 var(--line-inset, calc(var(--song-width-small, 270) + var(--gap, 8) + 16)) -6 0;
    }
}
)css";

}  // namespace ampify::theme
