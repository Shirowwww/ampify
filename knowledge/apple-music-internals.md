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
- **The interface shows the player volume.** If a mod changes `AVCFPlayerSetVolume`, Apple Music's slider moves (observed:
  55% in stereo, 71.67% in Atmos with +5 dB). Never set the gain through the player volume: act on the samples.

## What was already built (in `../reference/`)

- `apple-music-atmos-gain.wh.cpp`: a Windhawk mod (v2.3) injected in `AMPLibraryAgent.exe`. It watches `AVCFPlayerSetVolume`
  (without changing it), detects Atmos from the format, measures the decoded sound (BS.1770) on the WASAPI stereo stream
  and on the spatial objects, learns the target from stereo songs leveled by Sound Check, then applies to each Atmos song
  a gain on the samples, with a ramp and a soft limiter. It remembers the value per variant.
- `loudness-meter/`: a C# .NET 8 tool that measures the LUFS of the output by WASAPI loopback, in memory only.

## Pitfalls observed

- The package folder is read-only. Apple Music's writes to AppData and HKCU are redirected into
  `%LOCALAPPDATA%\Packages\AppleInc.AppleMusicWin_nzyj5cx40ttqa\LocalCache\` (MSIX virtualization).
- Logs: ETW in `...\LocalState\Logs\*.etl`, readable with `tracerpt <file> -o x.csv -of CSV`. The active file is empty until
  it has run (one rotation per launch).
- Preferences: `...\LocalCache\Roaming\Apple Computer\Preferences\AMPLibraryAgent.exe.plist` (binary plist; `volumeWSG`,
  `losslessEnabled`, `downloadDolbyAtmos`, `crossfadeSeconds`...).
- Injection: Windhawk (DLL injection at process start) is tolerated by Apple Music, and so is a hot reload. A Frida trial
  coincided with Apple Music closing, but that came from another cause (another tool closed it). Nothing points to
  anti-debugging detection, though that is not proven.
- After a restart, the play queue is empty: playback can only resume by a click from the user. The Windows media commands
  (SMTC) handle play, pause, next and previous, but not choosing a song or a position.
- A freeze of about 7 s around 15 s into the first Atmos song after a launch happens with no mod at all (a FairPlay key
  request times out at 20 s, then the retry succeeds). It is not a bug to blame on mods.
- First launch: an MPEG LA licence (buttons Agree, Decline, Save As) opens before the main window, then, after sign-in, a
  "Welcome to Apple Music" screen (Start Listening). While they are open there is no XAML window.
- Cover and lyrics of the playing item (read from the 1.1792 metadata, `core/amp_abi.hpp`): `INowPlayingItem.get_Artwork`
  (slot 11) gives an `Artwork`, queried as `IArtwork`: `get_Uri` is a `Windows.Foundation.Uri` whose absolute address is
  probably an `mzstatic.com` template with `{w}`, `{h}`, `{c}` and `{f}`; `get_BGColor` is a `Windows.UI.Color` (A, R, G, B).
  `get_LyricsInfo` (slot 46) gives `ILyricsInfo`: `HaveLyrics`, `GetTTML` (timed lines and words), `GetStaticLyrics`,
  `GetLanguage`. Lyrics may come some seconds after the track starts. None of this is confirmed on a real Apple Music yet.
- Versions: `1.1540.23042.0` and `1.1792.39682.0` are both in the tested list. In 1.1792 the interfaces of
  `AMP.Services.Player` changed ("IPlayer absent" in the log), so the player read falls back to SMTC.
