// Affiche une notification quand un nouveau morceau commence. Le même morceau n'est annoncé qu'une fois.
let lastTrackId = null;

AMK.player.on("trackchange", (state) => {
  const track = state.track;
  if (!track || track.id === lastTrackId) return;
  lastTrackId = track.id;
  AMK.notify({ title: track.title, text: track.artist });
});
