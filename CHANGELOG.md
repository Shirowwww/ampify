# Changelog

## 0.5.0 (unreleased)

The first version meant to be tested in full on a real PC. It is not published yet: it waits for a listening check of the
Atmos leveler.

### Included

- The installer `ampify_setup.exe`, and a clean uninstall from Windows Settings. Ampify never touches Apple Music's package.
  After an uninstall, Apple Music starts as before.
- The launcher and the kernel, loaded into both Apple Music processes (`AppleMusic.exe` and `AMPLibraryAgent.exe`), with
  safe mode: after two failed starts, Apple Music opens with no mods.
- JavaScript extensions, with the whole API (`Ampify.*`) and the React-style layer (`Ampify.React`, `Ampify.components`).
- Themes (colors and `user.css` rules), chosen in the Marketplace page. Spicetify `color.ini` files work as they are, and
  `ampify theme import` brings in a whole Spicetify theme.
- Stable names for about 120 zones of Apple Music (`.sidebar`, `.playbar`, `.card`...) and ready-made layouts
  (`@import "ampify:floating-panels"`, `"ampify:bottom-bar"`) with aligned edges. `ampify_theme_check --names` checks the
  names against inspector trees after an Apple Music update.
- Themes apply before a new page is drawn, with animated transitions, `:nth-child`, and four example themes: Greenroom
  (play bar at the bottom), Artwork, Glass and Midnight. A button keeps the theme's colours under the pointer and while
  pressed (Greenroom's white play button used to turn black when clicked). Rules accept `border-radius` as well as
  `corner-radius`, and `min-width`, `max-width`, `min-height`, `max-height`, `compact-pane-length`, and values can
  use `calc()`. `@media` rules follow the window's size. In a narrow window, `floating-panels` keeps the folded
  sidebar at Apple Music's 48 pixels, whole, with the same gap to the pages as when it is open, and `bottom-bar` keeps
  the song in view. The window's own buttons
  (minimize, maximize, close) follow the theme's light or dark mode: they were black on dark themes on a PC in light
  mode.
- The inspector shows what each element's visual states change. It no longer walks Apple Music's resource dictionaries,
  which made the app crash at the next theme change.
- Now Playing: a button of the playback bar that opens Apple Music's own full-screen player as a window laid over the
  main one, without a title bar (`Ampify.ui.applePlayer.open()`). The view Ampify drew before, the cover on a blurred
  background with the lyrics in time (Apple Music's own, or LRCLIB's), is now the `lyrics-view` example.
- The Marketplace page, opened from the Ampify entry at the bottom of the sidebar. It reads the public catalog,
  Shirowwww/ampify-catalog.
- The "Ampify" card in Settings > Playback: one switch per extension, with an immediate effect.
- The Atmos leveler: Dolby Atmos songs are brought to the level of stereo songs, without ever moving the volume slider.
- The `ampify` command line and the catalog (`ampify market`, `ampify install`, `ampify update`).
- The player: current song, play and pause, next song.
- The installer puts `ampify.exe` next to the kernel, because the Marketplace page runs it.

### Preview

The rest exists but is not at the same level yet. It is marked "preview" or off by default: apps (added pages), snippets,
the equalizer, audio analysis, Discord presence, scrobbling, the local remote, MIDI and the notification-area icon.
The fullscreen view (`Ampify.ui.fullscreen`, permission `ui.fullscreen`) and the style attributes of the interface
primitives (`layer`, backgrounds and acrylic, text size and color, opacity, scale and transitions) are in preview too.

### Known limits

- Tested on Apple Music 1.1540 and 1.1792. The shipping build injects the kernel only on the versions of its list
  (`core/version_gate.hpp`, or the `versions.txt` downloaded by `ampify versions update`). On any other version, Apple Music
  opens normally with no mods.
- The places of panels, of the Ampify card and of the theme choice may still move.
- The Atmos leveler passed the automated tests, and still needs a listening check on a real PC.
- On Apple Music 1.1792, Apple keeps the cover's address internal, so `track.artwork` is empty (Now Playing takes the
  cover from the iTunes Search API), and Apple's lyrics only come once its lyrics panel has been open for the song.
- The binaries are not code-signed: Windows SmartScreen can ask for a confirmation.
