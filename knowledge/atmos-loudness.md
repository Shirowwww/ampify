# Atmos volume: what Apple does and what works

Measured on Apple Music for Windows `1.1540.23042.0`. It completes the "Playback" section of
[apple-music-internals.md](apple-music-internals.md). Addresses (RVA) are only here for understanding: Ampify never
resolves a function by address.

## Apple's system does not set the Atmos volume

**Sound Check never touches Atmos.**

- `AVCFPlayerItemSetLoudnessInfo` only receives `{"adjust to album loudness": true}`.
- Atmos items carry no loudness information.
- The audio tap (equalizer, Sound Check) is skipped on purpose for Atmos: preference
  `skip-audio-tap-for-atmos-passthrough`. An element that went through the tap would not see Atmos.

**CoreMedia has its own Atmos leveling, and it is not enough.**

- `media::MediaEngineWrapper::EnableLoudnessModeForDolbyAtmosMusic` (RVA `0x2067b0`) sets a flag (`+0x365`) before the
  engine is created.
- Creating the engine (`0x207130`) then calls `ApplyLoudnessModeForDolbyAtmosMusic` (`0x206cf0`). That inserts the Dolby
  audio effect with `CODECAPI_AVDecDDOperationalMode` = preference `atmos-loudness-mode` (domain `com.apple.coremedia`,
  read once per process, default `8` = PORTABLE16).
- The RVAs are valid for a `CoreMedia.dll` whose PE timestamp is `0x69b9c1b1`.

**BS.1770 measurements on the same song:**

| Setting | Loudness |
|---|---|
| Atmos, mode 8 (Apple's, always applied and successful) | -19.9 LUFS |
| Atmos, modes 7 and 6 (the decoder falls back with no dialnorm) | -31.15 LUFS |
| Stereo with Sound Check | -15.9 to -16.1 LUFS |

**Do not try this setting again.** Only mode 8 works, and it leaves Atmos about 4 dB below stereo. Ampify neither reads nor
writes `atmos-loudness-mode`.

## The only fix that works: a gain on the decoded samples, measured per song

| Path | Interface | Format | Position relative to the slider |
|---|---|---|---|
| Stereo | `IAudioRenderClient` | float32, 2 channels, 96 kHz (the test output) | after: the player volume is already applied |
| Atmos | `ISpatialAudioObjectRenderStreamForMetadata` (IID `Data1` = `0xBBC9C907`) | mono float32 objects, 48 kHz, 16 dynamic | before: the slider volume is not applied yet |

Methods hooked on the fly on the Atmos stream (vtable numbers): `GetBuffer` at 3 (on the audio object),
`BeginUpdatingAudioObjects` at 8, `EndUpdatingAudioObjects` at 9, object activation at 10 and 11.

Consequence: to compare Atmos with stereo, the measurement must bring both paths to the same scale (stereo divided by the
player volume, or Atmos multiplied by it).

Apple Music's slider shows the player volume (`AVCFPlayerSetVolume`). The leveler reads it to scale its measurements and
applies its gain to the samples.


## Leveling settings

- Target: the median of the latest stereo songs measured.
- The gain is bounded, with a peak ceiling and a soft knee. The numbers are in `extensions/atmos-leveler/leveler.hpp`
  (`LevelerConfig`); the knee is in `src/core/audio/gain_ramp.cpp`.
- One value kept per stable variant of the song (`AVCFPlayerItemCopyCurrentStableVariantID`).
- A new Atmos song starts at a fixed gain, then it converges as the measurement builds.
- With AMExclusive active (exclusive output), leveling pauses ([amexclusive-notes.md](amexclusive-notes.md)).

## A seek opens a new spatial stream, and the renderer starts too loud

Measured on `1.1792.39942.0` with Dolby Atmos for Headphones, by recording the endpoint (WASAPI loopback) while an A-B
loop came back to the same passage, and comparing with that passage played through:

- Every seek (an A-B loop return, a click in the scrubber) makes Apple Music activate a new
  `ISpatialAudioObjectRenderStreamForMetadata`. A change of song keeps the same stream.
- For the first 0.8 s of a new stream the output is 10 to 15 dB louder than the same passage played through, then it
  matches within 1 dB. Same result with the leveler off (no gain), so it is not the leveler.
- The objects the hooks see are not louder over that time: the burst comes after them, in the renderer.

`ObjectPass` is made again for each new stream, so it fades its gain in: held at -12 dB for 0.4 s, then up to the
gain by 0.8 s (`object_pass.cpp`). It only does so while it gains the objects: at unity the samples stay untouched.

Apple Music's own "Dolby Atmos" setting (Settings > Playback) can come up empty; Apple Music then plays every song in
stereo, with no spatial stream at all. Choosing "Automatic" again brings Atmos back.
