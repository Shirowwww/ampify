// Shows a notification when a new track starts. The same track is announced only once.
let lastTrackId = null;

Ampify.player.on("trackchange", (state) => {
  const track = state.track;
  if (!track || track.id === lastTrackId) return;
  lastTrackId = track.id;
  Ampify.notify({ title: track.title, text: track.artist });
});
