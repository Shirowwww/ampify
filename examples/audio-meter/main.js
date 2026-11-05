// audio-meter: writes the loudness and the peak of the sound to Ampify's log, about once a second.
// Shows the audio API (onFrame, loudness). It needs only the audio.analyze permission: it changes no sound.

let lastLogged = 0;

Ampify.audio.onFrame((frame) => {
  if (frame.time - lastLogged < 1000) return;
  lastLogged = frame.time;
  Ampify.log("peak " + frame.peak.toFixed(2) + ", rms " + frame.rms.toFixed(2));
});

Ampify.log("loudness now: " + JSON.stringify(Ampify.audio.loudness()));
