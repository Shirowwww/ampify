# Knowledge

What was learned about Apple Music for Windows while building Ampify, and the raw lists the code and tests rely on.

## Notes

| File | Content |
|---|---|
| [apple-music-internals.md](apple-music-internals.md) | The package, the two processes, COM, playback, audio output, pitfalls |
| [apple-music-xaml-tree.md](apple-music-xaml-tree.md) | The real XAML tree of the main window, with selectors, and the duplicate-name traps |
| [atmos-loudness.md](atmos-loudness.md) | Why Atmos plays quieter than stereo, what Apple's own leveling does, and the gain that works |
| [amexclusive-notes.md](amexclusive-notes.md) | What the AMExclusive project teaches about finding the window, the UI thread and Apple's player |
| [apple-music-native-views.md](apple-music-native-views.md) | Apple's mini player, full-screen player and karaoke lyrics, and how they compare with Ampify's Now Playing |
| [apple-music-queue.md](apple-music-queue.md) | Apple's play queue: reading and removing work, what was tried to add a song and why it does not work yet |
| [spicetify.md](spicetify.md) | How Spicetify patches Spotify, what its themes and extensions use, and the lessons Ampify takes |

## Raw lists

These files are plain lists of type names, method names and export names that were read from Apple Music's own metadata and
export tables. They hold names only, no code and no data from Apple's files. They are kept for interoperability: Ampify
calls the app's services by name and resolves functions by name, never by address. Tests read some of them.

| Path | Content | Used by |
|---|---|---|
| `winmd/*.txt` | Types, methods and fields of `AMP.Core`, `AMP.Services`, `AMP.UI.Controls`, `AMP.UI.ViewModels` and `AMP.UI.Views`, read from their `.winmd` metadata | `tests/player_model_tests.cpp`, `tests/win/amp_player_tests.cpp` (the slot number of each player method) |
| `exports/*.txt` | Export names of `AVFoundationCF.dll`, `CoreMedia.dll` and `AMPLibraryAgent.Proxies.dll` | `tools/validate.py` (the exports Ampify requires must exist in the list) |
| `manifest-com.txt` | The COM server section of Apple Music's `AppxManifest.xml` | Documentation for the COM class `AMP.Core.IAMPMusicLibrary` |

To refresh a list after an Apple Music update, regenerate it from the new package and compare it with the old one. A changed
method order in `winmd/AMP.Services.txt` changes the slot numbers used by `src/win/amp_player.cpp`.
