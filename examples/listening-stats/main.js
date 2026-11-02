// Listening statistics kept on this PC, like Spicetify's Stats: your top artists and songs of the last 7 days, the
// last 4 weeks or since the start, and the time spent listening. A song counts as a play after 30 seconds of
// listening, once per play. Nothing leaves the PC: the counts live in this element's storage.
const { h, useState, useEffect } = Ampify.React;
const { Card, Dropdown, Text } = Ampify.components;

Ampify.i18n.define({
  en: {
    title: "Listening stats", period: "Period", week: "Last 7 days", month: "Last 4 weeks", all: "Since the start",
    time: "Listening time", play: "1 play", plays: "{count} plays", artists: "Top artists", songs: "Top songs",
    empty: "Nothing yet: songs count after 30 seconds of listening.", hours: "{hours} h {minutes} min",
    minutes: "{minutes} min",
  },
  fr: {
    title: "Statistiques d'écoute", period: "Période", week: "7 derniers jours", month: "4 dernières semaines",
    all: "Depuis le début", time: "Temps d'écoute", play: "1 écoute", plays: "{count} écoutes",
    artists: "Artistes les plus écoutés", songs: "Morceaux les plus écoutés", empty: "Rien pour l'instant : un morceau compte après 30 secondes d'écoute.",
    hours: "{hours} h {minutes} min", minutes: "{minutes} min",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);
const playCount = (count) => (count === 1 ? t("play") : t("plays", { count }));

const TICK_MS = 5000;
const PLAY_AFTER_MS = 30000;
const KEEP_DAYS = 28;
const TOP = 5;
const MAX_SONGS_PER_DAY = 400;  // a day of non-stop listening is far below this
const MAX_SONGS_IN_ALL = 20000;  // keeps the storage small; past it, only songs already counted grow

// Storage: days["YYYY-MM-DD"] = { songs: { key: { title, artist, plays, ms } } }, the 28 last days; all = the same,
// since the start.
let days = Ampify.storage.get("days", {});
let all = Ampify.storage.get("all", { songs: {} });
let current = null;  // { key, title, artist, heardMs, counted }

// The local date, so a day starts at midnight on this PC: "YYYY-MM-DD", which sorts like the dates.
function dayName(time) {
  const date = new Date(time);
  const two = (n) => String(n).padStart(2, "0");
  return `${date.getFullYear()}-${two(date.getMonth() + 1)}-${two(date.getDate())}`;
}
const today = () => dayName(Date.now());

function add(bucket, song, plays, ms, limit) {
  const entry = bucket.songs[song.key] || { title: song.title, artist: song.artist, plays: 0, ms: 0 };
  if (!bucket.songs[song.key] && Object.keys(bucket.songs).length >= limit) return;
  bucket.songs[song.key] = { ...entry, plays: entry.plays + plays, ms: entry.ms + ms };
}

function record(song, plays, ms) {
  const day = today();
  const bucket = days[day] || { songs: {} };
  add(bucket, song, plays, ms, MAX_SONGS_PER_DAY);
  add(all, song, plays, ms, MAX_SONGS_IN_ALL);
  days = { ...days, [day]: bucket };
  const oldest = dayName(Date.now() - KEEP_DAYS * 86400000);
  for (const name of Object.keys(days)) if (name < oldest) delete days[name];
  Ampify.storage.set("days", days);
  Ampify.storage.set("all", all);
}

function tick() {
  const state = Ampify.player.state();
  const track = state && state.track;
  if (!state || !state.playing || !track || !track.title) return;
  const key = track.id || track.title + "\n" + track.artist;
  if (!current || current.key !== key) current = { key, title: track.title, artist: track.artist || "", heardMs: 0, counted: false };
  current.heardMs += TICK_MS;
  const counts = !current.counted && current.heardMs >= PLAY_AFTER_MS;
  if (counts) current.counted = true;
  record(current, counts ? 1 : 0, TICK_MS);
}

// A new entry of the queue is a new play, even of the same song.
Ampify.player.on("trackchange", () => (current = null));

// The songs of a period, summed over its days.
function songsOf(period) {
  if (period === "all") return Object.values(all.songs);
  const count = period === "week" ? 7 : KEEP_DAYS;
  const first = dayName(Date.now() - (count - 1) * 86400000);
  const merged = {};
  for (const [day, bucket] of Object.entries(days)) {
    if (day < first) continue;
    for (const [key, song] of Object.entries(bucket.songs)) {
      const known = merged[key] || { title: song.title, artist: song.artist, plays: 0, ms: 0 };
      merged[key] = { ...known, plays: known.plays + song.plays, ms: known.ms + song.ms };
    }
  }
  return Object.values(merged);
}

function topArtists(songs) {
  const artists = {};
  for (const song of songs) {
    if (!song.artist) continue;
    const known = artists[song.artist] || { name: song.artist, plays: 0, ms: 0 };
    artists[song.artist] = { ...known, plays: known.plays + song.plays, ms: known.ms + song.ms };
  }
  return Object.values(artists).sort((a, b) => b.ms - a.ms).slice(0, TOP);
}

function duration(ms) {
  const minutes = Math.round(ms / 60000);
  if (minutes < 60) return t("minutes", { minutes });
  return t("hours", { hours: Math.floor(minutes / 60), minutes: minutes % 60 });
}

function Line({ name, detail }) {
  return h("row", null, h(Text, { grow: true, maxLines: 1 }, name), h(Text, { variant: "caption" }, detail));
}

function StatsPage() {
  const [period, setPeriod] = useState("week");
  const [, setStamp] = useState(0);
  useEffect(() => {
    const timer = setInterval(() => setStamp(Date.now()), 30000);  // the counts grow while the panel is open
    return () => clearInterval(timer);
  }, []);

  const songs = songsOf(period);
  const listened = songs.reduce((sum, song) => sum + song.ms, 0);
  const plays = songs.reduce((sum, song) => sum + song.plays, 0);
  const top = [...songs].filter((song) => song.plays > 0).sort((a, b) => b.plays - a.plays || b.ms - a.ms).slice(0, TOP);
  return h("stack", null,
    h(Text, { variant: "title" }, t("title")),
    h(Dropdown, {
      label: t("period"),
      value: period,
      options: ["week", "month", "all"].map((value) => ({ value, label: t(value) })),
      onChange: setPeriod,
    }),
    h(Card, { title: t("time"), subtitle: playCount(plays) }, h(Text, { variant: "title" }, duration(listened))),
    plays === 0
      ? h(Text, { wrap: true }, t("empty"))
      : h("stack", null,
          h(Card, { title: t("artists") },
            ...topArtists(songs).map((artist) => h(Line, { name: artist.name, detail: duration(artist.ms) }))),
          h(Card, { title: t("songs") },
            ...top.map((song) => h(Line, { name: song.title + " · " + song.artist, detail: playCount(song.plays) })))));
}

const timer = setInterval(tick, TICK_MS);
Ampify.onStop(() => clearInterval(timer));

// The app's "/listening-stats" page, and its entry at the bottom of the sidebar, a bar chart in the text colour.
globalThis.render = () => h(StatsPage);
const STATS_ICON = '<svg viewBox="0 0 24 24"><path d="M4 20h3V11H4zm6.5 0h3V4h-3zM17 20h3v-6h-3z"/></svg>';
Ampify.ui.sidebar.addItem({ label: t("title"), icon: STATS_ICON, onClick: () => Ampify.ui.navigate("/listening-stats") });
