# Apple Music's player engine (AVFoundationCF): what a mix can use

Measured on 12/11/2026 with Apple Music 1.1792.39942.0, a stereo output at 96 kHz, catalog songs (HLS streams). Each
point was checked in the agent's journal and in recordings of the output (`record.on`, below), not only in the API's
answers. AutoMix (`src/win/audio/automix.cpp`, `src/core/automix/`) is built on the "works" list.

## Apple's queue player

- One `AVCFQueuePlayer` in `AMPLibraryAgent.exe`, made when playback first starts:
  `AVCFQueuePlayerCreateWithPlayerItemsAndOptions(NULL, NULL, options, notificationQueue)`.
  - `options` is a temporary dictionary (`AVCFPlayerAppliesMediaSelectionCriteriaAutomaticallyKey` among its three
    keys): `CFRetain` it to make another player with it later; a freed copy crashed the agent in `objc.dll`.
  - `notificationQueue` is a libdispatch queue (`com.apple.iTunes.ITAVCF`), not a CoreFoundation object: `CFRetain` on
    it crashes the agent in `objc.dll` too.
- A few seconds after a song starts, Apple inserts the next one (`AVCFQueuePlayerInsertItem` after the current item),
  then calls `AVCFPlayerSetRate(player, 1)`. When a song ends it calls `AVCFPlayerItemSetAudioTapProcessor(item, NULL)`
  twice on it. It never calls the audio mix or volume ramp functions.
- With "repeat one" (the button reads « Répéter un élément »), the next item is the same song again (same stream
  address). The button's name is the current mode: « Ne pas répéter », « Tout répéter », « Répéter un élément ».
- Items are `AVCFURLAsset`s on HLS: `https://aod.itunes.apple.com/itunes-assets/HLSMusic.../P<number>_lossless.m3u8`.
  The `P` number is not the song's catalog id (one of them is an audiobook's). The catalog id is in the asset's
  creation options (`AVCFURLAssetCopyCreationOptions`), under `AVCFURLAssetAlternativeConfigurationOptionsKey`:
  `MediaIdentifier` = `aadamid=1607320391,adamid=1607320391,sadamid=1607320391,`, `StorefrontID` = `143442-3,42`
  (143442 is France).
- The player supports overlapped playback (`AVCFPlayerGetSupportsAdvanceTimeForOverlappedPlayback` is true).

## Works

- **A switch at an exact point**: `AVCFPlayerItemSetForwardPlaybackEndTime(current, tA)` and a seek of the next item
  to `tB`. The queue moves on at `tA`, and the next song is heard **60 or 70 ms late** (see "Streams" for where the
  time goes). AutoMix seeks the next song that much further, counts the delay in samples at each mix and keeps a
  running mean: the next song then lands within 5 ms of its point (13 ms seen once), and the journal's figure is the
  recording's (-4.98 ms said, -5.0 ms recorded). An invalid time (flags 0) as the end time removes it.
- `AVCFPlayerItemSeekToTime` seeks with an infinite tolerance (`CoreMedia.dll` ordinal 718 as both tolerances, seen
  in its code); `AVCFPlayerItemSeekToTimeWithTolerance(item, time, before, after)` with zero tolerances is exact: the
  item starts on the very frame asked for.
- **Overlapped playback**: `AVCFPlayerItemSetAdvanceTimeForOverlappedPlayback(current, t)` starts the next item at
  `t` while the current one goes on to its end (or its forward end time). Both play at full volume, in exact step.
- **A second player** on Apple's own asset: `AVCFPlayerItemCreateWithAsset(NULL, asset, queue)` and
  `AVCFPlayerCreateWithPlayerItemAndOptions(NULL, item, options, queue)`, with Apple's options and queue. It plays the
  protected stream through Apple's own path (nothing of FairPlay is touched), into a WASAPI stream of its own.
- **Clocks**: `AVCFPlayerGetCurrentTime` and `AVCFPlayerItemGetCurrentTime` of two players, read back to back, give
  the gap between them; it matched the delay of the copy heard in the recording to 0.2 ms, and stayed within 0.05 ms
  for 20 s.
- **Seeking a playing player** lands about 100 ms behind its target (measured: 96.0, 101.9, 105.0, 100.1, 99.6 ms)
  and seconds late on a stream that is still loading (3.4 s seen). Its first start, from a fresh item, takes 0.3 to
  2 s. AutoMix no longer seeks it into step: it sends it two to four seconds ahead (one seek as a rule, four at most)
  and delays its samples itself, by the lag at which Apple's last quarter second is found in them.
- **`AVCFPlayerSetVolume`** sets one player's volume, smoothly at a hundred changes a second.

## Streams: where each song's samples can be reached

Measured on 12/11/2026 by hooking `IAudioClient::GetService` and `IAudioRenderClient::ReleaseBuffer` in the agent and
by recording the default output (WASAPI loopback) while a song was mixed into itself.

- Every player renders into its own shared-mode stream (float, the device's rate), and makes **a new stream for each
  song and at each seek**. So each song's samples pass alone through `ReleaseBuffer`: gain, filters and time
  stretching can be put on one song only, which the player's own taps and audio mixes refuse (below).
- A new render client often gets the **address of the one just released**. Telling streams apart by address misses
  the switch; AutoMix numbers them as `GetService` hands them out.
- Windows takes 960 frames per pass at 96 kHz (10 ms). A stream's first block is 2112 frames, the next ones 192.
- At a switch (end time on the current item, next item sought), counted in samples and confirmed by the recording:
  the old stream sounds up to its last sample; the new one gets its first block 768 or 1728 frames later (8 or 18 ms,
  it alternates); it begins with 2114 empty frames (22 ms); and the next song is heard **30.0 ms** after that count
  says (the same on four switches and three target points: +0, +13 and +200 ms). 60 or 70 ms in all, 78 ms seen once
  with a stretched song.
- The two players' own clocks do not tell this delay: read at the switch they gave 18 to 59 ms for a delay that the
  recording showed fixed.
- What this allows, measured on the recorded output: the second player's copy of the current song replaces Apple's
  to the sample (100 ms crossfade between the two streams, residual -15 to -27 dB from the two decoders); a song
  stretched by +6 % plays 1.060 times faster at the same pitch (+0 cent, -0.1 dB, no more clicks than the plain song)
  and one stretched by -6 % ends its ramp 234 to 310 ms behind (248 expected); a low shelf at 150 Hz takes 29.7 dB off
  20-60 Hz, 23.8 dB off 60-100 Hz, 17.5 dB off 100-150 Hz and leaves everything above 500 Hz alone.
- The second player's stream stops about a second after it reaches the end of the song, and the deck only sends
  sound out through that stream. A mix ending 0.25 s before the end of the song, with the second player 1.98 s
  ahead, failed 0.3 s before its last key ("the second player stopped"); ending 3.1 s before the end it played out.
  Hence `RoomAfter` (`src/core/automix/mix_plan.cpp`): a plan must leave the song that long after its last key.

## Dolby Atmos

- A song that plays in Dolby Atmos has no stereo stream at all: Windows gets spatial objects
  ([atmos-loudness.md](atmos-loudness.md)). Nothing above reaches it, so AutoMix mixes neither out of nor into it.
- Apple's player picks the version when the song starts. Before that the next item has no tracks, so its format
  cannot be read: the kernel's `atmos` field of the next song stays `null`. The same song came out in stereo once and
  in Atmos five times on the same PC and settings.
- `AVCFPlayerItemSetAllowedAudioSpatializationFormats` (Apple always passes 4) and
  `AVCFPlayerItemSetVariantPreferences` (4, then 7) do not decide it: with 0 or 3 for the first, or 0 for the second
  (which only took the lossless version away), the song still played in Atmos.
- An item made without variant preferences (0) plays the stream's first, lossy version. The second player's item so
  takes those of Apple's item (`AVCFPlayerItemGetVariantPreferences`, then `Set`): measured, Apple's item on variant
  `ek6n0nusx` with preferences 7, the second player's on `j2lqjiv2c` with 0 before, both on `ek6n0nusx` with 7 after
  (`AVCFPlayerItemCopyCurrentStableVariantID`). The two copies of a song still differ by -28 to -39 dB on some
  songs and -66 dB on others, with the same variant: the cause is not the version, and is not found.
- What the window shows at a mix (captures every 150 ms, 2026-11-12): title, cover, list highlight and time change
  in one frame, as at a song that ends by itself; the lyrics panes show "Loading" for about 0.4 s, then the line of
  the point the next song starts from. Apple's time display runs late at every song start, mix or not: it shows
  0:01 about 2 s in.
- A mix into an Atmos song, as recorded: the song comes in 0.1 to 0.7 s late (a spatial stream is activated for it)
  and at full volume. The `automix` extension therefore reads `audioTraits` in the catalog (`"atmos"` when the song
  has an Atmos version) and remembers whether such songs play in Atmos on this PC.

## Does not work on Apple Music's streams

- Audio mixes (`AVCFPlayerItemSetAudioMix` with `AVCFAudioMixInputParametersSetVolumeRamp`): taken, read back
  (`AVCFAudioMixInputParametersGetVolumeRamp`), never heard. A player item gets its tracks only once it plays.
- Processing taps (`MTAudioProcessingTapCreate`, on the item with `AVCFPlayerItemSetAudioTapProcessor` or in an audio
  mix): never called. iOS has the same limit for HLS. Effects go on the song's own stream instead (above).
- `AVCFPlayerItemTrackSetEnabled(track, false)`: no effect.
- `AVCFPlayerSetRate` other than 1 on the second player: its sound jumps back 80 to 280 ms at each change (seen in
  the recording). AutoMix stretches the samples itself (`src/core/automix/tempo_stretch.cpp`, Signalsmith Stretch).
- `AVCFPlayerItemSetAudioMix(item, NULL)` crashes the agent (`AVFoundationCF.dll` + 0x5ebce): set an empty mix.
- Loudness info: Apple only sets `{"adjust to album loudness": true}` on its items.

## Apple's analysis of a song

- `https://amp-api.music.apple.com/v1/catalog/<country>/songs/<id>?include=audio-analysis&extend[audio-analysis]=fades,loudnessCurve`,
  with the web player's token (`authorization: Bearer <token>`, `origin: https://music.apple.com`). The token is a
  JWT (`eyJ...`) in the player script that music.apple.com's pages load (`/assets/index~<hash>.js`); the one read on
  12/11/2026 was issued for 70 days. The extension keeps it five days and drops it on a 401 or 403.
- Its attributes: `beats` (`beatsInMilliseconds`, `barsInMilliseconds`), `bpm` (`main`, `beginning`, `ending`),
  `danceability` (same three parts), `fades` (`fadeIn`, `fadeOut`: start and end in ms), `loudness.main` (`value`,
  `peak`, `range`), `loudnessCurve.value` (one value every half second, a second's window each), `vocalActivity`
  (spans with a `strength`), `key`, `energy`, `acousticness`, `melodicness`, `valence`.
- `key` is `{ beginning, ending, main }`, each `{ tonic, mode }` (`"D"`, `"minor"`), missing on some songs.
- The song's structure comes only when asked for: `include=audio-analysis,flexml-analysis`. Its `videoEvents` has two
  lists of the same length, `timeInSeconds` and `score`: 200 is a beat, 400 a bar, 600 and more a phrase, 800 and
  more a section. It also has `entryPoints` and `exitPoints`.
- The beat grid follows the song: one whose intro is slower has longer bars there (3.35 to 3.39 s, then 3.20 s from
  16.7 s on), and the recorded sound repeats at exactly those lengths (845 ms a beat in the intro, 800.0 ms after).
  The `bpm` values are rounded and say less than the bars.
- Fade-outs are loose: one song's says 161.6 to 166.7 s, while its loudness curve drops 15 dB within a second of
  162.3 s. The curve says where a song really falls silent.
- Tempos rarely match: on 49 pairs of songs in a row in the French chart, one pair was within 3 % of each other.

## Signatures (x64)

- `CMTime` is `{int64 value; int32 timescale; uint32 flags; int64 epoch}`, passed by value (by pointer in the ABI).
- `MTAudioProcessingTapCallbacks` is packed to 4 bytes: version (0) at 0, clientInfo at 4, init 0x0C, finalize 0x14,
  prepare 0x1C, unprepare 0x24, process 0x2C (required). The creation flags must be exactly 1 (pre effects) or 2
  (post effects). Windows has no `MTAudioProcessingTapGetStorage`.

## How it was measured

- `record.on` in the Ampify folder: `src/win/audio/output_record.cpp` writes what the stereo stream sends to
  `logs\output-<time>.wav` (float, the device's rate, two minutes at most) once the file goes.
- A copy of the song inside the recording (the second player at another point of the same song) shows as an
  autocorrelation peak: its lag is the true gap between the players. The same "echo test" measures a mix: with
  "repeat one", a plan that brings the song in again half a second behind itself, both at 0.7 for three seconds,
  puts the peak at 500 ms plus whatever the next song's start missed by (0.4 against 0.15 elsewhere; no peak outside
  the mix).
- A mix's loudness: the quietest second inside the mix against the songs before and after it. Over the end of a song
  that dies out within a second (Apple's fade-out said five), a six-second crossfade fell 8.5 dB; the crossfade that
  ends where the loudness curve falls silent fell 0.3 dB. On 49 pairs of Apple's French chart, the planner's own
  estimate from the loudness curves gives a median of 0.9 dB.
- What is heard, whatever the stream it comes from: a WASAPI loopback recording of the default output. A song mixed
  into itself at a known point shows the switch delay as the lag between the two copies; a recording of the same
  songs without a mix gives each song's gain along a mix by least squares, band by band for the filters.
- Beat grids fitted to the onsets of a recording settle a tempo, not a phase: on rap, the low band and the full band
  put the same grid half a beat apart.
- The calls were tried from the agent's poll thread through `GetProcAddress`, with the trace of Apple's own calls
  (an import hook per function) in the same journal.
