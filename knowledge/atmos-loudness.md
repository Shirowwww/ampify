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

- Target: the median of the last 30 stereo songs measured.
- Gain between 0 and 15 dB.
- Peak ceiling 0.95, soft knee from 0.94.
- One value kept per stable variant of the song (`AVCFPlayerItemCopyCurrentStableVariantID`).
- Starting gain for a new Atmos song: 5.0 dB, then it converges as the measurement builds.
- With AMExclusive active (exclusive output), leveling pauses ([amexclusive-notes.md](amexclusive-notes.md)).
