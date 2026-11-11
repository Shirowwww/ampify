// Shows the volume of Apple Music's slider as a percentage in the playback bar. The volume is read, never set: the
// slider stays Apple Music's, and the percentage is a text, not a button. Ampify reads it from Apple Music's
// preferences at start, then from the player; an Ampify older than 1.3 knows it only once a song starts ("– %").
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

// No onClick: a text of the bar, which the pointer goes through, right after the volume button (Ampify before 1.3.2
// puts it at the end of the bar). Ampify before 1.3 wants an onClick: a click then reads the volume again.
function addText() {
  try {
    return Ampify.ui.playbar.addButton({ label: label(), beside: "volume" });
  } catch {
    return Ampify.ui.playbar.addButton({ label: label(), onClick: refresh });
  }
}
const button = addText();
shown = label();

const timer = setInterval(refresh, EVERY_MS);
Ampify.onStop(() => clearInterval(timer));
