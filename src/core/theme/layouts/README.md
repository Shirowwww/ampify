# Ready-made layouts

Each file here is served to themes as `@import "ampify:<file name>";` (`floating-panels` also brings
`scrollbars`). The build copies them into the kernel (`cmake/layouts.cmake`): edit the CSS, rebuild. What
each rule does and the few rules Ampify's own code relies on: `docs/themes/format.md`, "Ready-made layouts".

## floating-panels

Rounded panels floating on the window, with even gaps and aligned edges: the sidebar on the left, the pages on the right below the playback bar. Colours come from variables a theme sets in its :root. The title bar strip keeps its own background: with a colour, it catches the clicks meant for the buttons under it.

## bottom-bar

The playback bar along the bottom of the window, under both panels: the song on the left, the controls in the middle, volume and toggles on the right. Pairs with floating-panels (import it first).

## scrollbars

Scroll bars that sit inside their panel: off its edge, clear of its rounded corners, without the arrows Windows adds with "Always show scroll bars" (they stuck out at both ends). Part of floating-panels; alone for the other layouts.

## centre-bar

The playback bar with the buttons centred and the progress line under them, the elapsed time at its left end and the length at its right end. Pairs with bottom-bar (import it first). The line and the time move out of the song's display into the centre cell, which has no rows: the line stands at its foot and the buttons just above it, so both share the cell. The line's template repeats its margin inside: half the room goes on each side, and a top or bottom margin would take its 6 pixels (none drew).
