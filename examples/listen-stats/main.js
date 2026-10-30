// Compte les écoutes sur ce PC : nombre de lectures et temps écouté, par titre et par artiste, puis les montre dans
// un panneau. Seuls le titre, l'artiste et des durées sont gardés, dans le stockage de l'élément : jamais de son.
// Une lecture compte à partir de 30 secondes écoutées.

AMK.i18n.define({
  fr: {
    title: "Statistiques d'écoute",
    total: "{plays} lectures, {minutes} min écoutées",
    artists: "Artistes les plus écoutés",
    tracks: "Titres les plus écoutés",
    row: "{rank}. {name} — {plays} lecture(s)",
    empty: "Rien d'écouté pour l'instant",
  },
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
const MAX_TRACKS = 500;  // au-delà, les titres les moins écoutés sont oubliés (le stockage est limité à 1 Mo)

const stats = AMK.storage.get("stats", { plays: 0, listenedMs: 0, tracks: {}, artists: {} });
let current = null;  // { id, title, artist, playedMs, since }

function elapsed(now) {
  return current.playedMs + (current.since === null ? 0 : now - current.since);
}

// Ajoute le temps écouté du morceau en cours aux compteurs, puis repart de zéro pour ce morceau.
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
  AMK.storage.set("stats", stats);
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
      text: AMK.i18n.t("row", { rank: index + 1, name: label(key), plays: table[key].plays }),
    }));
  const children = [
    { type: "text", text: AMK.i18n.t("title"), style: "title" },
    { type: "text", text: AMK.i18n.t("total", { plays: stats.plays, minutes: Math.round(stats.listenedMs / 60000) }) },
  ];
  if (stats.plays === 0 && Object.keys(stats.tracks).length === 0) {
    children.push({ type: "text", text: AMK.i18n.t("empty") });
  } else {
    children.push({ type: "text", text: AMK.i18n.t("artists"), style: "subtitle" });
    children.push({ type: "list", children: lines(stats.artists, top(stats.artists, 5), (name) => name) });
    children.push({ type: "text", text: AMK.i18n.t("tracks"), style: "subtitle" });
    children.push({
      type: "list",
      children: lines(stats.tracks, top(stats.tracks, 5), (id) => stats.tracks[id].title + " (" + stats.tracks[id].artist + ")"),
    });
  }
  AMK.ui.panel.register("stats", { type: "stack", children });
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

AMK.player.on("trackchange", onTrack);
AMK.player.on("statechange", onState);
onTrack(AMK.player.state());
// Le temps écouté est enregistré chaque minute, pour ne pas le perdre si l'app se ferme.
setInterval(() => {
  commit(Date.now(), false);
  render();
}, 60000);
AMK.onStop(() => commit(Date.now(), true));
