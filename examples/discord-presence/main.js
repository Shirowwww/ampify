// Shows the current track in your Discord status (Rich Presence).
//
// Set up once:
//   ampify config discord.enabled 1
//   ampify config extensions.discord-presence.clientId <id of your Discord application>
// Create the id at https://discord.com/developers/applications ("New Application" button).
// Optional setting: showPaused (1 to keep the status while playback is paused).

Ampify.i18n.define({
  en: { by: "by {artist}", paused: "Paused", unknown: "Unknown title", missing: "Set the id of your Discord application: ampify config extensions.discord-presence.clientId <id>" },
});

const clientId = String(Ampify.config.get("clientId", "")).trim();
const showPaused = Ampify.config.get("showPaused", false);

function show(state) {
  if (!clientId) return;
  const track = state && state.track;
  if (!track || (!state.playing && !showPaused)) {
    Ampify.discord.clear();
    return;
  }
  const activity = {
    clientId,
    details: track.title || Ampify.i18n.t("unknown"),
    state: track.artist ? Ampify.i18n.t("by", { artist: track.artist }) : "Apple Music",
    largeImage: "apple-music",
    largeText: track.album || "Apple Music",
  };
  if (state.playing && state.durationMs > 0) {
    // Discord counts the remaining time from these two dates.
    activity.startMs = Date.now() - state.positionMs;
    activity.endMs = activity.startMs + state.durationMs;
  } else if (!state.playing) {
    activity.smallText = Ampify.i18n.t("paused");
  }
  Ampify.discord.setActivity(activity);
}

if (!clientId) Ampify.log(Ampify.i18n.t("missing"));
Ampify.player.on("trackchange", show);
Ampify.player.on("statechange", show);
show(Ampify.player.state());
Ampify.onStop(() => Ampify.discord.clear());
