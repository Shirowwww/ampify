// Listening statistics kept on this PC, like Spicetify's Stats: your listening time, plays, top artists, songs and
// albums of the last 7 days, the last 4 weeks or since the start, day by day and hour by hour. A song counts as a play
// after 30 seconds of listening, once per play. Nothing leaves the PC: the counts live in this element's storage.
const { h, useState, useEffect } = Ampify.React;
const { Card, Dropdown, Text } = Ampify.components;

Ampify.i18n.define({
  en: {
    title: "Listening stats", period: "Period", week: "Last 7 days", month: "Last 4 weeks", all: "Since the start",
    time: "Listening time", plays: "Plays", songs: "Songs", artists: "Artists", play: "1 play", playCount: "{count} plays",
    topArtists: "Top artists", topSongs: "Top songs", topAlbums: "Top albums", byDay: "Day by day",
    byHour: "Hour by hour", lastDays: "The last 28 days", empty: "Nothing yet: songs count after 30 seconds of listening.",
    hours: "{hours} h {minutes} min", minutes: "{minutes} min", noAlbums: "Albums are counted from now on.",
  },
  fr: {
    title: "Statistiques d'écoute", period: "Période", week: "7 derniers jours", month: "4 dernières semaines",
    all: "Depuis le début", time: "Temps d'écoute", plays: "Écoutes", songs: "Morceaux", artists: "Artistes",
    play: "1 écoute", playCount: "{count} écoutes", topArtists: "Artistes les plus écoutés",
    topSongs: "Morceaux les plus écoutés", topAlbums: "Albums les plus écoutés", byDay: "Jour par jour",
    byHour: "Heure par heure", lastDays: "Les 28 derniers jours",
    empty: "Rien pour l'instant : un morceau compte après 30 secondes d'écoute.", hours: "{hours} h {minutes} min",
    minutes: "{minutes} min", noAlbums: "Les albums sont comptés à partir de maintenant.",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);
const playCount = (count) => (count === 1 ? t("play") : t("playCount", { count }));

const TICK_MS = 5000;
const PLAY_AFTER_MS = 30000;
const KEEP_DAYS = 28;
const TOP = 8;
const TOP_ALBUMS = 5;
const MAX_SONGS_PER_DAY = 400;  // a day of non-stop listening is far below this
const MAX_SONGS_IN_ALL = 20000;  // keeps the storage small; past it, only songs already counted grow
const DAY_MS = 86400000;
const ACCENT = "#FA2D48";
const CHART_HEIGHT = 96;
const noHours = () => Array.from({ length: 24 }, () => 0);

// Storage: days["YYYY-MM-DD"] = { songs: { key: { title, artist, album, plays, ms } }, hours: [24 ms] }, the 28 last
// days; all = { songs: {...}, hours: [24 ms] }, since the start. Days kept before 0.2 have no hours nor albums.
let days = Ampify.storage.get("days", {});
let all = Ampify.storage.get("all", { songs: {} });
let current = null;  // { key, title, artist, album, heardMs, counted }

// The local date, so a day starts at midnight on this PC: "YYYY-MM-DD", which sorts like the dates.
function dayName(time) {
  const date = new Date(time);
  const two = (n) => String(n).padStart(2, "0");
  return `${date.getFullYear()}-${two(date.getMonth() + 1)}-${two(date.getDate())}`;
}

function add(bucket, song, plays, ms, limit) {
  const known = bucket.songs[song.key];
  if (!known && Object.keys(bucket.songs).length >= limit) return bucket;
  const entry = known || { title: song.title, artist: song.artist, album: song.album, plays: 0, ms: 0 };
  const hours = (bucket.hours || noHours()).map((value, hour) => (hour === new Date().getHours() ? value + ms : value));
  return {
    ...bucket,
    songs: { ...bucket.songs, [song.key]: { ...entry, album: song.album || entry.album, plays: entry.plays + plays, ms: entry.ms + ms } },
    hours,
  };
}

function record(song, plays, ms) {
  const day = dayName(Date.now());
  const kept = {};
  const oldest = dayName(Date.now() - KEEP_DAYS * DAY_MS);
  for (const [name, bucket] of Object.entries(days)) if (name >= oldest) kept[name] = bucket;
  days = { ...kept, [day]: add(days[day] || { songs: {} }, song, plays, ms, MAX_SONGS_PER_DAY) };
  all = add(all, song, plays, ms, MAX_SONGS_IN_ALL);
  Ampify.storage.set("days", days);
  Ampify.storage.set("all", all);
}

function tick() {
  const state = Ampify.player.state();
  const track = state && state.track;
  if (!state || !state.playing || !track || !track.title) return;
  const key = track.id || track.title + "\n" + track.artist;
  if (!current || current.key !== key) {
    current = { key, title: track.title, artist: track.artist || "", album: track.album || "", heardMs: 0, counted: false };
  }
  current.heardMs += TICK_MS;
  const counts = !current.counted && current.heardMs >= PLAY_AFTER_MS;
  if (counts) current.counted = true;
  record(current, counts ? 1 : 0, TICK_MS);
}

// A new entry of the queue is a new play, even of the same song.
Ampify.player.on("trackchange", () => (current = null));

// --- Sums over a period ----------------------------------------------------------------------------------------------

// The days of a period, newest last: the 7 or 28 last, and for "all" the 28 last (only they are kept day by day).
function dayNames(period) {
  const count = period === "week" ? 7 : KEEP_DAYS;
  return Array.from({ length: count }, (_, index) => dayName(Date.now() - (count - 1 - index) * DAY_MS));
}

function summaryOf(period) {
  if (period === "all") return { songs: Object.values(all.songs), hours: all.hours || noHours() };
  const merged = {};
  const hours = noHours();
  for (const name of dayNames(period)) {
    const bucket = days[name];
    if (!bucket) continue;
    (bucket.hours || []).forEach((ms, hour) => (hours[hour] += ms));
    for (const [key, song] of Object.entries(bucket.songs)) {
      const known = merged[key] || { ...song, plays: 0, ms: 0 };
      merged[key] = { ...known, plays: known.plays + song.plays, ms: known.ms + song.ms };
    }
  }
  return { songs: Object.values(merged), hours };
}

// The songs grouped by one field (artist or album), the most listened first.
function groupBy(songs, field) {
  const groups = {};
  for (const song of songs) {
    const name = song[field];
    if (!name) continue;
    const known = groups[name] || { name, artist: song.artist, plays: 0, ms: 0 };
    groups[name] = { ...known, plays: known.plays + song.plays, ms: known.ms + song.ms };
  }
  return Object.values(groups).sort((a, b) => b.ms - a.ms).slice(0, TOP);
}

function duration(ms) {
  const minutes = Math.round(ms / 60000);
  if (minutes < 60) return t("minutes", { minutes });
  return t("hours", { hours: Math.floor(minutes / 60), minutes: minutes % 60 });
}

// --- The page --------------------------------------------------------------------------------------------------------

function Tile({ value, label }) {
  return h(Card, { grow: true, padding: 16 },
    h(Text, { size: 30, weight: 700, maxLines: 1 }, value),
    h(Text, { variant: "caption" }, label));
}

// A column chart: one node per bar, and a few labels under it, each over an equal share of the bars (the interface
// takes 300 nodes at most).
function Bars({ values, labels }) {
  const highest = Math.max(1, ...values);
  return h("stack", { gap: 4 },
    h("row", { gap: 4, height: CHART_HEIGHT },
      ...values.map((value, index) => h("stack", {
        key: String(index),
        grow: true,
        valign: "end",
        height: Math.max(2, Math.round((value / highest) * CHART_HEIGHT)),
        background: value > 0 ? ACCENT : "#33FFFFFF",
        cornerRadius: 3,
      }))),
    h("row", null, ...labels.map((label) => h(Text, { key: label, grow: true, size: 11, maxLines: 1 }, label))));
}

function Ranked({ rank, name, detail, note }) {
  return h("row", { gap: 12 },
    h(Text, { size: 15, weight: 700, color: ACCENT }, String(rank)),
    h("stack", { grow: true },
      h(Text, { size: 15, weight: 600, maxLines: 1 }, name),
      note ? h(Text, { variant: "caption", maxLines: 1 }, note) : null),
    h(Text, { variant: "caption" }, detail));
}

function Charts({ period, hours }) {
  const names = dayNames(period);
  const perDay = names.map((name) => {
    const bucket = days[name];
    return bucket ? Object.values(bucket.songs).reduce((sum, song) => sum + song.ms, 0) : 0;
  });
  // The week: each day under its bar; 4 weeks: the first day of each week. Hours: every 6.
  const dayLabels = names.filter((_, index) => period === "week" || index % 7 === 0).map((name) => name.slice(8));
  const hourLabels = ["0 h", "6 h", "12 h", "18 h"];
  return h("row", { gap: 12 },
    h(Card, { title: t("byDay"), subtitle: period === "week" ? undefined : t("lastDays"), grow: true },
      h(Bars, { values: perDay, labels: dayLabels })),
    h(Card, { title: t("byHour"), grow: true }, h(Bars, { values: hours, labels: hourLabels })));
}

function Tops({ songs }) {
  const artists = groupBy(songs, "artist");
  const albums = groupBy(songs, "album").slice(0, TOP_ALBUMS);
  const top = songs.filter((song) => song.plays > 0).sort((a, b) => b.plays - a.plays || b.ms - a.ms).slice(0, TOP);
  return h("stack", { gap: 12 },
    h("row", { gap: 12 },
      h(Card, { title: t("topArtists"), grow: true, valign: "start" },  // the top of the row, not its middle
        ...artists.map((artist, index) => h(Ranked, { rank: index + 1, name: artist.name, detail: duration(artist.ms) }))),
      h(Card, { title: t("topSongs"), grow: true, valign: "start" },
        ...top.map((song, index) => h(Ranked, { rank: index + 1, name: song.title, note: song.artist, detail: playCount(song.plays) })))),
    h(Card, { title: t("topAlbums") },
      ...(albums.length === 0
        ? [h(Text, { variant: "caption", wrap: true }, t("noAlbums"))]
        : albums.map((album, index) => h(Ranked, { rank: index + 1, name: album.name, note: album.artist, detail: duration(album.ms) })))));
}

function StatsPage() {
  const [period, setPeriod] = useState("week");
  const [, setStamp] = useState(0);
  useEffect(() => {
    const timer = setInterval(() => setStamp(Date.now()), 30000);  // the counts grow while the page is open
    return () => clearInterval(timer);
  }, []);

  const { songs, hours } = summaryOf(period);
  const listened = songs.reduce((sum, song) => sum + song.ms, 0);
  const plays = songs.reduce((sum, song) => sum + song.plays, 0);
  const artistCount = new Set(songs.map((song) => song.artist).filter(Boolean)).size;
  // The page's header already shows the app's name: the view starts with the period.
  return h("stack", { gap: 16, padding: "0,0,24,0" },
    h("row", null, h(Dropdown, {
      label: t("period"),
      value: period,
      options: ["week", "month", "all"].map((value) => ({ value, label: t(value) })),
      onChange: setPeriod,
    })),
    h("row", { gap: 12 },
      h(Tile, { value: duration(listened), label: t("time") }),
      h(Tile, { value: String(plays), label: t("plays") }),
      h(Tile, { value: String(songs.filter((song) => song.plays > 0).length), label: t("songs") }),
      h(Tile, { value: String(artistCount), label: t("artists") })),
    plays === 0
      ? h(Text, { size: 15, wrap: true }, t("empty"))
      : h("stack", { gap: 12 }, h(Charts, { period, hours }), h(Tops, { songs })));
}

const timer = setInterval(tick, TICK_MS);
Ampify.onStop(() => clearInterval(timer));

// The app's "/listening-stats" page, and its entry at the bottom of the sidebar: four rounded bars of a chart, in
// the text colour (icons take paths only).
globalThis.render = () => h(StatsPage);
const STATS_ICON = '<svg viewBox="0 0 24 24"><path d="' +
  "M2.25 14.5a1.5 1.5 0 0 1 3 0v4a1.5 1.5 0 0 1-3 0z" +
  "M7.75 8.5a1.5 1.5 0 0 1 3 0v10a1.5 1.5 0 0 1-3 0z" +
  "M13.25 11.5a1.5 1.5 0 0 1 3 0v7a1.5 1.5 0 0 1-3 0z" +
  "M18.75 5.5a1.5 1.5 0 0 1 3 0v13a1.5 1.5 0 0 1-3 0z" +
  '"/></svg>';
Ampify.ui.sidebar.addItem({ label: t("title"), icon: STATS_ICON, onClick: () => Ampify.ui.navigate("/listening-stats") });
