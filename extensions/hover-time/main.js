// Time on hover: the time of the song under the pointer on the progress line, with a thin marker where it falls, as
// Spotify shows it. Ampify draws both at the pointer's pace (Ampify.ui.playbar.readout); this extension tells it how
// long the song playing is, and takes the readout away while no song is loaded.

let shownMs = -1;

function follow(state) {
  const lengthMs = state && state.track && state.durationMs > 0 ? state.durationMs : 0;
  if (lengthMs === shownMs) return;
  shownMs = lengthMs;
  Ampify.ui.playbar.readout(lengthMs);
}

follow(Ampify.player.state());
// The length can come a moment after the song: a state change brings it.
Ampify.player.on("trackchange", follow);
Ampify.player.on("statechange", follow);
