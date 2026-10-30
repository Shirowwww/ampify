// Module audioAnalysis: adds to AMK.audio (made by the module audio) the analysis frames, the tempo, the loudness,
// the player volume (read only) and the equalizer. Natives: core/natives/audio_analysis.cpp. The kernel computes
// everything in C++ on the decoded samples; the script only gets small frames of numbers.

namespace amk::prelude {

extern const char kAudioAnalysis[];
const char kAudioAnalysis[] = R"AMKJS(
  const frameListeners = [];
  const presetNames = Object.freeze(JSON.parse(native.eqPresetNames));

  const eq = {
    setBand(index, gainDb) {
      const band = Number(index);
      const gain = Number(gainDb);
      if (!Number.isInteger(band)) throw new RangeError("eq.setBand attend un numéro de bande entier");
      if (!Number.isFinite(gain)) throw new RangeError("eq.setBand attend un gain en décibels");
      return native.eqSetBand(band, gain);
    },
    preset(name) {
      return native.eqPreset(text(name));
    },
    reset() {
      return native.eqPreset("flat");
    },
    bands() {
      return JSON.parse(native.eqBands());
    },
    presets() {
      return presetNames.slice();
    },
  };
  Object.freeze(eq);

  Object.assign(AMK.audio, {
    onFrame(fn) {
      if (typeof fn !== "function") throw new TypeError("audio.onFrame attend une fonction");
      const available = native.audioSubscribe();
      frameListeners.push(fn);
      return available;
    },
    analyze() {
      return JSON.parse(native.audioAnalyze());
    },
    loudness() {
      return JSON.parse(native.audioLoudness());
    },
    volume() {
      return native.audioVolume();
    },
    eq,
  });

  dispatch.audioFrame = () => {
    const json = native.audioTakeFrame();
    if (json === null) return;
    const frame = JSON.parse(json);
    for (const fn of frameListeners.slice()) runGuarded("audio.onFrame", fn, [frame]);
  };
)AMKJS";

}  // namespace amk::prelude
