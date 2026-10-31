// 10-band equalizer (31 Hz to 16 kHz). It works on the decoded samples, after the kernel's gain and before
// its limiter. Apple Music's volume slider does not move.
//
// Settings (ampify config extensions.equalizer.<key> <value>):
//   preset  name of a preset, "flat" by default (no effect); the list is written to the log
//   bands   gains in dB separated by commas, e.g. "3,2,0,0,0,0,0,1,2,3"; replaces the preset
// When the extension stops, the equalizer goes back to flat.

const preset = Ampify.config.get("preset", "flat");
const bands = Ampify.config.get("bands", "");

Ampify.log("presets: " + Ampify.audio.eq.presets().join(", "));

if (bands !== "") {
  const gains = String(bands).split(",").map(Number);
  gains.forEach((gain, index) => {
    if (index < 10 && Number.isFinite(gain)) Ampify.audio.eq.setBand(index, gain);
  });
  Ampify.log("bands set by hand: " + gains.join(" / ") + " dB");
} else if (Ampify.audio.eq.preset(preset)) {
  Ampify.log("preset \"" + preset + "\" applied");
} else {
  Ampify.log("equalizer not available in this process");
}

const summary = Ampify.audio.eq
  .bands()
  .map((band) => (band.frequency >= 1000 ? band.frequency / 1000 + " kHz" : band.frequency + " Hz") + " " + band.gainDb + " dB")
  .join(", ");
Ampify.log("equalizer: " + summary);
