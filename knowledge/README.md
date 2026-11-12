# Knowledge

What was learned about Apple Music for Windows while building Ampify, and the raw lists the code and tests read.

## Notes

| File | Content |
|---|---|
| [apple-music-internals.md](apple-music-internals.md) | The package, the two processes, COM, playback, audio output, pitfalls |
| [apple-music-xaml-tree.md](apple-music-xaml-tree.md) | The XAML tree of the main window, with selectors and the duplicate-name traps |
| [apple-music-native-views.md](apple-music-native-views.md) | Apple's mini player, full-screen player and karaoke lyrics, and what Ampify reaches of them |
| [apple-music-queue.md](apple-music-queue.md) | The play queue: reading and removing work, adding a song does not yet |
| [apple-music-playback.md](apple-music-playback.md) | Apple's player engine for a mix: what works, what does not, the 50 ms switch delay, Apple's song analysis |
| [apple-music-library-file.md](apple-music-library-file.md) | `Library.musicdb`: encryption, records, play counts, local-time dates |
| [player-buttons.md](player-buttons.md) | Shuffle and repeat buttons: how their state reads and what presses them |
| [atmos-loudness.md](atmos-loudness.md) | Why Atmos plays quieter than stereo, Apple's own leveling, and the gain that works |
| [amexclusive-notes.md](amexclusive-notes.md) | What AMExclusive teaches about finding the window, the UI thread and Apple's player |
| [theme-effects-feasibility.md](theme-effects-feasibility.md) | How colour filters and liquid glass are built, and what themes in the mini player would need |
| [spicetify.md](spicetify.md) | How Spicetify patches Spotify, what its themes and extensions use, and what its design teaches |

## Raw lists

Plain lists of type, method and export names read from Apple Music's own metadata and export tables. They hold names
only, no code and no data from Apple's files. Ampify calls the app's services and resolves functions by name, never by
address.

| Path | Content | Read by |
|---|---|---|
| `winmd/*.txt` | Types, methods and fields of `AMP.Services`, `AMP.UI.Controls` and `AMP.UI.Views` | `tests/player_model_tests.cpp`, `tests/win/amp_player_tests.cpp` (the slot number of each player method) |
| `exports/AVFoundationCF.txt` | Export names of `AVFoundationCF.dll` | `tools/validate.py` (every export the kit requires must be in the list) |
| `manifest-com.txt` | The COM server section of Apple Music's `AppxManifest.xml` | Referenced by the bench case `30-bench-injection.ps1` |

After an Apple Music update, regenerate a `winmd` list with `pwsh -File tools/dump-winmd.ps1 -Winmd <file.winmd> -Out
knowledge/winmd/<name>.txt` and compare it with the old one. A changed method order in `winmd/AMP.Services.txt` changes
the slot numbers `src/win/amp_player.cpp` uses.
