# Apple Music for Windows: what is known

Findings from package `AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa` (Windows 11 build 26200), then checked
again on `1.1792.39682.0`, the version of the Windows bench. What is marked "verified" was observed. The rest is inferred.
The Atmos measurements in [atmos-loudness.md](atmos-loudness.md) were taken on `1.1540.23042.0`.

## Package and processes

- The MSIX package is installed in `C:\Program Files\WindowsApps\AppleInc.AppleMusicWin_<version>_x64__nzyj5cx40ttqa\`.
  The folder is read-only and protected, so a proxy DLL cannot be placed there. Launch command:
  `explorer shell:AppsFolder\AppleInc.AppleMusicWin_nzyj5cx40ttqa!App`. The list of binaries is in
  [package-binaries.txt](package-binaries.txt).
- Two processes (verified):
  - `AppleMusic.exe`: the interface. **WinUI 3 / embedded Windows App SDK** (`Microsoft.UI.Xaml.dll` from the package),
    views and models in C++/WinRT: `AMP.UI.Views.dll`, `AMP.UI.Controls.dll`, `AMP.UI.ViewModels.dll`,
    `AMP.Services.dll`. It also contains V8 / `JavaScriptCoreV8.dll`, `JetEngine32.dll` (Apple's "Jet" engine, used for
    store pages) and WebView2.
  - `AMPLibraryAgent.exe`: library, playback, downloads, DRM. It loads Apple's ported frameworks: `CoreFoundation`,
    `Foundation`, `AVFoundationCF`, `CoreMedia`, `CoreAudioToolbox`, `CoreFP` (FairPlay), `CoreADI64`, `libdispatch`,
    `objc`.
- The interface talks to the engine through out-of-process COM (verified in `AppxManifest.xml`, see
  [manifest-com.txt](manifest-com.txt)): `com:ExeServer AMPLibraryAgent.exe`, class `AMP.Core.IAMPMusicLibrary`
  {68E7097C-F969-4006-AAC3-95115F0ED1C4}, proxy/stub `AMPLibraryAgent.Proxies.dll`. Interfaces: `IAMPLibrary`,
  `IAMPMusicLibrary`, delegates `PlaybackEventDelegate`, `LibraryEventDelegate`, and others. Commands mostly go through
  `MediaAppExecAgentCommandAsync` / `MediaAppExecFetchRequestAsync` (log: `kExecAgentCommand`). The engine starts at the
  first playback, or when that COM class is activated.
- **Full component metadata.** The package contains `AMP.Core.winmd` (engine) and `AMP.Services.winmd` (1,035 types:
  playback, queue, equalizer, lyrics, Atmos and lossless badges, settings...). Their content (types, methods, fields) is
  in `winmd/*.txt`. It is the map of the app: an interface or a mod can call these services instead of guessing.
- Protocols handled ([protocols.txt](protocols.txt)): `music:`, `musics:`, `itms:`, `itmss:`... A link
  `music://music.apple.com/...` opens the page in the app but does not start playback.
- A module scan of a running `AppleMusic.exe` showed about 250 modules, including `Microsoft.Web.WebView2.Core.dll`,
  `JavaScriptCoreV8.dll`, `Microsoft.UI.Xaml.dll`, `Microsoft.UI.Xaml.Controls.dll`, `Microsoft.UI.Xaml.Internal.dll` and
  `WinUIEdit.dll`. `AMPLibraryAgent.exe` loaded about 200 modules.
- Apple Music 1.1540 is aligned with Windows App SDK 1.8.

## Playback (engine `AMPLibraryAgent.exe`)

- Player: the C API `AVCFPlayer*` exported by `AVFoundationCF.dll` (list: [exports/AVFoundationCF.txt](exports/AVFoundationCF.txt)).
  Apple Music creates a new player for each playback context.
- App volume: `AVCFPlayerSetVolume(player, float)` (linear 0..1, the slider at 18% is about 0.178). The app reads the
  volume back with `AVCFPlayerGetVolume`.
- Atmos volume (Sound Check, the CoreMedia loudness mode, measurements, what works): see
  [atmos-loudness.md](atmos-loudness.md).
- Sound Check: the app calls `AVCFPlayerItemSetLoudnessInfo` ("adjust to album loudness") when items are created. **No
  level is applied to Atmos items**: measured 5 dB lower than the same song in stereo (stereo -31.6 LUFS, Atmos
  -36.6 LUFS at the output, over 0 to 90 s of one song).
- Effects: an audio tap (`play> effects> create tap`, `AVCFAudioMixInputParametersSetAudioTapProcessor`) feeds the built-in
  equalizer (`EqualizerViewModel` in AMP.Services).
- Reliable Atmos detection (verified): the CoreFoundation description of the item's track format. Atmos is
  `mediaSubType:'qec3'` (encrypted E-AC-3) with a `'qc+3'` layer of 16 channels. Stereo is `'qaac'` (AAC) or `'qlac'`
  (ALAC). `AVCFPlayerItemIsRenderingSpatialAudio` stays at 0 and is useless. A stable id per variant:
  `AVCFPlayerItemCopyCurrentStableVariantID` (a hash, stable from one session to the next).
- Audio output (verified by a Windhawk mod that hooks WASAPI in `AMPLibraryAgent.exe`):
  - **stereo:** `IAudioClient` / `IAudioRenderClient` in shared mode, float32, 2 channels, 96 kHz on the test output. Apple
    recreates the stream at each format change. The player volume is already applied in the samples: -15.95 LUFS is
    measured before volume for a song leveled by Sound Check, which is about Sound Check's target;
  - **Atmos:** **no** `IAudioRenderClient` stream. The sound goes through Windows' spatial audio API
    (`ISpatialAudioClient` -> `ISpatialAudioObjectRenderStream`, mono objects rendered by Dolby Atmos for Headphones). To
    act on Atmos sound, hook `ActivateSpatialAudioStream`, `Begin/EndUpdatingAudioObjects` and
    `ISpatialAudioObject::GetBuffer`;
  - the test output was a stereo USB headset at 96 kHz float, with Dolby Atmos for Headphones.
- **The interface shows the player volume.** A change through `AVCFPlayerSetVolume` moves Apple Music's slider (observed:
  55% in stereo, 71.67% in Atmos with +5 dB). The Atmos leveler reads that volume and applies its own gain to the samples.
- **Where the position of the song is (1.1792, measured 2026-11-05).** `INowPlayingItem.CurrentPosition` moves
  about once a second while the song is heard, and the SMTC timeline too (about a second behind it). Both stay at 0
  while the song loads (the first second) and for as long as its stream does not come, though Apple's state says
  playing: a first look at the position was made on a PC where every stream stalled (see "Songs stuck at 0:00"
  below) and concluded wrongly that the position never moves. So a clock must not count time Apple has not shown:
  `PositionClock` (`src/core/player_model.cpp`) counts on from the last value that changed, for two seconds at
  most. The lyrics view also tells the position: `KaraokeLyricsView` → `get_ViewModel` → `CurrentLine` →
  `StartTime`, the line Apple lights up (used when Apple's value is 0; slots in `src/core/amp_abi.hpp`). Apple's
  views are bound with `x:Bind`: `DataContext` is empty all the way up, and the models come from each view's own
  `get_ViewModel`. Identifiers and slots: read the `GuidAttribute` and the method order of the interface from the
  package's `.winmd` with `System.Reflection.Metadata` (`winmdidl` fails without its references).
- **Songs stuck at 0:00 (2026-11-05).** Apple's streams (`streamingaudio`, `aod`, `mvod.itunes.apple.com`) resolve
  first to an IPv6 address of Apple's own CDN (`2a0f:3400:…`, `*.aaplimg.com`) that some home connections cannot
  reach, and Apple Music never falls back to the IPv4 address that answers: every song stays at 0:00 while its
  lyrics and cover load. The songs are fetched by Windows' HTTP stack (`webio.dll` under WinHTTP, `WININET.dll`),
  whose Winsock imports are **delay-loaded**: an import hook that only walks the regular import table never sees
  them. Ampify puts the IPv4 addresses first in every resolver answer (`apple.network`, `src/win/network_fix.cpp`),
  regular and delay-loaded imports of every module, also those loaded later.

- **Lyrics without the pane (1.1792, slot read from the winmd).** Apple loads a song's lyrics only when its lyrics
  pane opens: until then `ILyricsInfo.HaveLyrics` is false. Its pane calls `IPlayer.GetNowPlayingLyricsAsync(LyricsSource)`
  (slot 49, an `IAsyncAction`; `LyricsSource` is Any, None, UserCustom, Store, StoreSyllable = 4). Ampify calls it with
  StoreSyllable at each new item (`src/win/amp_player.cpp`). `GetLyricsAsync(descriptor, StoreSyllable)` (slot 50)
  completes, but on 1.1792 it gave `timing="Line"` TTML for every library song tried (05/11/2026): Apple Music on
  Windows had no word timing for them. The karaoke then spreads a line's span over its words by length
  (`TimedWords`, `src/core/lyrics.cpp`), the parsed lyrics keep only what Apple gave.
- **A large cover.** The play bar's `Image#StaticArtwork` is a thumbnail. Apple's own loader is a static of
  `AMP.UI.Views.UIBroker` (`IUIBrokerStatics` AC46998B-…, slot 34 `GetImageSource(Artwork, Double scale, Double width,
  Double height) -> ImageSource`), given the item's `IArtwork`. Library covers sit on disk in
  `Apple Music Library.musiclibrary\artwork\<GUID>_sk1.jpeg`, mostly 600 px wide, some 800 or 1200.
- **Artist page and credits.** No WinRT call takes an artist or song id and returns a biography or credits: Apple's
  pages come from its JavaScript "Jet" stack (`JetEngine32.dll`), deserialized into models such as `ArtistBioHeader`
  and `ArtistBioInfo`. Reaching them means navigating Apple's own stack (`IServiceProvider.get_NavigationService`,
  `ArtistDetailPageDestination` built by an action, no public constructor). Not done; songwriters are in the TTML.
- **Running one of Apple's actions (1.1792, checked with a real click on 2026-11-05).** A view model's `...Action`
  properties (`INowPlayingViewModel.GoToNowPlayingItemAction`, `ScrubForwardAction`...) return an object with no
  runtime class name, not an `ICommand`: `Execute` is out of reach. Apple runs them with the static
  `IUIBrokerStatics.GlobalDispatchActionAsync(Object) -> IAsyncAction` (slot 25), as its own menus do. The display's
  go-to action opens what the song plays from: its album, or the playlist of a library file, at that song
  (`src/win/lcd_links.cpp`). Posted mouse messages (`PostMessage WM_LBUTTONDOWN`) never reach WinUI's `Tapped`: a
  test of a click needs real input (`SendInput`, with the window in front).

## Pitfalls observed

- The package folder is read-only. Apple Music's writes to AppData and HKCU are redirected into
  `%LOCALAPPDATA%\Packages\AppleInc.AppleMusicWin_nzyj5cx40ttqa\LocalCache\` (MSIX virtualization).
- Logs: ETW in `...\LocalState\Logs\*.etl`, readable with `tracerpt <file> -o x.csv -of CSV`. The active file is empty until
  it has run (one rotation per launch).
- Preferences: `...\LocalCache\Roaming\Apple Computer\Preferences\AMPLibraryAgent.exe.plist` (binary plist; `volumeWSG`,
  `losslessEnabled`, `downloadDolbyAtmos`, `crossfadeSeconds`...).
- Injection: Apple Music accepts a DLL injected at process start (Windhawk works too), and a hot reload. Nothing points
  to anti-debugging detection.
- After a restart, the play queue is empty: playback can only resume by a click from the user. The Windows media commands
  (SMTC) handle play, pause, next and previous, but not choosing a song or a position.
- A freeze of about 7 s around 15 s into the first Atmos song after a launch happens with no mod at all (a FairPlay key
  request times out at 20 s, then the retry succeeds). It is not a bug to blame on mods.
- First launch: an MPEG LA licence (buttons Agree, Decline, Save As) opens before the main window, then, after sign-in, a
  "Welcome to Apple Music" screen (Start Listening). While they are open there is no XAML window.
- Cover and lyrics of the playing item (read from the 1.1792 metadata, `src/core/amp_abi.hpp`): `INowPlayingItem.get_Artwork`
  (slot 11) gives an `Artwork`, queried as `IArtwork`: `get_Uri` is a `Windows.Foundation.Uri` whose absolute address is
  probably an `mzstatic.com` template with `{w}`, `{h}`, `{c}` and `{f}`; `get_BGColor` is a `Windows.UI.Color` (A, R, G, B).
  `get_LyricsInfo` (slot 46) gives `ILyricsInfo`: `HaveLyrics`, `GetTTML` (timed lines and words), `GetStaticLyrics`,
  `GetLanguage`. Lyrics may come some seconds after the track starts. None of this is confirmed on a real Apple Music yet.
- Versions: `1.1540.23042.0` and `1.1792.39682.0` are both in the tested list. In 1.1792 the interfaces of
  `AMP.Services.Player` changed ("IPlayer absent" in the log), so the player read falls back to SMTC.
- Windows media session (SMTC), seen in 1.1792: the artist field holds "Artist — Album" and the album field is empty, so
  the player model splits it. The length often arrives a poll after the track change. The thumbnail is the cover as a
  JPEG of about 40 to 170 KB; its content type reads "image/jpeg,image/jpe,image/jpg", keep the first entry.
- Closing the main window (or `taskkill` without `/F`) only hides Apple Music in the notification area: the process
  keeps the kernel loaded and blocks a rebuild. Stop it with `Stop-Process -Force` before linking.
- Changing an extension setting with `ampify config` does not restart the extension: `ampify extension disable <id>`
  then `enable <id>` does.

## Top bar: search and back in the title strip (`apple.topbar`, 2026-11-06)

- Apple Music's history goes back only. The 1.1792 metadata (`knowledge/winmd/AMP.Services.txt`) has `GoBack` and
  `CanGoBack` on `ISidebarViewModel`, `IBackNavigationHost` (with `ClearBackStack`, `AddToBackStack`) and the web view
  host, and `PushNavigationStack` / `PopNavigationStack` on `INavigationService`; no `GoForward`, `CanGoForward` or
  forward stack anywhere. A forward arrow would need a history of Ampify's own that does not see Apple's own
  navigations, so the strip has none.
- No Apple method is called, and no vtable slot was needed. The field is Apple's own: the NavigationView's
  `ContentControl#PaneAutoSuggestBoxPresenter` moves from `Grid#AutoSuggestArea` into a cell of Ampify's strip (a Popup
  of the window), so the `AutoSuggestBox` inside keeps its handlers (text, suggestions, submit); the cell takes the data
  context the presenter inherited. The NavigationView binds `Content` on the same presenter object, wherever it is.
  Its `ClosedCompact` state (sidebar folded and closed) collapses that presenter and shows `PaneAutoSuggestButton`;
  Apple's own template (`AMP.UI.Controls.pri`) also shows `TogglePaneButton`, the hamburger, and sets
  `PaneHeaderGrid.Height` there, so `NotClosedCompact` is not an empty state: holding the NavigationView in it hid the
  folded sidebar's hamburger. While the field is in the strip Ampify takes only the presenter's `Visibility` setter out
  of `ClosedCompact` (after leaving the state, so WinUI undoes the collapse) and puts it back on release, entering the
  state again from `NotClosedCompact`. Holding `NotClosedCompact` stays as a fallback if the setter cannot be found or
  the collection refuses the change. The arrow is Ampify's button; a click invokes Apple's
  `Button#NavigationViewBackButton` through its automation peer (as a click on it would), and Apple's arrow stays in
  place with opacity 0.
- The field and the arrow centre on the caption buttons, whose height is the AppWindow title bar's (`TitleBar.Height`,
  device pixels; 32 at 100%), not on the 40-pixel strip: centred on the strip they sat 5 to 6 pixels low at 125%.
- Apple's own layout and a theme's top bar put `Grid#TransportBar` in the strip: the strip shows only when the bar
  starts lower than 40 pixels. The window's passthrough regions (`InputNonClientPointerSource`) are Apple's plus
  Ampify's, merged every quarter second, since Apple sets its own again as its layout changes.

## Reliability: freezes, stuck library, error 9039 (research and code audit, 2026-11-05)

Source: an online survey (about thirty pages: Apple Community, Microsoft Q&A, GitHub AMWin-RP, press). Dates are those of
the pages. Not verified on a real Apple Music. Ranked by how often users report it and whether Ampify can act on it:

1. **Slow interface, freezes, "Loading Library" that never ends** (very frequent, last seen May-June 2026). Probable
   causes: WinUI 3 with many elements, web views for some pages, network waits. Ampify can only add its own load
   (below). Not mitigated.
2. **Playback stuck at 0:00 or without sound** (AMPLibraryAgent waits on the network or the audio device). The
   IPv6 part is already fixed (`apple.network`). The rest is inside Apple's agent, out of the kernel's reach.
3. **High CPU and memory** (the agent, the audio engine, WinUI with many elements). Not mitigated.
4. **Freeze with the lyrics or the queue open** (AMWin-RP #193, reported 2025, still open in May 2026). A maintainer
   suspects UI Automation tree searches block the app. This is the one case where Ampify's own code can be the cause:
   see "UI-thread audit" below.
5. **Slow start, first song blocked**: already described above (the 15 s block, Apple's bug).
6. **Lyrics** (black background, sync, word timing): Apple announced word-by-word lyrics for Windows in its
   1792 build (October 2026, press report, not checked on screen).
7. **Scroll at about 30 frames a second** on WinUI 3 scroll bars (microsoft-ui-xaml #5850). A framework limit.
8. **Error 9039 / error -50, library does not open**: no official fix. The cases found involve sign-in, permissions,
   AirPlay, or a large external library. Diagnostic only; Ampify never deletes the library.
9. **Output does not follow the default device change**: reports from 2024. Apple has not confirmed a fix, so this stays
   open until it is seen on screen.
10. **Dolby Atmos not offered** (Dolby Access): Apple and Dolby's problem, out of Ampify's scope (Atmos is never touched).
11. **iPhone sync and playlist folders**: Apple Devices, out of scope.

### Freeze watch (`apple.watchdog`, off by default)

- The kernel queues a heartbeat to the interface thread about every 200 ms (`PostToUiThread`, one at a time) and a
  monitor thread looks every 100 ms. Each beat is stamped when queued: the wait until it runs is the block, without
  the beat period. A wait of 300 ms or more is a stall. It is logged when the beat runs, as
  `UI thread stalled N ms, while: <what Ampify was doing>`, and a line each minute gives the beats, the worst delay and
  the number of stalls.
- What Ampify was doing comes from `ActivityScope` markers (`src/core/ui_watchdog.hpp`): "theme tick" (the posted
  tick), "theme pass after a change" (`Reapply` on each XAML change), "inspector tree", "element picker", "player
  buttons read", "player buttons press", "player volume" and "player command" (every `PlayerBridge` call run on the
  interface thread), and since 2026-11-06 the four tree searches of the audit below: "player buttons tree search",
  "full-screen player tree search", "player display tree search" and "sharp artwork tree search". Add a marker to any
  other interface-thread path that should be named.
- The same setting times name lookups and the automatic proxy detection (`Network: ... took N ms`, with "on the
  interface thread" when the window waited) and follows each process's private memory (`Memory: <process> uses N MB`,
  then `grew to` at each new step of 512 MB). See "Where Apple Music waits" below.
- A beat is queued only when the previous one ran (`BeatGate`), so a frozen interface thread queues one closure, not many.
- Cost: about 5 queued calls a second and one thread waking every 100 ms. Not measured here: the coordinator must
  measure CPU with the setting on and off.
- Logic and tests: `src/core/ui_watchdog.*`, `tests/ui_watchdog_tests.cpp`. Thread: `src/win/ui_watchdog.*`.

### UI-thread audit (static, not measured)

Work that runs on the interface thread and could block it for long, found by reading the code:

- `ThemeService` `WatchChanges` → `Reapply` on every change, and every element WinUI adds when `xaml.watcher` is on. The
  pass is already timed in the log ("Themes: pass after a change, N ms"). Measure with the freeze log before changing it.
- Visual-tree breadth-first searches by name, run on each player or lyrics event: `FindNamed` in `src/win/player_controls.cpp`,
  `src/win/apple_player.cpp`, `src/win/lcd_links.cpp` and `src/win/sharp_artwork.cpp`. Each one walks the whole subtree
  under the root it is given. This is the same kind of work as item 4 above. Next step: cache the found element per
  root (it is recreated only when the template changes) and measure. Each search now names itself in the freeze log;
  the cache waits for a stall line that names one of them.
- `element_picker.cpp` (inspector) hit-tests on the interface thread only when asked to pick.

### Stuck library agent (`apple.orphanAgent`, off by default)

- A killed Apple Music can leave `AMPLibraryAgent.exe` running. It holds the library, so Apple Music cannot open it
  (error 9039). Detection: an agent of the package runs and no `AppleMusic.exe` of the package runs.
- The launcher waits 10 s (`kOrphanGraceMs`) for the agent to leave, then ends it with `TerminateProcess`, on a
  handle whose image path it checks again first (an id can go to another process), and waits up to 2 s for it to be
  gone. A refused end is tried once per process id. It never ends an agent while Apple Music runs. It looks at launch
  (`ampify_launcher.exe`) and in the background launcher while Apple Music is closed. When Windows starts the launcher
  with each process (`--attach`), no background launcher runs, so only a launch through `ampify_launcher.exe` clears
  the agent: the attach path cannot, since it runs while an `AppleMusic.exe` is already there. A Start-menu launch of
  Apple Music itself is not covered.
- Logic and tests: `src/core/orphan_agent.*`, `tests/orphan_agent_tests.cpp`. Glue: `src/launcher/launcher.cpp`.

### Mitigations not implemented

Item 2 and item 3 would need the Apple Music process to be watched while it plays, and their effect verified on
screen. Neither was possible in this lane, so no kernel mitigation was added beyond the two settings above. Candidate
for later: a playback check (position still at 0:00 for N seconds while Apple reports playing) that offers to restart
the agent, after the freeze log shows the problem on real sessions.

### Where Apple Music waits (binaries and Apple's own logs, 2026-11-06)

Apple Music 1.1792.39682.0, read from files only (import tables, strings, a decompiler on `CFNetwork.dll`); CoreFP,
CoreADI64 and FairPlay were not opened.

- `AppleMusic.exe` is a shell (46 imported DLLs, no network, database or image decoding). The work is in
  `AMP.Services.dll`, `AMP.UI.Views.dll` and `AMPLibraryAgent.exe`.
- Apple's network library is `CFNetwork.dll` (Apple's CFNetwork ported to Windows) on raw Winsock (`getaddrinfo`,
  `gethostbyname`, `select`), SQLite for its cache and WinINet for the proxy. `AMP.Services.dll` also calls Winsock
  directly. Songs stream through WinHTTP in `CoreMedia.dll`, which is why the IPv4 fix also covers delay-loaded imports.
- **Synchronous WPAD.** A function of `CFNetwork.dll` reads Windows' proxy settings (`InternetQueryOptionA`, option
  0x4B) and, when "Automatically detect settings" is on, calls WinINet's `DetectAutoProxyUrl(buffer, 0x2000, 3)`
  (DHCP and DNS), which returns only once the network has answered. Who calls that function, how often and on which
  thread was not found (indirect call). The same file can run a PAC script synchronously ("Executing PAC file
  synchronously"). Ampify can time it and skip it (`apple.proxy = no-autodetect`, off by default: a company network that
  relies on WPAD would lose its proxy).
- `CoreMedia.dll` and `AVFoundationCF.dll` import blocking waits (`dispatch_semaphore_wait`, `dispatch_group_wait`,
  `dispatch_sync_f`); their thread is unknown. The agent has "SyncDispatchActionToMainThread" waits on its own main thread.
- Apple watches its agent itself: `AMPMediaAppClient` has `get_AgentIsHung`, `get_AgentPhase`, `get_AgentStatus` and
  `KillAgent` (`knowledge/winmd/AMP.Services.txt`). Not read by Ampify yet: first check the object is reachable from
  the kernel.
- Apple's ETW logs (`...\LocalState\Logs\SavedLogs\*.etl`, nine launches on one PC, possibly with Ampify loaded): the
  159 `ExecAgentCommandSync` calls and the 103 library reads never ran on the main thread (slowest command "play or
  queue", median 525 ms; library reads median 6 ms, at most 227 ms for 1763 songs). On the main thread,
  `DispatchIntentAsync` (a request to Apple's "Jet" JavaScript stack that builds the pages) took a median of 164 ms,
  12 of 200 over 1 s, at most 5 s; `ContentPageViewModel::TryLoadAsync` a median of 1.1 s. Those cover asynchronous
  work: they say how long a page takes to arrive, not that the window froze. Only the freeze log measures that.
- At start: `RunLibraryAgentStartUp` 402 to 453 ms, the agent's first iCloud sync up to 1.1 s, `wWinMain` 73 ms median.

Not to touch: the FairPlay key request behind the first song's freeze at about 15 s (CoreFP, forbidden; the playback
guard is the only workaround); the threads or waits of Apple's code (`dispatch_semaphore_wait`,
`SyncDispatchActionToMainThread`), without proof that they block; the library database and `ExecAgentCommandSync`,
which are not slow here; Jet's slow pages (Apple's server and JavaScript engine).

### Programs known to crash Apple Music

Reports on Apple's forums (2023 and 2024): 3Dconnexion's 3DxWare driver makes Apple Music vanish as soon as it starts
(fixed by adding `<ExecutableName>AppleMusic.exe</ExecutableName>` to
`C:\Program Files\3Dconnexion\3DxWare\3DxWinCore\Cfg\IgnoredApps.xml`); MSI Afterburner's on-screen display and
RivaTuner Statistics Server crash it (fixed by turning the overlay off for `AppleMusic.exe`). Ampify cannot repair
those from inside: `AppleMusic.log` names them once per start, and `ampify doctor` too, with the fix
(`src/core/reliability.*`). Afterburner alone is not named: its overlay is RivaTuner's (`RTSS.exe`), and without it
Afterburner draws nothing in other programs. `ampify doctor` also lists Apple Music crash dumps of the last 7 days
(`%LOCALAPPDATA%\CrashDumps`) and a library agent running without Apple Music.
