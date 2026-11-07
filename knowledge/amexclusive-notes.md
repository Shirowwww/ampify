# What AMExclusive teaches us

[AMExclusive](https://github.com/AntiOblivionis/AMExclusive) (MIT) adds exclusive WASAPI output to Apple Music for
Windows. It is tested on `1.1540.23042.0`, and it adds settings to the Playback page of Apple Music's Settings. Read in
read-only mode at commit `91f589d`. The references are paths in its `mod/` folder.

## What works for it and what Ampify takes

- **The XAML window.** `Window::Current()` returns null in desktop WinUI 3. AMExclusive calls the export of `AMP.Utils.dll`
  `?GetAppProperty@SharedUtils@@YA?AUIInspectable@Foundation@Windows@winrt@@AEBUhstring@5@@Z`
  (`IInspectable(hstring const&)`) with `"MainWindow"` or `"MiniPlayerWindow"`, then `try_as<Window>()`
  (`src/UiAdapter.cpp:315-364`). An absent `MiniPlayerWindow` returns `E_BOUNDS`.
- **The UI thread.** `window.DispatcherQueue().TryEnqueue(Low, ...)` from an MTA thread, with one ticket per window against
  duplicates, and `HasThreadAccess()` on entry (`UiAdapter.cpp:836-856`, `:1259-1293`). No `WH_GETMESSAGE` hook (Ampify's
  own never ran on Windows Server 2025).
- **Discovery.** `SetWinEventHook(EVENT_OBJECT_CREATE..SHOW, WINEVENT_OUTOFCONTEXT)` on the process, a poll every 5 s, then
  `Loaded`/`Unloaded`/`Content` on Frame, Page and UserControl, `Tapped`/`KeyDown` with `handledEventsToo`, `LayoutUpdated`
  until mount, and a revision number that invalidates stale callbacks. Breadth-first walk limited to 512 nodes, without
  entering ItemsControl, ItemsRepeater or ButtonBase.
- **Pages are recognized by class:** `get_class_name` contains `PlaybackSettingsPage`. In the page, the `StackPanel` that
  holds the most `ToggleSwitch` elements serves as the anchor, and its last child serves as the row model.
- **Native look.** Copy the `Style`, margins and padding of neighboring controls; failing that, the resources
  `CardBackgroundFillColorDefaultBrush` and `CardStrokeColorDefaultBrush`. `AutomationProperties` are always set.
- **Idempotence.** Each added element carries a prefixed name. If it exists, it is refreshed instead of recreated.
  Everything is built before being inserted, with a rollback if an addition fails.
- **Real Apple Music names:** `TransportControl_PlayPauseStop`, `TransportControl_SkipBack`, `TransportControl_SkipForward`,
  `LCDScrubber` (`x:Name` or AutomationId), `Scrubber` (AutomationId), track rows as `ListViewItem`.
- **The player, read-only.** Private WinRT interfaces of `AMP.Services.Player`, declared by hand
  (`src/ApplePlayback.h:16-152`): `IPlayerStatics` {34D100F9-0F0A-5980-8F04-7AF57FDB29B0} `get_Current`, `IPlayer`,
  `IPlayerState`, `INowPlayingItem` (including `PlayQueueItemID` and the audio format), with `INotifyPropertyChanged` on
  `PlayerState`, `NowPlayingItem` and `PlayQueueItemID`.
- **Injection.** The same method as Ampify (`CreateRemoteThread` + `LoadLibraryW`), after checking the package identity
  (`GetPackagesByPackageFamily`, x64, family `AppleInc.AppleMusicWin_nzyj5cx40ttqa`). The DLL folder gets the ACL
  `S-1-15-2-1` (ALL APPLICATION PACKAGES) for read and execute. A companion starts through a scheduled task on the event
  `Microsoft-Windows-AppModel-Runtime/Admin` 201 (Apple Music launch), which also works when the user opens Apple Music
  without the shortcut.
- **WinUI metadata.** Apple Music 1.1540 is aligned with Windows App SDK 1.8. AMExclusive projects from
  `microsoft.windowsappsdk.winui` 1.8.251222000, with the SHA-256 of `Microsoft.UI.Xaml.winmd` pinned
  (`A824AB1E380E384C382CEC14AC5DAD9D4C1B91F519D512082F0E7E5D6AC109DE`, `scripts/msvc-winui.ps1`).

## What Ampify does not take

- **The quality lock.** It changes the selection of HLS variants, including the protected `qlac` subtype, in CoreMedia,
  through a raw address. It sits next to the protected stream path, which is outside Ampify's rules, whatever its intent.
- The CoreAudioToolbox hooks, the replacement of Apple's `IAudioClient`, the fake `IAudioStreamVolume`, and the global
  `CoCreateInstance` hook.

## Living with Ampify

With AMExclusive active, Apple writes to a software render client, and the real rendering goes out through an exclusive
WASAPI client in integer PCM. Ampify's Atmos leveler would no longer see the right stream and would break bit-perfect
output. Rule: if `am-exclusive-hook.dll` is loaded in `AMPLibraryAgent.exe`, or if an `IAudioClient::Initialize` arrives in
exclusive mode, the leveler pauses and says so. AMExclusive does not handle Atmos, so there is no conflict on spatial
streams.
