# How Ampify works

## Overview

Apple Music for Windows is a native WinUI 3 app packaged as MSIX, with read-only files. Ampify works from the outside:

1. A small launcher starts with Windows and waits for Apple Music. You open Apple Music from its own icon.
2. As Apple Music starts, the launcher pauses it, loads a library (the kernel) into it, and lets it continue.
3. Inside, the kernel changes the interface's XAML properties while it runs, and runs JavaScript extensions.

Every change lives in memory. Removing a theme puts each value back, without a restart.

| Piece | Role | Source |
|---|---|---|
| `ampify_launcher.exe` | Waits for Apple Music from sign-in, pauses it, injects the kernel, resumes it. Or starts Apple Music itself (`ampify launch`) | `src/launcher/`, `src/win/background.cpp`, `src/win/package.cpp` |
| `ampify_kernel.dll` | The library loaded into both Apple Music processes | `src/kernel/`, `src/core/`, `src/win/` |
| `ampify.exe` | The command line | `src/cli/` |
| `ampify_setup.exe` | Installer and uninstaller | `src/setup/` |

## The two Apple Music processes

| Process | What it does |
|---|---|
| `AppleMusic.exe` | The interface (XAML). Also holds V8, `JetEngine32.dll` (store pages) and WebView2 |
| `AMPLibraryAgent.exe` | The engine: library, playback, downloads, DRM, with Apple's ported frameworks (`CoreFoundation`, `AVFoundationCF`, `CoreMedia`...). It starts at the first playback |

The interface talks to the engine through out-of-process COM (`AMP.Core.IAMPMusicLibrary`). The kernel loads in both,
and each element runs in the processes its manifest asks for (`processes`, `ui` by default). More:
[knowledge/apple-music-internals.md](../../knowledge/apple-music-internals.md).

## The launcher

Nothing of Ampify runs while Apple Music is closed. `ampify_launcher.exe --register` asks Windows, through
`IPackageDebugSettings::EnableDebugging` (per user, no administrator), to hand each new process of the Apple Music
package to `ampify_launcher.exe --attach -p <pid> -tid <tid>`. Windows creates the process suspended and starts the
launcher, which:

1. checks the package version and the exports of `AVFoundationCF.dll`, as below;
2. for `AppleMusic.exe`, loads the kernel before the process runs, then resumes it;
3. for `AMPLibraryAgent.exe`, resumes it first and loads the kernel 8 s later (`WatchConfig::agentSettleAfter`): a
   kernel in the agent while it opens the library stops the library from loading, and the agent outlives the app;
4. writes `logs\attach.log` and exits.

The setting is stored under `HKCU\Software\Classes\PackagesDebug\<package full name>` and belongs to one version of the
package: after an update of Apple Music, the next `--register` (at sign-in, from the Run key) sets it again. If the
launcher it names is missing, Windows cannot start Apple Music at all, so the uninstaller and `ampify restore` remove
it first (`DisableAttach`). When the setting cannot be made, `--register` falls back to the resident launcher below.

The resident launcher (`--background`, one per session, mutex `Local\AmpifyBackground`) looks for `AppleMusic.exe`
four times a second. When one appears it:

1. reads the package version and checks the exports of `AVFoundationCF.dll`;
2. suspends the new process's threads;
3. calls `CreateRemoteThread` with `LoadLibraryW` on `ampify_kernel.dll`;
4. resumes the process, waits for the load thread, follows the app until it closes, and loads the kernel into
   `AMPLibraryAgent.exe` and any second `AppleMusic.exe` too. It looks four times a second while the app starts, then
   every two seconds once it has run for 30 s (and reported its health), so that an open Apple Music costs the
   launcher a few milliseconds of processor a second.

`ampify restore` sets the event `Local\AmpifyBackgroundStop` and removes the sign-in entry; `ampify apply` puts both
back. `ampify launch` runs the same launcher in the foreground and starts Apple Music through its package identity
(with `app_launch_flags`). When the background launcher runs, the foreground one leaves the injection to it.

While Apple Music is closed, the background launcher also runs `ampify update --auto` three minutes after sign-in, a
minute after Apple Music quits, then every hour, with its output appended to `logs\update.log`. `ampify` reads `kit_update`, looks at the latest GitHub release
at most once a day (`cache\auto-update.txt`), and for a newer one downloads the zip, checks it against
`SHA256SUMS.txt`, asks first when `kit_update = ask`, ends a leftover `AMPLibraryAgent.exe`, renames the running files
to `*.old`, copies the new ones and restarts the background launcher from them. Only the installed copy updates
itself: a build folder never does.

A proxy DLL is not possible on a signed, read-only package; the injection method is the one
[AMExclusive](https://github.com/AntiOblivionis/AMExclusive) uses.

## Version gate and safe mode

The launcher and kernel load only on Apple Music versions in `facts/apple.json` (plus `versions.txt` from
`ampify versions update`), and only if `AVFoundationCF.dll` exports the names listed there too. The
kernel stores no addresses. A version counts as tested once the bench injects cleanly on it and its export signature is
recognized. The bench build (`AMPIFY_BENCH=ON`) skips the version list.

The kernel marks a launch healthy once the app has run for 8 seconds with its window responding. After two launches
in a row that never got there, the launcher starts Apple Music without the kernel (`src/core/safe_mode.*`, counter in `state.txt`).

## The kernel

When it loads, the kernel reads `%USERPROFILE%\Ampify\settings.txt`, opens its log (`logs\AppleMusic.log` or
`logs\AMPLibraryAgent.log`), checks safe mode and the version gate, then starts the JavaScript host and the services
that are switched on. In `AppleMusic.exe` it starts the interface service (window, theme engine, panels); in
`AMPLibraryAgent.exe` it installs the audio hooks when the Atmos leveler is on.

A running kernel re-reads `settings.txt` and reads new lines of `reload.txt` every 500 ms to start, stop or restart an
element. Every hook goes through `src/core/drm_guard.cpp`.

Before anything connects, both processes install the network fixes that are on (`src/win/network_fix.cpp`), all by
import-table hooks, never in a protected module and never by patching code: `apple.network = ipv4-first` (the
default) puts IPv4 addresses first in every resolver answer; `apple.proxy = no-autodetect` (off by default) makes
WinINet's `DetectAutoProxyUrl`, which Apple's `CFNetwork.dll` calls synchronously when Windows' "Automatically detect
settings" is on, answer "nothing found" at once; `apple.watchdog = on` times the lookups and the proxy detection and
logs those of 300 ms or more, and follows each process's memory in steps of 512 MB (`src/win/reliability_watch.cpp`).
Once per start, `AppleMusic.log` names any running program known to crash Apple Music (3Dconnexion's driver, and
RivaTuner, which draws MSI Afterburner's on-screen display) with its fix; `ampify doctor` gives the same lines. The logic is portable and tested
(`src/core/reliability.*`). What Apple's own code does here: [knowledge/apple-music-internals.md](../../knowledge/apple-music-internals.md),
"Where Apple Music waits".

The data folder is `%USERPROFILE%\Ampify`, not `%LOCALAPPDATA%`: MSIX redirects Apple Music's writes under AppData,
so the launcher and the kernel would not see the same files there.

## The window and the UI thread

- `Window::Current()` is null in a desktop WinUI 3 app. The kernel calls the `AMP.Utils.dll` export
  `?GetAppProperty@SharedUtils@@YA?AUIInspectable@Foundation@Windows@winrt@@AEBUhstring@5@@Z` with `"MainWindow"` (or
  `"MiniPlayerWindow"`, `"FullScreenPlayerWindow"`), then `try_as<Window>()`. Before the window exists it fails with
  `0x8000000B`, and the kernel tries again.
- All XAML work goes through `window.DispatcherQueue().TryEnqueue(...)`.
- Pages are followed with WinEvent hooks, a 5-second pass, and the `Loaded`, `Unloaded` and `Content` events. A walk
  stops at 512 elements and never enters item lists. Pages are recognized by class name (`PlaybackSettingsPage`).
- After `Window.Closed`, nothing touches XAML. Everything XAML is released on the UI thread.

The tree's shape: [knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md). Pitfalls:
[xaml-lessons.md](xaml-lessons.md).

## Panels, pages and the Ampify settings page

Extensions describe interfaces as trees of nodes, drawn with real WinUI controls (`src/win/ui_renderer.cpp`). Each
node is named `AMPIFY_<element>_<node>`, and a new render only changes what changed. Styles are copied from the
native controls of Settings > Playback.

| Place | How it is shown |
|---|---|
| Panels | A 380 px column on the right, in a popup of the window (Apple Music removes unknown children from `Grid#PageRoot`) |
| Dialogs | A native dialog, one at a time |
| Full-screen view | A popup over the whole window, closed by its button or Escape |
| App pages | Over the content, with a Back arrow that goes to the app's previous page (up to 16), or closes the page |
| Playback bar buttons | In `StackPanel#NowPlayingToggleButtons` |
| Ampify settings page | The kernel's own page at `/ampify/settings`, drawn like an app page. The "Ampify" row after Advanced on the Settings page opens it, and so does the Marketplace's Settings button. Its styles come from Settings > Playback, read once when Apple Music shows that page |
| Sidebar entries | A small panel in place of the sidebar's footer, since adding to Apple Music's own list crashed it |

The card holds the extensions, the themes and their settings, and three choices for Apple Music itself, stored in
`settings.txt` and read within a second:

| Setting | What the kernel does |
|---|---|
| `apple.notifications` | Replaces the vtable entry of `AppNotificationManager.Show` (`src/win/notifications.cpp`) and of `ToastNotifier.Show` in `AMPLibraryAgent.exe`, which shows the song notifications (`src/win/toast_hold.cpp`), to drop notifications by the chosen rule |
| `apple.close` | `tray`: a subclass of the main window answers `WM_CLOSE` and hides it behind a notification-area icon (`src/win/close_to_tray.cpp`) |
| `apple.fullscreen` | Places Apple's full-screen player window (`src/win/apple_player.cpp`) |

## The theme engine

1. Sources: the active theme, each enabled snippet (alphabetical), then `sidebar.json` and `home.json`. Each brings
   `colors.ini`, `resources.xaml` and `user.css`.
2. Rules: a selector is a XAML type and an `x:Name`, with combinators and pseudo-classes. Stable names (`.playbar`)
   and `@import "ampify:<layout>"` are expanded first (`src/core/theme/builtins.cpp`).
3. Cascade: `!important`, then source, then specificity, then place in the file. The Spicetify color translation is a
   lower layer that any `user.css` rule beats.
4. Apply: on the UI thread, the engine reads each original value and writes the new one. A pass runs when Apple Music
   shows something new and every 3 seconds. Rules are indexed by the name and type of their last part.
5. Restore: every value keeps its original and is restored in reverse order.

After a resource change the engine switches the light and dark mode and back, since `{ThemeResource}` is only re-read
at a mode change. Popups get the root's `RequestedTheme` at each refresh. `transition` uses XAML's implicit transitions,
and `animation` becomes composition keyframe animations (`src/win/theme_animations.cpp`).

The engine is portable C++ in `src/core/theme/`, tested on Linux; `src/win/theme_service.cpp` and
`src/win/xaml_backend.cpp` connect it to WinUI.

## The JavaScript runtime

[quickjs-ng](https://github.com/quickjs-ng/quickjs) is vendored without changes in `vendor/quickjs/` (version in
`UPSTREAM.txt`). Each element gets its own instance and a global `Ampify` object. `Ampify.React` is a small engine
with React's shape that produces the same node tree. Native features stay in the kernel; a public catalog cannot vouch
for a community DLL. Adding a module: [CONTRIBUTING.md](../../CONTRIBUTING.md#add-an-api-module).

## The Marketplace

The Marketplace is an app (`apps/marketplace`, built with `Ampify.React`). It reads `ampify market --json` and acts
through `Ampify.kit.run`, which runs `ampify.exe` with no window and empty input, so only `--yes` can approve. Only the
Marketplace holds `kit.manage`, and its commands are limited to `market`, `install`, `remove`, `update <id>`, `theme`,
`extension`, `app`, `snippet` and `list`. Installs therefore go through the same checks as the command line.

## Built-in extensions

Now Playing (`extensions/now-playing`) calls `Ampify.ui.applePlayer.open()`. The kernel calls
`AMP.UI.Views.FullScreenPlayer.Instance().Show()` by vtable slot (`src/core/amp_abi.hpp`), waits for the
`FullScreenPlayerWindow`, gives it an overlapped presenter with a border and no title bar, and places it as the
`apple.fullscreen` setting says. A press and drag on its background moves the window.

The playback guard (`extensions/playback-guard`) works around an Apple Music bug: with Lossless Audio on, Apple Music
1.1792 often freezes the first song after it opens at about 15 seconds. The guard reads Apple's player once a second.
A song stuck above position 0 for 3 seconds is restarted with "previous", then the guard seeks back to where it froze.
If the song has not restarted after 6 seconds, it goes next then previous. A song is repaired twice at most.

The Atmos leveler brings Dolby Atmos songs to the loudness of stereo songs with a gain on the decoded samples. Sound
Check leaves Atmos items alone, and CoreMedia's own Atmos mode leaves them about 4 dB below stereo.

- Stereo is measured on the shared-mode `IAudioRenderClient` stream (float32), after the player volume.
- Atmos is measured on `ISpatialAudioObjectRenderStreamForMetadata` (mono objects at 48 kHz), before the volume, and
  brought to the same scale.
- Loudness is BS.1770 per song, kept per audio variant in `atmos-levels.txt`. The target is the median of the last 30
  stereo songs; the gain goes from 0 to 15 dB with a soft knee from 0.94 and a ceiling of 0.95.
- While AMExclusive holds the output in exclusive mode, the leveler pauses.

Measurements: [knowledge/atmos-loudness.md](../../knowledge/atmos-loudness.md).
