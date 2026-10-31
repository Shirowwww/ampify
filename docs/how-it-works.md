# How Ampify works

## In plain words

Apple Music for Windows is a native Windows app. It does not run on a web page, so tools like Spicetify, which edit a
web bundle, cannot work on it. Ampify takes a different route:

1. A small program, the launcher, starts Apple Music for you.
2. It pauses Apple Music for a moment and slips a library, the kernel, into it. Then it lets Apple Music continue.
3. Inside Apple Music, the kernel changes colors and shapes of the interface while it runs, and runs small JavaScript
   programs (extensions) that can add buttons, panels and pages.

Nothing is written to Apple Music's install folder, which Windows keeps read-only. Every change lives in memory and
disappears when Apple Music closes. When you remove a theme, Ampify puts every value back as it was, without a restart.

Rules that never change:

- Ampify never touches FairPlay or any copy protection.
- Ampify never moves Apple Music's volume slider.
- On an Apple Music version it has not been tested with, the kernel does nothing.

## The pieces

| Piece | Role | Source |
|---|---|---|
| `ampify_launcher.exe` | Starts Apple Music through its package, pauses it, injects the kernel, resumes | `launcher/`, `win/package.cpp` |
| `ampify_kernel.dll` | The library injected into both Apple Music processes | `kernel/`, `core/`, `win/` |
| `ampify.exe` | The command line: settings, themes, extensions, Marketplace, author tools | `cli/` |
| `ampify_setup.exe` | Installer and uninstaller | `setup/` |
| `Ampify-Test.exe` | Test kit started by a double-click: ETW trace and report | `testkit/` |
| `ampify_theme_check.exe` | Checks a theme without Apple Music | `tools/` |

## The two Apple Music processes

Apple Music for Windows is a WinUI 3 app (Windows App SDK 1.8) packaged as MSIX. It runs as two processes.

| Process | What it does |
|---|---|
| `AppleMusic.exe` | The interface (XAML). Also contains V8, `JetEngine32.dll` (Apple's engine for store pages) and WebView2 |
| `AMPLibraryAgent.exe` | The engine: library, playback, downloads, DRM. Loads Apple's ported frameworks (`CoreFoundation`, `AVFoundationCF`, `CoreMedia`, `CoreFP`, `CoreADI64`...). It starts at the first playback, or when something activates the COM class the package declares |

The interface talks to the engine through out-of-process COM (`AMP.Core.IAMPMusicLibrary`). The kernel loads in both
processes. The JavaScript runtime in each process runs the elements that asked for that process (the manifest field
`processes`, `ui` by default).

More detail: [knowledge/apple-music-internals.md](../knowledge/apple-music-internals.md).

## The launcher

The launcher starts Apple Music through its package identity, then loads the kernel with the classic technique:

1. Start the Apple Music process suspended.
2. Read the package version and check the player library (`AVFoundationCF.dll`) exports.
3. Call `CreateRemoteThread` with `LoadLibraryW` pointing at `ampify_kernel.dll`.
4. Resume the process.

Details that matter:

- The load thread is kept and the launcher waits for it after resuming. The load often finishes a few milliseconds
  after the resume. An earlier version counted that as a failure.
- Apple Music sometimes hands control to a second process at start. The launcher keeps watching and loads the kernel
  there too. It stays open while Apple Music runs, and exits a few seconds after the app closes.
- A proxy DLL is not possible because the package is signed and read-only. Injection is the same method as
  [AMExclusive](https://github.com/AntiOblivionis/AMExclusive).
- `app_launch_flags` in the settings adds arguments to the Apple Music start.
- Safe mode lives here too: after two starts where the app did not stay healthy for 20 seconds, the launcher starts
  Apple Music with no kernel. See [getting-started.md](getting-started.md).

## The version gate

The kernel and the launcher load only on Apple Music versions in the tested list (`core/version_gate.hpp`, plus
`versions.txt` written by `ampify versions update`), and only if `AVFoundationCF.dll` still exports the names Ampify
needs (`core/build_signature.hpp`). The kernel stores no addresses and resolves everything by name. An Apple Music update
therefore breaks nothing: Ampify stays quiet until the new version is tested.

To mark a version as tested, both must hold: the bench injects cleanly on it, and its export signature is recognized.

The bench build (`AMPIFY_BENCH=ON`) accepts any version. It is for tests only and must never be shipped.

## The kernel

When the kernel loads in a process it:

1. Reads `%USERPROFILE%\Ampify\settings.txt`, opens the logs, and checks safe mode and the version gate.
2. Starts the extension host (JavaScript runtime) and the services that are switched on.
3. In `AppleMusic.exe`, starts the interface service (XAML window, theme engine, panels).
4. In `AMPLibraryAgent.exe`, installs the audio hooks used by the Atmos leveler when it is switched on.
5. About 20 seconds later, if the app is still responsive, marks the launch healthy.

The exact order can differ between versions. The log (`logs\AppleMusic.log`, `logs\AMPLibraryAgent.log`) shows what
happened.

A running kernel re-reads `settings.txt`. It also reads new lines of `reload.txt` every 500 ms, which is how `ampify`
starts, stops and reloads an extension without restarting Apple Music (see [cli.md](cli.md)).

Hooks are installed only through one gate, `core/drm_guard.cpp`, which refuses any target in CoreFP, CoreADI64 or the
FairPlay path. See [CONTRIBUTING.md](../CONTRIBUTING.md).

## The XAML window and the UI thread

Everything visual happens inside `AppleMusic.exe`.

- **Finding the window.** `Window::Current()` returns null in a desktop WinUI 3 app. The kernel calls the export
  `?GetAppProperty@SharedUtils@@YA?AUIInspectable@Foundation@Windows@winrt@@AEBUhstring@5@@Z` of `AMP.Utils.dll`
  with `"MainWindow"` (or `"MiniPlayerWindow"`), then `try_as<Window>()`. Before the window exists, the call fails with
  `0x8000000B` (E_BOUNDS). That is normal, and the kernel tries again.
- **The UI thread.** All XAML work runs through `window.DispatcherQueue().TryEnqueue(...)`, one request at a time per
  window. A `WH_GETMESSAGE` hook was tried first and never fired on Windows Server 2025.
- **Following pages.** The kernel uses WinEvent hooks and a 5-second pass, plus the `Loaded`, `Unloaded` and `Content`
  events of frames and pages. It never walks more than 512 elements and never enters item lists, because a library can
  have thousands of rows.
- **Recognizing pages.** Pages are recognized by class name (`PlaybackSettingsPage` for Settings > Playback), not by
  position in the tree.
- **Closing.** `Window.Closed` sets a flag, and nothing touches XAML after it.
- **Releasing objects.** Everything XAML is released on the UI thread (`ReleaseXaml`, `DropContext`). Static services
  are never destroyed.

The shape of the tree is in [knowledge/apple-music-xaml-tree.md](../knowledge/apple-music-xaml-tree.md). The pitfalls
met while building this are in [development/xaml-lessons.md](development/xaml-lessons.md).

### Panels, pages and the Settings card

Extensions describe an interface as a tree of primitives (`stack`, `text`, `button`, `toggle`...). The kernel renders
it with real WinUI controls (`win/ui_renderer.cpp`). Each node gets the name and automation id
`AMPIFY_<element>_<node>`, and a new render only changes the nodes that changed. Style, margins and colors are copied
from the native controls of the Settings > Playback page once it has been opened.

| Place | How it is shown |
|---|---|
| Panels (`Ampify.ui.panel`) | A 380 px column on the right, in a popup of the window, with a Close button |
| Popups | A native dialog, one at a time |
| Fullscreen view (`Ampify.ui.fullscreen`) | A popup of the window that covers all of it, above the other overlays, with a Close button and Escape |
| App pages | Cover the content when the app calls `Ampify.ui.navigate`, with a Back button |
| Playback bar buttons | In `StackPanel#NowPlayingToggleButtons`, next to the lyrics and queue buttons |
| "Ampify" card | Added to the end of Settings > Playback, re-inserted each time the page loads |

Panels sit in a popup because Apple Music removes unknown children from `Grid#PageRoot`.

## The theme engine

Spotify themes are CSS. Apple Music has no HTML, so Ampify reads a CSS subset and turns it into XAML properties. Colors
use the file format of Spicetify, so a Spicetify `color.ini` works as it is.

Pipeline:

1. **Sources.** The active theme, then each enabled snippet in alphabetical order, then `sidebar.json` and `home.json`.
   Each source brings `colors.ini` (named colors), `resources.xaml` (resource overrides) and `user.css` (rules).
2. **Rules.** A selector is a XAML type and an `x:Name` (`Grid#TransportBar`), with descendant and child combinators,
   `:nth-child` and `:hover`. A stable name written as a class (`.playbar`, `.card`) is replaced by the path it stands
   for before the selector is read, and `@import "ampify:<layout>"` brings in a ready-made layout at its place
   (`core/theme/builtins.cpp`). A declaration maps to a XAML property (`background` to `Background`, `corner-radius` to
   `CornerRadius`, ...).
3. **Cascade.** `!important`, then source order, then specificity, then place in the file.
4. **Apply.** On the UI thread, the engine finds elements that match, reads the original value, and writes the new one.
   A pass runs when Apple Music shows something new (a page navigation, list items appearing) and every 3 seconds, so a
   new page is themed before it is drawn. Rules are indexed by the name and type of their last part, so a pass over a
   few thousand elements takes milliseconds.
5. **Layers.** The Spicetify color translation forms a low layer. A rule in the theme's `user.css` always beats it.
6. **Restore.** Every written value keeps its original. Removing the theme restores all values in reverse order, with no
   restart. A hover rule restores the previous value when the pointer leaves.

Other points:

- A resource override is only re-read by `{ThemeResource}` at a mode change. After each resource change the engine
  switches light and dark, then back, so the screen updates.
- Apple Music's backgrounds do not read the Fluent brushes (`SolidBackgroundFillColorBaseBrush`...). Overriding those
  resources changes only what uses them. To color backgrounds, target the elements by name in `user.css`.
- The root `RequestedTheme` follows the scheme (Dark for a dark scheme). Popups are outside the window tree, so the
  value is copied to them at each refresh.
- `theme.scheme = auto` follows the Windows mode, and `theme.schedule` follows the time. The engine re-reads both every
  3 seconds.
- A changed theme folder (a signature of the folder is compared) is reloaded: remove, then apply again.
- The inspector (`theme.inspect = 1`, or `ampify devtools`) writes `logs\inspector.txt` and `inspector.json`: the
  resources and the element tree with a selector for each element. It is the source of real names, and of the table of
  stable names: `ampify_theme_check --names` checks that table against saved trees after an Apple Music update.
- A `transition` is handed to XAML's own implicit transitions, so the compositor animates the change. A transform
  that animates goes through `UIElement.Scale`, `Rotation` and `Translation`, around the centre unless the theme
  chooses another origin.

The engine is portable C++ in `core/theme/` and is tested on Linux. `win/theme_service.cpp` and `win/xaml_backend.cpp`
connect it to WinUI. Formats: [themes/format.md](themes/format.md).

## The JavaScript runtime

Extensions are JavaScript. The runtime is [quickjs-ng](https://github.com/quickjs-ng/quickjs) 0.17.0 (MIT), vendored
without changes in `vendor/quickjs/` (version and hash in `vendor/quickjs/UPSTREAM.txt`). QuickJS from 2021 does not
compile with MSVC (error C2059). V8 is too large and Duktape is ES5 only.

- One runtime instance per element, with a global `Ampify` object. The API follows Spicetify's shape (`Ampify.Player`,
  `Ampify.React`, `Ampify.components`...), so Spicetify authors feel at home.
- A sandbox: no file system, no native calls, network only to hosts the manifest declares. Every capability beyond the
  basics needs a permission in `manifest.json`, shown to the user before an element is switched on.
- The rendering is native XAML. There is no WebView, so the interface stays Apple Music's own and light.
- `Ampify.React` is a small engine with React's shape (hooks, fragments, props). It produces the same primitive tree
  as direct calls.
- Native extensions (DLLs) are not open to the community. A DLL runs with every right inside Apple Music, and a public
  catalog cannot vouch for it. Native features stay built into the kernel, like the Atmos leveler.

The API is in [extensions/api.md](extensions/api.md) and the manifest in [extensions/manifest.md](extensions/manifest.md).

### Code layout

Each API module lives in its own files so that several people can add modules without editing the same lines. See
[CONTRIBUTING.md](../CONTRIBUTING.md#add-an-api-module). The host exposes optional services through
`Host::Capability<T>()`.

## The Marketplace

The Marketplace is an app (`apps/marketplace`, built with `Ampify.React`). It reads `ampify market --json` and acts
through `Ampify.kit.run`, which runs `ampify.exe` beside the kernel with no window and empty input, so only `--yes`
can approve anything. Only the Marketplace holds the permission `kit.manage`, and the catalog refuses it for any other
element. The commands it may run are limited to `market`, `install`, `remove`, `update <id>`, `theme`, `extension`,
`app`, `snippet` and `list`.

Because it calls the same command line, the page applies the same checks as `ampify install` (see
[extensions/catalog.md](extensions/catalog.md)). Themes are chosen in two clicks.

The catalog lives in its own public repository, Shirowwww/ampify-catalog, where every archive is pinned by SHA-256.
`ampify` uses `tar.exe` and `curl.exe`, which ship with Windows 10 (1803) and 11, so nothing is installed for the user.

The Marketplace opens from the Ampify entry at the bottom of Apple Music's sidebar. Apple Music fills that part of its
sidebar from a list it owns, and adding to the list crashed it: Ampify puts its entries in a small panel that takes the
place of the sidebar's footer and keeps Apple Music's own separator in it.

## Now Playing

Now Playing (`extensions/now-playing`) is an extension that ships with Ampify. Its button joins the lyrics and queue
buttons of the playback bar and opens Apple Music's own full-screen player (`Ampify.ui.applePlayer`,
`win/apple_player.cpp`):

- The kernel calls `AMP.UI.Views.FullScreenPlayer.Instance().Show()`, which the full-screen button of Apple's mini
  player calls, by vtable slot (`core/amp_abi.hpp`). It refuses when the player has no view model yet.
- Apple then creates the player's window, which it names `FullScreenPlayerWindow` among its app properties
  (`GetAppProperty`, like `MainWindow`). A timer of the interface thread waits for it and for its FullScreen presenter,
  then gives it an overlapped presenter with a border and no title bar, and places it so that its visible rectangle is
  the main window's. Changing the Win32 style is not enough: the FullScreen presenter puts its own style back.
- A press that no control of the player takes (its background, the artwork, the titles), then a move of a few pixels,
  moves the window with the pointer, as a title bar would. A plain click still reaches Apple's handlers.

The lyrics view Ampify drew before is the `lyrics-view` example (`examples/lyrics-view`). It opens a view over the whole
window (`Ampify.ui.fullscreen`): a popup of the main window, above Apple Music's pages and panels, which navigation
leaves in place.

- The song comes from Apple's player (`AMP.Services`), read through the slots of its WinRT interfaces. The artist and
  album come from the item's subtitle when Apple leaves them empty.
- The lyrics are Apple Music's own (`ILyricsInfo.GetTTML`, parsed in `core/lyrics.cpp`) when Apple has fetched them. It
  does so once its lyrics panel has been open for the song, so the view falls back on LRCLIB, a free public library
  of synced lyrics, and switches to Apple's when they arrive.
- Apple's cover address is internal (`amp-artwork-id://`), so the cover comes from the iTunes Search API.
- The view is redrawn four times a second while open; the kernel only changes the lines whose state changed, and their
  opacity animates on the compositor.

## Atmos leveler

Dolby Atmos songs play quieter than stereo songs in Apple Music. The leveler fixes that with a gain on the decoded
samples. Why the obvious options fail:

- Sound Check never touches Atmos items.
- CoreMedia has its own Atmos mode (`atmos-loudness-mode`, default 8). Only mode 8 works, and it leaves Atmos about
  4 dB below stereo. Ampify does not read or write that setting.
- Changing the player volume (`AVCFPlayerSetVolume`) would move Apple Music's slider.

What Ampify does:

- Stereo is measured on the shared-mode `IAudioRenderClient` stream (float32), after the player volume.
- Atmos is measured on `ISpatialAudioObjectRenderStreamForMetadata` (mono objects at 48 kHz), before the volume. The
  measurement brings both paths to the same scale.
- Loudness is measured per song (BS.1770). One value is kept per audio variant in `atmos-levels.txt`.
- Target: the median of the last 30 stereo songs. Gain from 0 to 15 dB, peak ceiling 0.95, soft knee from 0.94.
- When AMExclusive is active (exclusive output), the leveler pauses, because it would break bit-perfect output.

Details and measurements: [knowledge/atmos-loudness.md](../knowledge/atmos-loudness.md).

## What stays untouched

- **FairPlay and DRM.** No hook in CoreFP, CoreADI64 or anything near them. `core/drm_guard.cpp` refuses every request.
  The quality lock of AMExclusive was not adopted because it edits HLS variant selection, including the protected
  `qlac` subtype, close to the protected stream path.
- **The volume bar.** The kernel reads the player volume and never writes it. Any gain acts on samples.
- **The package.** Nothing is written in Apple Music's install folder.
- **Audio output.** Samples are never recorded to disk. Scripts only receive small frames of numbers.

## Files on disk

```
%USERPROFILE%\Ampify\
  settings.txt           settings, one key = value per line
  extensions\<id>\       manifest.json, main.js, ...
  apps\<id>\
  themes\<id>\
  snippets\<id>\
  storage\<id>.json      data of Ampify.storage
  active.txt             what should load
  reload.txt             requests to a running kernel (start, stop, restart, rescan)
  backups\               copies of settings and elements
  logs\                  AppleMusic.log, AMPLibraryAgent.log, launcher.log, inspector.txt
  reports\               test kit reports
  versions.txt           extra tested Apple Music versions
  atmos-levels.txt       kept loudness values
```

The data folder is `%USERPROFILE%\Ampify`, not `%LOCALAPPDATA%`. MSIX redirects Apple Music's writes under AppData, so
the launcher and the kernel would not see the same files there.
