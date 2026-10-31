// Module audio: Ampify.audio, the element's gain. Natives: core/natives/audio.cpp. The analysis, the equalizer, the
// loudness and the volume are added to the same object by the module audioAnalysis (core/prelude/audio_analysis.cpp).

namespace ampify::prelude {

extern const char kAudio[];
const char kAudio[] = R"AMPIFYJS(
  Ampify.audio = {
    setGainDb(db) {
      const value = Number(db);
      if (!Number.isFinite(value)) throw new RangeError("setGainDb expects a number of decibels");
      return native.setGain(value);
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
