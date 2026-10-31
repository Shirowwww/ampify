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
- The Marketplace page, opened with a button next to the lyrics and queue buttons.
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
- On Apple Music 1.1792, the player interfaces changed, so the current song is read through the Windows media controls
  (SMTC) and some fields are empty.
- The binaries are not code-signed: Windows SmartScreen can ask for a confirmation.
