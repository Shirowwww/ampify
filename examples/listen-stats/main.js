// Counts plays on this PC: number of plays and time listened, per track and per artist, then shows them in
// a panel. Only the title, the artist and durations are kept, in the item's storage: never any sound.
// A play counts from 30 seconds listened.

Ampify.i18n.define({
  en: {
    title: "Listening stats",
    total: "{plays} plays, {minutes} min listened",
    artists: "Top artists",
    tracks: "Top tracks",
    row: "{rank}. {name} — {plays} play(s)",
    empty: "Nothing played yet",
  },
});

const MIN_PLAY_MS = 30000;
const MAX_TRACKS = 500;  // beyond this, the least played tracks are forgotten (storage is limited to 1 MB)

const stats = Ampify.storage.get("stats", { plays: 0, listenedMs: 0, tracks: {}, artists: {} });
let current = null;  // { id, title, artist, playedMs, since }

function elapsed(now) {
  return current.playedMs + (current.since === null ? 0 : now - current.since);
}

// Adds the time listened to the current track to the counters, then restarts from zero for this track.
function commit(now, finished) {
  if (!current) return;
  const ms = elapsed(now);
  current.playedMs = 0;
  current.since = current.since === null ? null : now;
  if (ms <= 0) return;
  const track = stats.tracks[current.id] || (stats.tracks[current.id] = { title: current.title, artist: current.artist, plays: 0, ms: 0 });
  const artist = stats.artists[current.artist] || (stats.artists[current.artist] = { plays: 0, ms: 0 });
  track.ms += ms;
  artist.ms += ms;
  stats.listenedMs += ms;
  current.total += ms;
  if (finished && !current.counted && current.total >= MIN_PLAY_MS) {
    track.plays++;
    artist.plays++;
    stats.plays++;
    current.counted = true;
  }
  prune();
  Ampify.storage.set("stats", stats);
}

function prune() {
  const ids = Object.keys(stats.tracks);
  if (ids.length <= MAX_TRACKS) return;
  ids.sort((a, b) => stats.tracks[a].ms - stats.tracks[b].ms);
  for (const id of ids.slice(0, ids.length - MAX_TRACKS)) delete stats.tracks[id];
}

function top(table, count) {
  return Object.keys(table)
    .sort((a, b) => table[b].plays - table[a].plays || table[b].ms - table[a].ms)
    .slice(0, count);
}

function render() {
  const lines = (table, names, label) =>
    names.map((key, index) => ({
      type: "text",
      text: Ampify.i18n.t("row", { rank: index + 1, name: label(key), plays: table[key].plays }),
    }));
  const children = [
    { type: "text", text: Ampify.i18n.t("title"), style: "title" },
    { type: "text", text: Ampify.i18n.t("total", { plays: stats.plays, minutes: Math.round(stats.listenedMs / 60000) }) },
  ];
  if (stats.plays === 0 && Object.keys(stats.tracks).length === 0) {
    children.push({ type: "text", text: Ampify.i18n.t("empty") });
  } else {
    children.push({ type: "text", text: Ampify.i18n.t("artists"), style: "subtitle" });
    children.push({ type: "list", children: lines(stats.artists, top(stats.artists, 5), (name) => name) });
    children.push({ type: "text", text: Ampify.i18n.t("tracks"), style: "subtitle" });
    children.push({
      type: "list",
      children: lines(stats.tracks, top(stats.tracks, 5), (id) => stats.tracks[id].title + " (" + stats.tracks[id].artist + ")"),
    });
  }
  Ampify.ui.panel.register("stats", { type: "stack", children });
}

function onTrack(state) {
  const now = Date.now();
  const track = state && state.track;
  if (current && (!track || track.id !== current.id)) {
    commit(now, true);
    current = null;
  }
  if (track && !current) {
    current = { id: track.id, title: track.title, artist: track.artist || "?", playedMs: 0, since: state.playing ? now : null, total: 0, counted: false };
  }
  render();
}

function onState(state) {
  if (!current || !state.track || state.track.id !== current.id) {
    onTrack(state);
    return;
  }
  const now = Date.now();
  if (state.playing && current.since === null) current.since = now;
  if (!state.playing && current.since !== null) {
    current.playedMs += now - current.since;
    current.since = null;
  }
}

Ampify.player.on("trackchange", onTrack);
Ampify.player.on("statechange", onState);
onTrack(Ampify.player.state());
// Time listened is saved every minute, so it is not lost if the app closes.
setInterval(() => {
  commit(Date.now(), false);
  render();
}, 60000);
Ampify.onStop(() => commit(Date.now(), true));
