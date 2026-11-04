# Apple's shuffle and repeat buttons (Apple Music 1.1792)

`Ampify.player.shuffle()`, `repeat()` and their setters read and press the main window's `ShuffleButton` and
`RepeatButton` through their automation peers (`src/win/player_controls.cpp`). What was measured on the real app:

- **Shuffle** is a `ToggleButton`: `IsChecked` is the state and `Toggle()` changes it at once.
- **Repeat** is a plain `Button`. Its dot (`RepeatButtonDot`) is `Visible`, opaque and present in every state; only its
  fill changes: `#00FFFFFF` (transparent) while repeat is off, the accent colour when it is on. Reading `Visibility`
  or `Opacity` reports "on" all the time.
- **Repeat one** is told from repeat all by the icon, two UTF-16 units: `U+10029E` for off and all, `U+10029F` for one.
  Reading only the first unit gives the same value for both.
- The automation name is the state, localised: "Ne pas répéter" (off), "Tout répéter" (all), "Répéter un élément" (one)
  in French. The cycle is off, all, one, off. Do not rely on the name.
- A press takes effect on Apple's own turn: the first one after Apple Music starts took 2.8 s, the following ones 0.1 s.
  Pressing again before the state changes skips states, so a setter presses once and waits for the change.

The volume (`Ampify.player.setVolume`) goes through the leveler's `AVCFPlayerSetVolume` hook with the player Apple last
set a volume on, so it needs a song to have started. Apple's own slider is not redrawn.

## Not understood yet

On one PC (Apple Music 1.1792, default output a 96 kHz float stereo endpoint), with a song playing, Windows' session
and endpoint peak meters read 0 on every output, with or without Ampify, and the leveler's `IAudioClient::Initialize`
hooks saw no stream (the log line "audio stream ..." never appeared). The equalizer and the loudness meter could not be
checked there. The leveler's log now names every stream format it follows or leaves alone, to tell apart an exclusive or
24-bit stream from a stream opened before the hooks.
