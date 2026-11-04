// Listening statistics kept on this PC, like Spicetify's Stats: your listening time, plays, top artists, songs and
// albums of the last 7 days, the last 4 weeks or since the start, day by day and hour by hour. A song counts as a play
// after 30 seconds of listening, once per play. The counts stay on the PC, in this element's storage; the page of an
// artist, album or song asks Apple's public search for its artwork and its Apple Music link.
const { h, Fragment, useState, useEffect } = Ampify.React;
const { Button, Card, Dropdown, Text } = Ampify.components;

Ampify.i18n.define({
  en: {
    title: "Listening stats", period: "Period", week: "Last 7 days (this device)", month: "Last 4 weeks (this device)",
    all: "Since the start (this device)", time: "Listening time", plays: "Plays", songs: "Songs", artists: "Artists",
    play: "1 play", playCount: "{count} plays", topArtists: "Top artists", topSongs: "Top songs", topAlbums: "Top albums",
    byDay: "Day by day", byHour: "Hour by hour", lastDays: "The last 28 days",
    empty: "Nothing yet: songs count after 30 seconds of listening.", hours: "{hours} h {minutes} min",
    minutes: "{minutes} min", noAlbums: "Albums are counted from now on.", openDetails: "See its stats",
    openInApple: "Open in Apple Music", notFound: "Not found on Apple Music.",
    "kind.artist": "Artist", "kind.song": "Song", "kind.album": "Album",
    library: "All plays (Apple Music)", reading: "Reading your library…",
    libraryError: "Apple Music's library could not be read. The other periods show what plays on this device.",
    libraryNote: "Apple Music's own play counts, every device included; the other periods count what plays on this device.",
    deviceNote: "Only what plays on this device, counted by Ampify: a song counts after 30 seconds. For every device, choose All plays (Apple Music).",
  },
  fr: {
    title: "Statistiques d'écoute", period: "Période", week: "7 derniers jours (cet appareil)",
    month: "4 dernières semaines (cet appareil)", all: "Depuis le début (cet appareil)", time: "Temps d'écoute",
    plays: "Écoutes", songs: "Morceaux", artists: "Artistes", play: "1 écoute", playCount: "{count} écoutes",
    topArtists: "Artistes les plus écoutés", topSongs: "Morceaux les plus écoutés", topAlbums: "Albums les plus écoutés",
    byDay: "Jour par jour", byHour: "Heure par heure", lastDays: "Les 28 derniers jours",
    empty: "Rien pour l'instant : un morceau compte après 30 secondes d'écoute.", hours: "{hours} h {minutes} min",
    minutes: "{minutes} min", noAlbums: "Les albums sont comptés à partir de maintenant.",
    openDetails: "Voir ses statistiques", openInApple: "Ouvrir dans Apple Music", notFound: "Introuvable sur Apple Music.",
    "kind.artist": "Artiste", "kind.song": "Morceau", "kind.album": "Album",
    library: "Toutes les écoutes (Apple Music)", reading: "Lecture de votre bibliothèque…",
    libraryError: "La bibliothèque d'Apple Music n'a pas pu être lue. Les autres périodes montrent ce qui joue sur cet appareil.",
    libraryNote: "Les compteurs d'Apple Music, tous vos appareils compris ; les autres périodes comptent ce qui joue sur cet appareil.",
    deviceNote: "Seulement ce qui joue sur cet appareil, compté par Ampify : un morceau compte après 30 secondes. Pour tous vos appareils, choisissez Toutes les écoutes (Apple Music).",
  },
  de: {
    title: "Hörstatistik", period: "Zeitraum", week: "Letzte 7 Tage (dieses Gerät)", month: "Letzte 4 Wochen (dieses Gerät)",
    all: "Seit Beginn (dieses Gerät)", time: "Hörzeit", plays: "Wiedergaben", songs: "Titel", artists: "Künstler",
    play: "1 Wiedergabe", playCount: "{count} Wiedergaben", topArtists: "Meistgehörte Künstler",
    topSongs: "Meistgehörte Titel", topAlbums: "Meistgehörte Alben", byDay: "Nach Tag", byHour: "Nach Stunde",
    lastDays: "Die letzten 28 Tage", empty: "Noch nichts: Ein Titel zählt nach 30 Sekunden.",
    hours: "{hours} Std. {minutes} Min.", minutes: "{minutes} Min.", noAlbums: "Alben werden ab jetzt gezählt.",
    openDetails: "Statistik ansehen", openInApple: "In Apple Music öffnen", notFound: "Nicht in Apple Music gefunden.",
    "kind.artist": "Künstler", "kind.song": "Titel", "kind.album": "Album",
    library: "Alle Wiedergaben (Apple Music)", reading: "Mediathek wird gelesen …",
    libraryError: "Die Mediathek von Apple Music konnte nicht gelesen werden. Die anderen Zeiträume zeigen, was auf diesem Gerät läuft.",
    libraryNote: "Die Zähler von Apple Music, alle Geräte inbegriffen; die anderen Zeiträume zählen, was auf diesem Gerät läuft.",
    deviceNote: "Nur, was auf diesem Gerät läuft, von Ampify gezählt: Ein Titel zählt nach 30 Sekunden. Für alle Geräte „Alle Wiedergaben (Apple Music)“ wählen.",
  },
  es: {
    title: "Estadísticas de escucha", period: "Periodo", week: "Últimos 7 días (este dispositivo)",
    month: "Últimas 4 semanas (este dispositivo)", all: "Desde el principio (este dispositivo)",
    time: "Tiempo de escucha", plays: "Reproducciones", songs: "Canciones", artists: "Artistas", play: "1 reproducción",
    playCount: "{count} reproducciones", topArtists: "Artistas más escuchados", topSongs: "Canciones más escuchadas",
    topAlbums: "Álbumes más escuchados", byDay: "Por día", byHour: "Por hora", lastDays: "Los últimos 28 días",
    empty: "Nada todavía: una canción cuenta tras 30 segundos de escucha.", hours: "{hours} h {minutes} min",
    minutes: "{minutes} min", noAlbums: "Los álbumes se cuentan a partir de ahora.", openDetails: "Ver sus estadísticas",
    openInApple: "Abrir en Apple Music", notFound: "No encontrado en Apple Music.",
    "kind.artist": "Artista", "kind.song": "Canción", "kind.album": "Álbum",
    library: "Todas las reproducciones (Apple Music)", reading: "Leyendo tu biblioteca…",
    libraryError: "No se pudo leer la biblioteca de Apple Music. Los demás periodos muestran lo que suena en este dispositivo.",
    libraryNote: "Los contadores de Apple Music, todos tus dispositivos incluidos; los otros periodos cuentan lo que suena en este dispositivo.",
    deviceNote: "Solo lo que suena en este dispositivo, contado por Ampify: una canción cuenta tras 30 segundos. Para todos tus dispositivos, elige Todas las reproducciones (Apple Music).",
  },
  pt: {
    title: "Estatísticas de audição", period: "Período", week: "Últimos 7 dias (este dispositivo)",
    month: "Últimas 4 semanas (este dispositivo)", all: "Desde o início (este dispositivo)", time: "Tempo de audição",
    plays: "Reproduções", songs: "Músicas", artists: "Artistas", play: "1 reprodução", playCount: "{count} reproduções",
    topArtists: "Artistas mais ouvidos", topSongs: "Músicas mais ouvidas", topAlbums: "Álbuns mais ouvidos",
    byDay: "Por dia", byHour: "Por hora", lastDays: "Os últimos 28 dias",
    empty: "Nada ainda: uma música conta após 30 segundos de audição.", hours: "{hours} h {minutes} min",
    minutes: "{minutes} min", noAlbums: "Os álbuns são contados a partir de agora.", openDetails: "Ver as estatísticas",
    openInApple: "Abrir no Apple Music", notFound: "Não encontrado no Apple Music.",
    "kind.artist": "Artista", "kind.song": "Música", "kind.album": "Álbum",
    library: "Todas as reproduções (Apple Music)", reading: "Lendo sua biblioteca…",
    libraryError: "Não foi possível ler a biblioteca do Apple Music. Os outros períodos mostram o que toca neste dispositivo.",
    libraryNote: "Os contadores do Apple Music, todos os seus dispositivos incluídos; os outros períodos contam o que toca neste dispositivo.",
    deviceNote: "Só o que toca neste dispositivo, contado pelo Ampify: uma música conta após 30 segundos. Para todos os dispositivos, escolha Todas as reproduções (Apple Music).",
  },
  ru: {
    title: "Статистика прослушиваний", period: "Период", week: "Последние 7 дней (это устройство)",
    month: "Последние 4 недели (это устройство)", all: "С самого начала (это устройство)",
    time: "Время прослушивания", plays: "Прослушивания", songs: "Песни", artists: "Исполнители",
    play: "1 прослушивание", playCount: "Прослушиваний: {count}", topArtists: "Самые слушаемые исполнители",
    topSongs: "Самые слушаемые песни", topAlbums: "Самые слушаемые альбомы", byDay: "По дням", byHour: "По часам",
    lastDays: "Последние 28 дней", empty: "Пока ничего: песня засчитывается после 30 секунд прослушивания.",
    hours: "{hours} ч {minutes} мин", minutes: "{minutes} мин", noAlbums: "Альбомы считаются с этого момента.",
    openDetails: "Открыть статистику", openInApple: "Открыть в Apple Music", notFound: "Не найдено в Apple Music.",
    "kind.artist": "Исполнитель", "kind.song": "Песня", "kind.album": "Альбом",
    library: "Все прослушивания (Apple Music)", reading: "Чтение медиатеки…",
    libraryError: "Не удалось прочитать медиатеку Apple Music. Другие периоды показывают то, что играет на этом устройстве.",
    libraryNote: "Счётчики Apple Music со всех ваших устройств; остальные периоды считают то, что играет на этом устройстве.",
    deviceNote: "Только то, что играет на этом устройстве, по подсчёту Ampify: песня засчитывается после 30 секунд. Для всех устройств выберите «Все прослушивания (Apple Music)».",
  },
  ja: {
    title: "再生の統計", period: "期間", week: "過去7日間（このデバイス）", month: "過去4週間（このデバイス）",
    all: "最初から（このデバイス）", time: "再生時間", plays: "再生回数", songs: "曲", artists: "アーティスト",
    play: "1回", playCount: "{count}回", topArtists: "よく聴くアーティスト", topSongs: "よく聴く曲",
    topAlbums: "よく聴くアルバム", byDay: "日別", byHour: "時間別", lastDays: "過去28日間",
    empty: "まだありません：30秒聴くと1回と数えます。", hours: "{hours}時間{minutes}分", minutes: "{minutes}分",
    noAlbums: "アルバムはこれから数えます。", openDetails: "統計を見る", openInApple: "Apple Musicで開く",
    notFound: "Apple Musicで見つかりません。", "kind.artist": "アーティスト", "kind.song": "曲", "kind.album": "アルバム",
    library: "すべての再生（Apple Music）", reading: "ライブラリを読み込んでいます…",
    libraryError: "Apple Music のライブラリを読み込めませんでした。ほかの期間では、このデバイスで再生した曲を表示します。",
    libraryNote: "Apple Music自体の再生回数で、すべてのデバイスを含みます。ほかの期間はこのデバイスで再生したものを数えます。",
    deviceNote: "このデバイスで再生したものだけをAmpifyが数えます（30秒で1回）。すべてのデバイスは「すべての再生（Apple Music）」を選んでください。",
  },
  zh: {
    title: "收听统计", period: "时间段", week: "最近 7 天（此设备）", month: "最近 4 周（此设备）",
    all: "从一开始（此设备）", time: "收听时长", plays: "播放次数", songs: "歌曲", artists: "艺人", play: "1 次播放",
    playCount: "{count} 次播放", topArtists: "最常听的艺人", topSongs: "最常听的歌曲", topAlbums: "最常听的专辑",
    byDay: "按天", byHour: "按小时", lastDays: "最近 28 天", empty: "还没有记录：收听 30 秒后计为一次播放。",
    hours: "{hours} 小时 {minutes} 分钟", minutes: "{minutes} 分钟", noAlbums: "专辑从现在开始计数。",
    openDetails: "查看统计", openInApple: "在 Apple Music 中打开", notFound: "在 Apple Music 中未找到。",
    "kind.artist": "艺人", "kind.song": "歌曲", "kind.album": "专辑",
    library: "全部播放（Apple Music）", reading: "正在读取资料库…",
    libraryError: "无法读取 Apple Music 资料库。其他时段显示在此设备上播放的内容。",
    libraryNote: "Apple Music 自己的播放次数，包含你所有的设备；其他时间段统计在此设备上播放的内容。",
    deviceNote: "仅统计 Ampify 在此设备上记录的播放：收听 30 秒计为一次。如需所有设备，请选择“全部播放（Apple Music）”。",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);
const playCount = (count) => (count === 1 ? t("play") : t("playCount", { count }));

const TICK_MS = 5000;
const PLAY_AFTER_MS = 30000;
const RESTART_MS = 15000;  // a song back under this position, from further on, is played again
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
  const position = state.positionMs || 0;
  // Back at its start without a new queue entry (repeat one, or Previous): Apple sends no track change, yet it is a
  // new play.
  const restarted = current && current.key === key && position + TICK_MS < current.positionMs && position < RESTART_MS;
  if (!current || current.key !== key || restarted) {
    current = { key, title: track.title, artist: track.artist || "", album: track.album || "", heardMs: 0, counted: false };
  }
  current.positionMs = position;
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

// Apple Music's own counts, from its library: every play it knows of, on every device that syncs it. Read once a
// page opens, then kept; null until read, [] when the library cannot be read. Pages opened during the read share it.
let library = null;
let libraryFailed = false;
let libraryRead = null;

function readLibrary(done) {
  if (library !== null) return done();
  libraryRead = libraryRead || Ampify.library.tracks().then(
    (tracks) => {
      library = tracks.filter((track) => track.playCount > 0).map((track) => ({
        title: track.title, artist: track.artist, album: track.album,
        plays: track.playCount, ms: track.playCount * (track.durationMs || 0),
      }));
    },
    (error) => {
      Ampify.log("library not read: " + error);
      library = [];
      libraryFailed = true;
    });
  libraryRead.then(done);
}

function summaryOf(period) {
  if (period === "library") return { songs: library || [], hours: null };
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

// One line of a top: its rank, the name with the artist right under it, and its count. A click opens its page.
function Ranked({ rank, name, detail, note, onClick }) {
  return h(Card, { padding: "8,6,8,6", onClick, tooltip: t("openDetails") },
    h("row", { gap: 12 },
      h(Text, { size: 15, weight: 700, color: ACCENT, width: 20, valign: "center" }, String(rank)),
      h("stack", { grow: true, gap: 0, valign: "center" },
        h(Text, { size: 15, weight: 600, maxLines: 1 }, name),
        note ? h(Text, { variant: "caption", maxLines: 1 }, note) : null),
      h(Text, { variant: "caption", valign: "center", wrap: false }, detail)));
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
  // A fragment, not a stack: a clickable line of a top is already deep, and the interface takes 8 levels at most.
  return h(Fragment, null,
    h("row", { gap: 12 },
      h(Card, { title: t("topArtists"), grow: true, valign: "start" },  // the top of the row, not its middle
        ...artists.map((artist, index) => h(Ranked, { key: artist.name, rank: index + 1, name: artist.name, detail: duration(artist.ms),
                                                     onClick: () => openDetail({ kind: "artist", name: artist.name }) }))),
      h(Card, { title: t("topSongs"), grow: true, valign: "start" },
        ...top.map((song, index) => h(Ranked, { key: song.title + song.artist, rank: index + 1, name: song.title, note: song.artist,
                                               detail: playCount(song.plays),
                                               onClick: () => openDetail({ kind: "song", name: song.title, artist: song.artist }) })))),
    h(Card, { title: t("topAlbums") },
      ...(albums.length === 0
        ? [h(Text, { variant: "caption", wrap: true }, t("noAlbums"))]
        : albums.map((album, index) => h(Ranked, { key: album.name, rank: index + 1, name: album.name, note: album.artist,
                                                  detail: duration(album.ms),
                                                  onClick: () => openDetail({ kind: "album", name: album.name, artist: album.artist }) })))));
}

// --- The page of one artist, song or album ---------------------------------------------------------------------------

const DETAIL_PAGE = "/listening-stats/item";
const ITUNES = "https://itunes.apple.com/";
const APPLE_MUSIC = "https://music.apple.com/";
let detail = null;    // { kind, name, artist } of the page shown
let lastPeriod = "library";  // the list's period, kept when the arrow brings it back

function openDetail(target) {
  detail = target;
  Ampify.ui.navigate(DETAIL_PAGE);
}

// The store of the interface's language: the US one found other artists, and no song, for a French rapper.
const STOREFRONTS = { fr: "fr", de: "de", es: "es", pt: "br", ru: "ru", ja: "jp", zh: "cn" };
const storefront = () => STOREFRONTS[Ampify.i18n.language] || "us";

// The iTunes search's hits ("search?term=..." or "lookup?id=..."), [] when the network or the reply fails.
async function search(query, entity) {
  const reply = await Ampify.http.get(ITUNES + query + "&entity=" + entity + "&limit=5&country=" + storefront());
  if (reply.status !== 200) return [];
  try {
    return JSON.parse(reply.text).results || [];
  } catch {
    return [];
  }
}

const same = (a, b) => String(a || "").toLowerCase() === String(b || "").toLowerCase();
const largeCover = (hit) => (hit && hit.artworkUrl100 ? hit.artworkUrl100.replace("100x100bb", "300x300bb") : "");

// Its Apple Music page and artwork, found by the iTunes search: null when nothing matches or the network fails.
async function findOnAppleMusic(target) {
  const entity = { artist: "musicArtist", song: "song", album: "album" }[target.kind];
  const results = await search("search?term=" + encodeURIComponent([target.name, target.artist].filter(Boolean).join(" ")), entity);
  const nameOf = (hit) => (target.kind === "artist" ? hit.artistName : target.kind === "album" ? hit.collectionName : hit.trackName);
  const hit = results.find((candidate) => same(nameOf(candidate), target.name)) || results[0];
  if (!hit) return null;
  const url = hit.trackViewUrl || hit.collectionViewUrl || hit.artistLinkUrl || hit.artistViewUrl || "";
  let cover = largeCover(hit);
  // The search gives an artist no picture: the cover of one of their own albums stands in (a feature's otherwise),
  // rather than an empty page. Their songs came first as features of others.
  if (!cover && target.kind === "artist" && hit.artistId) {
    const albums = (await search("lookup?id=" + hit.artistId, "album")).filter((album) => album.wrapperType === "collection");
    cover = largeCover(albums.find((album) => same(album.artistName, hit.artistName)) || albums[0]);
  }
  return { url: url.startsWith(APPLE_MUSIC) ? url : "", cover };
}

// The songs of the period that belong to the target, the most played first.
function songsOf(target, songs) {
  const mine = songs.filter((song) =>
    target.kind === "artist" ? song.artist === target.name
      : target.kind === "album" ? song.album === target.name
        : song.title === target.name && song.artist === target.artist);
  return mine.sort((a, b) => b.plays - a.plays || b.ms - a.ms);
}

function DetailPage() {
  const target = detail;
  const [found, setFound] = useState(undefined);  // undefined while it is searched
  useEffect(() => {
    if (!target) return;
    findOnAppleMusic(target).then(setFound, () => setFound(null));
  }, []);
  if (!target) return h(Text, null, t("empty"));

  const { songs } = summaryOf(lastPeriod);
  const mine = songsOf(target, songs);
  const listened = mine.reduce((sum, song) => sum + song.ms, 0);
  const plays = mine.reduce((sum, song) => sum + song.plays, 0);
  const subtitle = [t("kind." + target.kind), target.artist, t(lastPeriod)].filter(Boolean).join(" · ");
  return h("stack", { gap: 16, padding: "0,0,24,0" },
    h("row", { gap: 20 },
      found && found.cover
        ? h("image", { src: found.cover, label: target.name, width: 160, height: 160, cornerRadius: target.kind === "artist" ? 80 : 8, fit: "cover" })
        : null,
      h("stack", { gap: 6, grow: true, valign: "center" },
        h(Text, { variant: "title", maxLines: 2 }, target.name),
        h(Text, { variant: "caption" }, subtitle),
        h("row", { gap: 8 },
          h(Button, { label: t("openInApple"), disabled: !(found && found.url), onClick: () => Ampify.http.open(found.url) }),
          h("stack", { grow: true })),
        found === null ? h(Text, { variant: "caption" }, t("notFound")) : null)),
    h("row", { gap: 12 },
      h(Tile, { value: duration(listened), label: t("time") }),
      h(Tile, { value: String(plays), label: t("plays") }),
      target.kind === "song" ? null : h(Tile, { value: String(mine.length), label: t("songs") })),
    target.kind === "song"
      ? null
      : h(Card, { title: t("topSongs") },
          ...mine.slice(0, TOP * 2).map((song, index) => h(Ranked, {
            key: song.title + song.artist, rank: index + 1, name: song.title,
            note: target.kind === "album" ? "" : song.album, detail: playCount(song.plays),
            onClick: () => openDetail({ kind: "song", name: song.title, artist: song.artist }) }))));
}
Ampify.ReactDOM.render(h(DetailPage), { page: DETAIL_PAGE });

function StatsPage() {
  const [period, setPeriodState] = useState(lastPeriod);
  const setPeriod = (value) => {
    lastPeriod = value;
    setPeriodState(value);
  };
  const [, setStamp] = useState(0);
  useEffect(() => {
    readLibrary(() => setStamp(Date.now()));
    const timer = setInterval(() => setStamp(Date.now()), 30000);  // the counts grow while the page is open
    return () => clearInterval(timer);
  }, []);
  if (period === "library" && library === null) return h(Text, { variant: "caption" }, t("reading"));

  const { songs, hours } = summaryOf(period);
  const listened = songs.reduce((sum, song) => sum + song.ms, 0);
  const plays = songs.reduce((sum, song) => sum + song.plays, 0);
  const artistCount = new Set(songs.map((song) => song.artist).filter(Boolean)).size;
  // The page's header already shows the app's name: the view starts with the period.
  return h("stack", { gap: 16, padding: "0,0,24,0" },
    h("row", null, h(Dropdown, {
      label: t("period"),
      value: period,
      options: ["library", "week", "month", "all"].map((value) => ({ value, label: t(value) })),
      onChange: setPeriod,
    })),
    h(Text, { variant: "caption", wrap: true }, t(period === "library" ? "libraryNote" : "deviceNote")),
    h("row", { gap: 12 },
      h(Tile, { value: duration(listened), label: t("time") }),
      h(Tile, { value: String(plays), label: t("plays") }),
      h(Tile, { value: String(songs.filter((song) => song.plays > 0).length), label: t("songs") }),
      h(Tile, { value: String(artistCount), label: t("artists") })),
    plays === 0
      ? h(Text, { size: 15, wrap: true }, t(period === "library" && libraryFailed ? "libraryError" : "empty"))
      : h(Fragment, null, hours ? h(Charts, { period, hours }) : null, h(Tops, { songs })));
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
