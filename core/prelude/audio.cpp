// Module audio: AMK.audio, the element's gain. Natives: core/natives/audio.cpp. The analysis, the equalizer, the
// loudness and the volume are added to the same object by the module audioAnalysis (core/prelude/audio_analysis.cpp).

namespace amk::prelude {

extern const char kAudio[];
const char kAudio[] = R"AMKJS(
  AMK.audio = {
    setGainDb(db) {
      const value = Number(db);
      if (!Number.isFinite(value)) throw new RangeError("setGainDb attend un nombre de décibels");
      return native.setGain(value);
    },
  };
)AMKJS";

}  // namespace amk::prelude
