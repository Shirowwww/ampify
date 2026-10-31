// Pauses playback after the delay set by the "minutes" setting (30 by default), then lets you know.
const minutes = Ampify.config.get("minutes", 30);
Ampify.log("pause in " + minutes + " min");

setTimeout(() => {
  Ampify.player.pause();
  Ampify.notify({ title: "Timer", text: "Playback is paused." });
}, Math.max(1, minutes) * 60 * 1000);
