// Module audio: AMK.audio, the element's gain. Natives: core/natives/audio.cpp.

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
