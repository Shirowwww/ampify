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
  to `tB`. The queue moves on at `tA`, but the next song is heard **50 ms late**: measured four times (two points of a
  song, both kinds of seek) at 49.9 ± 0.2 ms in the recorded sound, and the same by the clocks once the next song has
  played half a second (read at the switch itself, its clock shows only 23 to 29 ms). AutoMix seeks the next song that
  much further, learns the delay again at each mix, and then lands within 0.1 ms. An invalid time (flags 0) as the end
  time removes it.
- `AVCFPlayerItemSeekToTime` seeks with an infinite tolerance (`CoreMedia.dll` ordinal 718 as both tolerances, seen
  in its code); `AVCFPlayerItemSeekToTimeWithTolerance(item, time, before, after)` with zero tolerances is exact.
  Both gave the same 50 ms above.
- **Overlapped playback**: `AVCFPlayerItemSetAdvanceTimeForOverlappedPlayback(current, t)` starts the next item at
  `t` while the current one goes on to its end (or its forward end time). Both play at full volume, in exact step.
- **A second player** on Apple's own asset: `AVCFPlayerItemCreateWithAsset(NULL, asset, queue)` and
  `AVCFPlayerCreateWithPlayerItemAndOptions(NULL, item, options, queue)`, with Apple's options and queue. It plays the
  protected stream through Apple's own path (nothing of FairPlay is touched), and AVFoundation mixes it into the same
  output stream as Apple's player.
- **Clocks**: `AVCFPlayerGetCurrentTime` and `AVCFPlayerItemGetCurrentTime` of two players, read back to back, give
  the gap between them; it matched the delay of the copy heard in the recording to 0.2 ms, and stayed within 0.05 ms
  for 20 s.
- **Seeking a playing player** lands about 100 ms behind its target (measured: 96.0, 101.9, 105.0, 100.1, 99.6 ms),
  so a few seeks with a learned delay put it within a millisecond of a target: 3 to 9 seeks in seven runs, once more
  than 14 (hence 20 seeks at most, and 2 then 3 ms accepted after the sixth). Its first start, from a fresh item,
  takes 0.3 to 2 s.
- **`AVCFPlayerSetVolume`** sets one player's volume, smoothly at a hundred changes a second.

## Does not work on Apple Music's streams

- Audio mixes (`AVCFPlayerItemSetAudioMix` with `AVCFAudioMixInputParametersSetVolumeRamp`): taken, read back
  (`AVCFAudioMixInputParametersGetVolumeRamp`), never heard. A player item gets its tracks only once it plays.
- Processing taps (`MTAudioProcessingTapCreate`, on the item with `AVCFPlayerItemSetAudioTapProcessor` or in an audio
  mix): never called, so no effect can be put on one song only. iOS has the same limit for HLS.
- `AVCFPlayerItemTrackSetEnabled(track, false)`: no effect.
- `AVCFPlayerSetRate` other than 1 on the second player: its sound jumps back 80 to 280 ms at each change (seen in
  the recording), so a song cannot be stretched to another's tempo this way.
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
- The calls were tried from the agent's poll thread through `GetProcAddress`, with the trace of Apple's own calls
  (an import hook per function) in the same journal.
