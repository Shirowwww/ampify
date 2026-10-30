// Affiche le morceau en cours dans le statut Discord (Rich Presence).
//
// À régler une fois :
//   amk config discord.enabled 1
//   amk config extensions.discord-presence.clientId <identifiant de ton application Discord>
// L'identifiant se crée sur https://discord.com/developers/applications (bouton « New Application »).
// Réglage facultatif : showPaused (1 pour garder le statut quand la lecture est en pause).

AMK.i18n.define({
  fr: { by: "par {artist}", paused: "En pause", unknown: "Titre inconnu", missing: "Indique l'identifiant de ton application Discord : amk config extensions.discord-presence.clientId <identifiant>" },
  en: { by: "by {artist}", paused: "Paused", unknown: "Unknown title", missing: "Set the id of your Discord application: amk config extensions.discord-presence.clientId <id>" },
});

const clientId = String(AMK.config.get("clientId", "")).trim();
const showPaused = AMK.config.get("showPaused", false);

function show(state) {
  if (!clientId) return;
  const track = state && state.track;
  if (!track || (!state.playing && !showPaused)) {
    AMK.discord.clear();
    return;
  }
  const activity = {
    clientId,
    details: track.title || AMK.i18n.t("unknown"),
    state: track.artist ? AMK.i18n.t("by", { artist: track.artist }) : "Apple Music",
    largeImage: "apple-music",
    largeText: track.album || "Apple Music",
  };
  if (state.playing && state.durationMs > 0) {
    // Discord compte le temps restant à partir de ces deux dates.
    activity.startMs = Date.now() - state.positionMs;
    activity.endMs = activity.startMs + state.durationMs;
  } else if (!state.playing) {
    activity.smallText = AMK.i18n.t("paused");
  }
  AMK.discord.setActivity(activity);
}

if (!clientId) AMK.log(AMK.i18n.t("missing"));
AMK.player.on("trackchange", show);
AMK.player.on("statechange", show);
show(AMK.player.state());
AMK.onStop(() => AMK.discord.clear());
