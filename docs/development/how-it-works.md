# How Ampify works

Apple Music for Windows is a native WinUI 3 app packaged as MSIX, with read-only files. Ampify works from outside:
a small launcher runs before each Apple Music process, loads a library (the kernel) into it, and lets it continue. The
kernel then changes XAML properties while the app runs and runs JavaScript extensions. Every change lives in memory,
and removing a theme puts each value back without a restart.

| Piece | Role | Source |
|---|---|---|
| `ampify_launcher.exe` | Loads the kernel into each Apple Music process, or starts Apple Music itself (`ampify launch`) | `src/launcher/` |
| `ampify_kernel.dll` | The library loaded into both Apple Music processes | `src/kernel/`, `src/core/`, `src/win/` |
| `ampify.exe` | The command line | `src/cli/` |
| `ampify_setup.exe` | Installer and uninstaller | `src/setup/` |

## The two Apple Music processes

| Process | What it does |
|---|---|
| `AppleMusic.exe` | The interface (XAML). Also holds V8, `JetEngine32.dll` (store pages) and WebView2 |
| `AMPLibraryAgent.exe` | The engine: library, playback, downloads, DRM, with Apple's ported frameworks. Starts at the first playback and outlives the app |

They talk through out-of-process COM (`AMP.Core.IAMPMusicLibrary`). The kernel loads in both, and each element runs in
the processes its manifest names (`processes`, `ui` by default). More in
[knowledge/apple-music-internals.md](../../knowledge/apple-music-internals.md).

## The launcher

Nothing of Ampify runs while Apple Music is closed. `ampify_launcher.exe --register` asks Windows, through
`IPackageDebugSettings::EnableDebugging` (per user, no administrator), to start `ampify_launcher.exe --attach -p <pid>
-tid <tid>` for each new process of the Apple Music package. Windows creates the process suspended, and the launcher:

1. checks the package version and the exports of `AVFoundationCF.dll`;
2. for `AppleMusic.exe`, loads the kernel, then resumes the process;
3. for `AMPLibraryAgent.exe`, resumes it first and loads the kernel 8 seconds later, since a kernel in the agent while
   it opens the library stops the library from loading;
4. appends to `logs\attach.log` and exits.

The setting lives under `HKCU\Software\Classes\PackagesDebug\<package full name>` and belongs to one package version,
so after an Apple Music update the next `--register` (at sign-in, from the Run key) sets it again. If the launcher it
names is missing, Windows cannot start Apple Music at all, so the uninstaller and `ampify restore` remove it first
(`DisableAttach`). When the setting cannot be made, `--register` falls back to the resident launcher.

The resident launcher (`--background`, one per session, mutex `Local\AmpifyBackground`) watches for `AppleMusic.exe`
four times a second. For each new process it checks the version and exports, suspends the threads, calls
`CreateRemoteThread` with `LoadLibraryW` on `ampify_kernel.dll`, resumes, and follows the app until it closes. It also
loads the kernel into `AMPLibraryAgent.exe` and any second `AppleMusic.exe`. Once the app has run for 30 seconds and
reported its health, it looks every two seconds.

`ampify restore` sets the event `Local\AmpifyBackgroundStop` and removes the sign-in entry; `ampify apply` puts both
back. `ampify launch` runs the same launcher in the foreground and starts Apple Music through its package identity
(with `app_launch_flags`). If the background launcher runs, the foreground one leaves the injection to it.

At each sign-in, `--register` also runs `ampify update --auto` once (the resident launcher runs it while Apple Music is
closed: three minutes after sign-in, a minute after Apple Music quits, then every hour). Output goes to
`logs\update.log`. The command reads `kit_update`, checks the latest GitHub release at most once a day
(`cache\auto-update.txt`), verifies the zip against `SHA256SUMS.txt`, asks first when `kit_update = ask`, renames the
running files to `*.old`, copies the new ones and restarts the background launcher. Only the installed copy updates
itself.

A proxy DLL is impossible on a signed, read-only package. The injection method is the one
[AMExclusive](https://github.com/AntiOblivionis/AMExclusive) uses.

## Version gate and safe mode

The launcher and kernel load only on Apple Music versions listed in `facts/apple.json` (plus `versions.txt` from
`ampify versions update`), and only if `AVFoundationCF.dll` exports the names listed there. The kernel stores no
addresses. A version counts as tested once the bench injects cleanly on it and its export signature is recognized. The
bench build (`AMPIFY_BENCH=ON`) skips the version list.

The kernel marks a launch healthy once the app has run for 8 seconds with its window responding. After two launches in
a row that never got there, the launcher starts Apple Music without the kernel (`src/core/safe_mode.*`, counter
`unhealthy_launches` in `state.txt`).

## The kernel

On load, the kernel reads `%USERPROFILE%\Ampify\settings.txt`, opens its log (`logs\AppleMusic.log` or
`logs\AMPLibraryAgent.log`), checks safe mode and the version gate, then starts the JavaScript host and the services
that are on. In `AppleMusic.exe` it starts the interface service (window, theme engine, panels). In
`AMPLibraryAgent.exe` it installs the audio hooks (`src/win/audio/`) when the Atmos leveler is on or the interface asks
for sound.

Every 500 ms the kernel re-reads `settings.txt` and the new lines of `reload.txt` to start, stop or restart an element.
Every hook goes through `src/core/drm_guard.cpp`.

The data folder is `%USERPROFILE%\Ampify`, because MSIX redirects Apple Music's writes under AppData and the launcher
and kernel would see different files there.

Before anything connects, both processes install the network fixes that are on (`src/win/network_fix.cpp`), all by
import-table hooks and never in a protected module:

- `apple.network = ipv4-first` (default) puts IPv4 addresses first in every resolver answer.
- `apple.proxy = no-autodetect` (off by default) makes WinINet's `DetectAutoProxyUrl` answer "nothing found" at once.
  Apple's `CFNetwork.dll` calls it synchronously when Windows' "Automatically detect settings" is on.
- `apple.watchdog = on` logs lookups and proxy detections of 300 ms or more, and follows each process's memory in
  steps of 512 MB (`src/win/reliability_watch.cpp`).

Once per start, `AppleMusic.log` names any running program known to crash Apple Music (3Dconnexion's driver, RivaTuner)
with its fix; `ampify doctor` prints the same lines (`src/core/reliability.*`).

## The window and the UI thread

- `Window::Current()` is null in a desktop WinUI 3 app. The kernel calls the `AMP.Utils.dll` export
  `?GetAppProperty@SharedUtils@@YA?AUIInspectable@Foundation@Windows@winrt@@AEBUhstring@5@@Z` with `"MainWindow"` (or
  `"MiniPlayerWindow"`, `"FullScreenPlayerWindow"`), then `try_as<Window>()`. Before the window exists it fails with
  `0x8000000B` and the kernel tries again.
- All XAML work goes through `window.DispatcherQueue().TryEnqueue(...)`.
- Pages are followed with WinEvent hooks, a pass every 5 seconds, and the `Loaded`, `Unloaded` and `Content` events. A
  walk stops at 512 elements and skips item lists. Pages are recognized by class name (`PlaybackSettingsPage`).
- After `Window.Closed`, nothing touches XAML, and everything XAML is released on the UI thread.

The tree's shape is in [knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md). Pitfalls are in
[xaml-lessons.md](xaml-lessons.md).

## Panels, pages and the settings page

Extensions describe interfaces as trees of nodes, drawn with real WinUI controls (`src/win/ui_renderer*.cpp`, the node
model in `src/core/ui_render/`). Each node is named `AMPIFY_<element>_<node>`, and a new render changes only what
changed. Styles are copied from the native controls of Settings > Playback.

| Place | How it is shown |
|---|---|
| Panels | A 380 px column on the right, in a popup of the window (Apple Music removes unknown children from `Grid#PageRoot`) |
| Dialogs | A native dialog, one at a time |
| Full-screen view | A popup over the whole window, closed by its button or Escape |
| App pages | Over the content, with a Back arrow to the app's previous page (up to 16) or closing the page |
| Playback bar buttons | In `StackPanel#NowPlayingToggleButtons` |
| Ampify settings page | The kernel's page at `/ampify/settings`, drawn like an app page. The "Ampify" row after Advanced on the Settings page opens it, and so does the Marketplace's Settings button |
| Sidebar entries | A small panel in place of the sidebar's footer, since adding to Apple Music's own list crashed it |

The settings page (`src/win/ui_service_settings.cpp`) is built from panes, groups and rows (`SettingsNav`,
`src/win/ui_service_settings_nav.cpp`): one tab per pane, a search that looks through every row's label and help and
every element's settings at once (`settings_panel::MatchingSettings`), and each element's settings in a WinUI
`Expander` that stays open across rebuilds when it was left open. A new row is one `AddSettingsRow` call with its
text, so the search finds it. Everything that moves on Ampify's own surfaces uses `src/win/ui_motion.hpp` (`Enter`,
`Glide`, `FadeTo`): the same durations and easing, and the final state at once when Windows' animation effects are
off. The page takes its colours from theme resources (`AccentFillColorDefaultBrush`, the `Expander*` keys), so every
theme recolours it.

The page holds the extensions, the themes and their settings, and three choices for Apple Music itself, stored
in `settings.txt` and read within a second:

| Setting | What the kernel does |
|---|---|
| `apple.notifications` | Replaces the vtable entry of `AppNotificationManager.Show` and `ToastNotifier.Show` in `AMPLibraryAgent.exe`, which shows the song notifications, to drop them by the chosen rule |
| `apple.close` | `tray`: a subclass of the main window answers `WM_CLOSE` and hides it behind a notification-area icon (`src/win/close_to_tray.cpp`) |
| `apple.fullscreen` | Places Apple's full-screen player window (`src/win/apple_player.cpp`) |

## The theme engine

1. Sources: the active theme, each enabled snippet (alphabetical), then `sidebar.json` and `home.json`. Each brings
   `colors.ini`, `resources.xaml` and `user.css`.
2. Rules: a selector is a XAML type and an `x:Name`, with combinators and pseudo-classes. Stable names (`.playbar`) and
   `@import "ampify:<layout>"` are expanded first (`src/core/theme/builtins.cpp`). The layouts are plain CSS in
   `src/core/theme/layouts/` that `cmake/layouts.cmake` compiles into the kernel.
3. Cascade: `!important`, then source, then specificity, then place in the file. The Spicetify color translation is a
   lower layer that any `user.css` rule beats.
4. Apply: on the UI thread, the engine reads each original value and writes the new one. A pass runs when Apple Music
   shows something new and every 3 seconds. Rules are indexed by the name and type of their last part.
5. Restore: every value keeps its original and is restored in reverse order.

After a resource change the engine switches light and dark mode and back, since `{ThemeResource}` is only re-read at a
mode change. Popups get the root's `RequestedTheme` at each refresh. `transition` uses XAML's implicit transitions, and
`animation` becomes composition keyframe animations (`src/win/theme_animations.cpp`).

The engine is portable C++ in `src/core/theme/` and is tested on Linux. `src/win/theme_service.cpp` and
`src/win/xaml_backend.cpp` connect it to WinUI.

## The JavaScript runtime

[quickjs-ng](https://github.com/quickjs-ng/quickjs) is vendored unchanged in `vendor/quickjs/` (version in
`UPSTREAM.txt`). Each element gets its own instance and a global `Ampify` object. `Ampify.React` is a small engine with
React's shape that produces the same node tree. Native features stay in the kernel, because a public catalog cannot
vouch for a community DLL. Adding a module: [CONTRIBUTING.md](../../CONTRIBUTING.md#add-an-api-module).

## The Marketplace

The Marketplace (`apps/marketplace`, built with `Ampify.React`) reads `ampify market --json` and acts through
`Ampify.kit.run`, which runs `ampify.exe` with no window and empty input, so only `--yes` can approve. Only it holds
`kit.manage`, limited to `market`, `install`, `remove`, `update <id>`, `theme`, `extension`, `app`, `snippet` and `list`.

## Built-in extensions

- Now Playing (`extensions/now-playing`) calls `Ampify.ui.applePlayer.open()`. The kernel calls
  `AMP.UI.Views.FullScreenPlayer.Instance().Show()` by vtable slot (`src/core/amp_abi.hpp`), waits for the
  `FullScreenPlayerWindow`, gives it an overlapped presenter with a border and no title bar, and places it as
  `apple.fullscreen` says.
- The Song panel (`extensions/song-panel`) adds a playback bar button that slides in a column about the current song.
- The playback guard (`extensions/playback-guard`) works around an Apple Music 1.1792 bug: with Lossless Audio on, the
  first song often freezes at about 15 seconds. It reads Apple's player once a second. A song stuck above position 0
  for 3 seconds is restarted with "previous" and sought back to where it froze. After 6 more seconds without a restart
  it goes next then previous. A song is repaired twice at most.
- The Atmos leveler (`extensions/atmos-leveler/` decides, `src/win/audio/` hooks the samples) gives Atmos songs a gain
  on the decoded samples so they match stereo loudness. Stereo is measured on the shared-mode `IAudioRenderClient`
  stream (float32) after the player volume. Atmos is measured on `ISpatialAudioObjectRenderStreamForMetadata` (mono
  objects at 48 kHz) before the volume, and brought to the same scale. Loudness is BS.1770 per song, kept per audio
  variant in `atmos-levels.txt`. The target is the median of the latest stereo songs, with a capped gain, a soft knee
  and a peak ceiling (numbers in `extensions/atmos-leveler/leveler.hpp`; `audio.maxGainDb` bounds the gain extensions
  add on top). The leveler pauses while AMExclusive holds the output in exclusive mode. Measurements:
  [knowledge/atmos-loudness.md](../../knowledge/atmos-loudness.md).
