// Shows the volume of Apple Music's slider as a percentage in the playback bar. The volume is read, never set: the
// slider stays Apple Music's. It is known once Apple Music has set it, usually when the first song starts; until then
// the button shows "– %".
const EVERY_MS = 300;
// Apple Music's slider is in decibels: position p gives 30 × (p − 1) dB (measured on Apple Music 1.1792).
const SLIDER_RANGE_DB = 30;
let shown = null;

// The slider's position for the gain Apple Music set, so the label matches what the slider shows.
function sliderPosition(gain) {
  if (gain <= 0) return 0;
  return Math.min(1, Math.max(0, 1 + (20 * Math.log10(gain)) / SLIDER_RANGE_DB));
}

function label() {
  const volume = Ampify.audio.volume();
  return volume === null ? "– %" : Math.round(sliderPosition(volume) * 100) + " %";
}

function refresh() {
  const text = label();
  if (text === shown) return;
  shown = text;
  Ampify.ui.setLabel(button, text);
}

// A click reads the volume again at once, for a change made a moment ago.
const button = Ampify.ui.playbar.addButton({ label: label(), onClick: refresh });
shown = label();

const timer = setInterval(refresh, EVERY_MS);
Ampify.onStop(() => clearInterval(timer));
