// Fixed gain, in decibels, on the decoded samples. The "gainDb" setting defaults to 3. The kernel clamps it
// to its ceiling (audio.maxGainDb). Apple Music's volume slider does not move.
const applied = Ampify.audio.setGainDb(Ampify.config.get("gainDb", 3));
Ampify.log("gain applied: " + applied + " dB");
