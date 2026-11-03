// New releases from the artists you listen to, like Spicetify's New Releases. It counts the artist of each song you
// play and keeps the 50 you hear most. Once a day, and when the page opens with data older than 12 hours, it asks
// Apple's public iTunes API for their recent albums (no account, no server of Ampify) and lists the ones released in
// the last 30, 60 or 90 days on the page "/new-releases". Clicking a release opens its Apple Music page.
//
// An app has no settings in the Ampify card, so the period and "include singles" are on the page; they live in storage.
const { h, useState, useEffect } = Ampify.React;
const { Card, Dropdown, Toggle, Text } = Ampify.components;

Ampify.i18n.define({
  en: {
    title: "New releases", period: "Period", days: "Last {days} days", singles: "Include singles", checking: "Checking…",
    empty: "No artist yet. Listen to some music: the artists you play are checked for new releases.",
    none: "Nothing new from your artists in the last {days} days.", single: "Single", ep: "EP", album: "Album",
    checked: "Checked {hours} h ago", checkedNow: "Checked just now", failed: "Apple's catalog did not answer. Try again later.",
  },
  fr: {
    title: "Nouveautés", period: "Période", days: "{days} derniers jours", singles: "Inclure les singles", checking: "Recherche…",
    empty: "Aucun artiste pour l'instant. Écoutez de la musique : les artistes que vous jouez sont vérifiés.",
    none: "Rien de nouveau de vos artistes ces {days} derniers jours.", single: "Single", ep: "EP", album: "Album",
    checked: "Vérifié il y a {hours} h", checkedNow: "Vérifié à l'instant", failed: "Le catalogue d'Apple n'a pas répondu. Réessayez plus tard.",
  },
  de: {
    title: "Neuerscheinungen", period: "Zeitraum", days: "Letzte {days} Tage", singles: "Singles einbeziehen", checking: "Suche…",
    empty: "Noch kein Künstler. Hören Sie etwas Musik: Die Künstler, die Sie spielen, werden auf Neuerscheinungen geprüft.",
    none: "Nichts Neues von Ihren Künstlern in den letzten {days} Tagen.", single: "Single", ep: "EP", album: "Album",
    checked: "Vor {hours} Std. geprüft", checkedNow: "Gerade geprüft", failed: "Der Apple-Katalog hat nicht geantwortet. Später erneut versuchen.",
  },
  es: {
    title: "Novedades", period: "Periodo", days: "Últimos {days} días", singles: "Incluir sencillos", checking: "Buscando…",
    empty: "Aún no hay artistas. Escucha música: se buscan novedades de los artistas que reproduces.",
    none: "Nada nuevo de tus artistas en los últimos {days} días.", single: "Sencillo", ep: "EP", album: "Álbum",
    checked: "Revisado hace {hours} h", checkedNow: "Revisado ahora mismo", failed: "El catálogo de Apple no respondió. Inténtalo más tarde.",
  },
  pt: {
    title: "Lançamentos", period: "Período", days: "Últimos {days} dias", singles: "Incluir singles", checking: "Procurando…",
    empty: "Ainda não há artistas. Ouça música: os artistas que você toca são verificados em busca de lançamentos.",
    none: "Nada novo dos seus artistas nos últimos {days} dias.", single: "Single", ep: "EP", album: "Álbum",
    checked: "Verificado há {hours} h", checkedNow: "Verificado agora", failed: "O catálogo da Apple não respondeu. Tente de novo mais tarde.",
  },
  ru: {
    title: "Новинки", period: "Период", days: "Последние {days} дн.", singles: "Включать синглы", checking: "Проверка…",
    empty: "Пока нет исполнителей. Послушайте музыку: новинки проверяются у тех, кого вы слушаете.",
    none: "У ваших исполнителей ничего нового за последние {days} дн.", single: "Сингл", ep: "EP", album: "Альбом",
    checked: "Проверено {hours} ч назад", checkedNow: "Проверено только что", failed: "Каталог Apple не ответил. Попробуйте позже.",
  },
  ja: {
    title: "新作リリース", period: "期間", days: "過去 {days} 日", singles: "シングルを含める", checking: "確認中…",
    empty: "まだアーティストがいません。音楽を聴くと、再生したアーティストの新作を確認します。",
    none: "過去 {days} 日間、あなたのアーティストの新作はありません。", single: "シングル", ep: "EP", album: "アルバム",
    checked: "{hours} 時間前に確認", checkedNow: "たった今確認", failed: "Apple のカタログから応答がありません。後でもう一度お試しください。",
  },
  zh: {
    title: "新发行", period: "时间范围", days: "最近 {days} 天", singles: "包含单曲", checking: "检查中…",
    empty: "还没有艺术家。先听些音乐：我们会检查你常听艺术家的新发行。",
    none: "最近 {days} 天你的艺术家没有新发行。", single: "单曲", ep: "EP", album: "专辑",
    checked: "{hours} 小时前检查", checkedNow: "刚刚检查", failed: "Apple 目录没有响应，请稍后再试。",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const ITUNES = "https://itunes.apple.com/";
const APPLE_MUSIC = "https://music.apple.com/";
const STOREFRONTS = { fr: "fr", de: "de", es: "es", pt: "br", ru: "ru", ja: "jp", zh: "cn" };  // English: the US store
const MAX_ARTISTS = 50;
const PERIODS = [30, 60, 90];
const DEFAULT_DAYS = 60;
const WORKERS = 3;  // requests in flight; Ampify allows 4 per element
const PAUSE_MS = 200;
const DAY_MS = 86400000;
const HOUR_MS = 3600000;
const REFRESH_EVERY_MS = DAY_MS;
const STALE_ON_OPEN_MS = 12 * HOUR_MS;
const RETRY_UNKNOWN_MS = 7 * DAY_MS;  // an artist the catalog did not find is searched again after a week
const MAX_SHOWN = 40;  // the interface holds 300 nodes at most, and a release takes six
const COVER_SIZE = "300x300bb";
const EP_MAX_TRACKS = 6;
const SINGLE_MAX_TRACKS = 3;
const CREDIT_SEPARATOR = /\s+(?:&|x|feat\.?|ft\.?)\s+|\s*,\s*/i;

// Storage: artists[key] = { name, plays, last }; ids[key] = { id, at } (id 0: not found); releases = every album of
// the last 90 days seen so far; checkedAt = when the last check succeeded.
let artists = Ampify.storage.get("artists", {});
let ids = Ampify.storage.get("ids", {});
let releases = Ampify.storage.get("releases", []);
let checkedAt = Ampify.storage.get("checkedAt", 0);
let prefs = { days: DEFAULT_DAYS, singles: true, ...Ampify.storage.get("prefs", {}) };
let refreshing = false;
let failed = false;
const watchers = new Set();
const changed = () => watchers.forEach((watcher) => watcher());
const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Lower case, without accents or punctuation, for comparing names. Letters of every script are kept.
function plain(text) {
  return String(text || "")
    .normalize("NFD")
    .replace(/\p{M}/gu, "")
    .toLowerCase()
    .replace(/[^\p{L}\p{N}]+/gu, " ")
    .trim();
}

// --- The artists you listen to -----------------------------------------------------------------------------------

// Always rising, so two plays in the same millisecond still have an order.
let lastStamp = 0;
const stamp = () => (lastStamp = Math.max(Date.now(), lastStamp + 1));

function count(track) {
  const name = String((track && track.artist) || "").trim();
  const key = plain(name);
  if (!key) return;
  const known = artists[key] || { name, plays: 0, last: 0 };
  const next = { ...artists, [key]: { name: known.name, plays: known.plays + 1, last: stamp() } };
  // Keep the most listened; among equals, the most recent.
  const kept = Object.entries(next)
    .sort(([, a], [, b]) => b.plays - a.plays || b.last - a.last)
    .slice(0, MAX_ARTISTS);
  artists = Object.fromEntries(kept);
  Ampify.storage.set("artists", artists);
}

// --- Asking Apple's catalog --------------------------------------------------------------------------------------

const storefront = () => STOREFRONTS[Ampify.i18n.language] || "us";

// The parsed answer, null when the request failed.
async function getJson(url) {
  try {
    const answer = await Ampify.http.get(url);
    return answer.status === 200 ? JSON.parse(answer.text) : null;
  } catch (error) {
    Ampify.log("request failed: " + error);
    return null;
  }
}

// The catalog id of the artist; 0 when the catalog has no artist of that name, null when it could not be asked.
async function artistId(key, name) {
  const cached = ids[key];
  if (cached && (cached.id || Date.now() - cached.at < RETRY_UNKNOWN_MS)) return cached.id;
  const found = await getJson(ITUNES + "search?term=" + encodeURIComponent(name) + "&entity=musicArtist&limit=1&country=" + storefront());
  if (!found) return null;
  // The search is loose: only an artist of the same name counts, or the page would list a stranger's albums.
  const match = (found.results || [])[0];
  const id = match && plain(match.artistName) === key ? Number(match.artistId) || 0 : 0;
  ids = { ...ids, [key]: { id, at: Date.now() } };
  Ampify.storage.set("ids", ids);
  return id;
}

// "Single", "EP" or "Album": Apple names them "Title - Single" and "Title - EP"; the track count covers the rest.
function kindOf(album) {
  const name = String(album.collectionName || "");
  const tracks = Number(album.trackCount) || 0;
  // Apple's own word first: a three-track "X - EP" stays an EP.
  if (/ - single$/i.test(name)) return "single";
  if (/ - ep$/i.test(name)) return "ep";
  if (tracks > 0 && tracks <= SINGLE_MAX_TRACKS) return "single";
  if (tracks > 0 && tracks <= EP_MAX_TRACKS) return "ep";
  return "album";
}

function releaseOf(album) {
  const when = Date.parse(album.releaseDate);
  const cover = String(album.artworkUrl100 || "").replace(/\d+x\d+bb(?=\.\w+$)/, COVER_SIZE);
  if (!album.collectionId || Number.isNaN(when)) return null;
  return {
    id: album.collectionId,
    name: String(album.collectionName || "").replace(/ - (single|ep)$/i, ""),
    artist: String(album.artistName || ""),
    date: album.releaseDate,
    kind: kindOf(album),
    cover: cover.startsWith("https://") ? cover : "",
    // The link carries a tracking parameter (uo=4) that the page does not need.
    url: String(album.collectionViewUrl || "").replace(/([?&])uo=\d+&?/, "$1").replace(/[?&]$/, ""),
  };
}

// The recent albums of one artist, null when the catalog could not be asked.
async function albumsOf(key, name) {
  const id = await artistId(key, name);
  if (id === 0) return albumsOfEach(name);
  if (!id) return null;
  await wait(PAUSE_MS);
  const found = await getJson(ITUNES + "lookup?id=" + id + "&entity=album&sort=recent&limit=10&country=" + storefront());
  if (!found) return null;
  return (found.results || []).filter((item) => item.wrapperType === "collection").map(releaseOf).filter(Boolean);
}

// A shared credit ("Ninho & Freeze Corleone") is no artist of the catalog, but each name in it is. A name of one
// artist that the catalog lacks gives nothing.
async function albumsOfEach(credit) {
  const names = credit.split(CREDIT_SEPARATOR).map((part) => part.trim()).filter(Boolean);
  if (names.length < 2) return [];
  const albums = [];
  for (const name of names) {
    await wait(PAUSE_MS);
    const found = await albumsOf(plain(name), name);
    if (!found) return null;
    albums.push(...found);
  }
  return albums;
}

async function refresh() {
  const queue = Object.entries(artists);
  if (refreshing || queue.length === 0) return;
  refreshing = true;
  failed = false;
  changed();
  const fresh = [];
  let answered = 0;
  const worker = async () => {
    while (queue.length > 0) {
      const [key, artist] = queue.shift();
      const albums = await albumsOf(key, artist.name);
      if (albums) {
        answered += 1;
        fresh.push(...albums);
      }
      await wait(PAUSE_MS);
    }
  };
  try {
    await Promise.all(Array.from({ length: WORKERS }, worker));
  } catch (error) {
    Ampify.log("check failed: " + error);
  }
  if (answered > 0) {
    const oldest = Date.now() - Math.max(...PERIODS) * DAY_MS;
    const byId = new Map([...releases, ...fresh].map((release) => [release.id, release]));
    releases = [...byId.values()].filter((release) => Date.parse(release.date) >= oldest);
    checkedAt = Date.now();
    Ampify.storage.set("releases", releases);
    Ampify.storage.set("checkedAt", checkedAt);
    Ampify.log("checked " + answered + " artists, " + releases.length + " releases");
  } else {
    failed = true;
  }
  refreshing = false;
  changed();
}

// --- The page ----------------------------------------------------------------------------------------------------

const isoDay = (iso) => String(iso).slice(0, 10);

function visible() {
  const oldest = Date.now() - prefs.days * DAY_MS;
  const newest = Date.now() + DAY_MS;
  return releases
    .filter((release) => {
      const when = Date.parse(release.date);
      return when >= oldest && when <= newest && (prefs.singles || release.kind !== "single");
    })
    .sort((a, b) => (a.date < b.date ? 1 : -1))
    .slice(0, MAX_SHOWN);
}

function open(release) {
  if (release.url.startsWith(APPLE_MUSIC)) Ampify.http.open(release.url);
}

function Release({ release }) {
  return h(Card, { onClick: () => open(release) },
    h("row", { gap: 12 },
      release.cover ? h("image", { src: release.cover, label: release.name, width: 72, height: 72, cornerRadius: 6, fit: "cover" }) : null,
      h("stack", { grow: true },
        h(Text, { maxLines: 1, weight: 600 }, release.name),
        h(Text, { variant: "caption", maxLines: 1 },
          [release.artist, t(release.kind), isoDay(release.date)].join(" · ")))));
}

function setPrefs(change) {
  prefs = { ...prefs, ...change };
  Ampify.storage.set("prefs", prefs);
  changed();
}

function status() {
  if (refreshing) return t("checking");
  if (failed) return t("failed");
  if (!checkedAt) return "";
  const hours = Math.floor((Date.now() - checkedAt) / HOUR_MS);
  return hours < 1 ? t("checkedNow") : t("checked", { hours });
}

function ReleasesPage() {
  const [, setStamp] = useState(0);
  useEffect(() => {
    const watcher = () => setStamp((n) => n + 1);
    watchers.add(watcher);
    if (Date.now() - checkedAt > STALE_ON_OPEN_MS) refresh();
    return () => watchers.delete(watcher);
  }, []);

  const list = visible();
  const line = status();
  return h("stack", null,
    h("row", { gap: 12 },
      h(Dropdown, {
        label: t("period"),
        value: String(prefs.days),
        options: PERIODS.map((days) => ({ value: String(days), label: t("days", { days }) })),
        onChange: (value) => setPrefs({ days: Number(value) || DEFAULT_DAYS }),
      }),
      h(Toggle, { label: t("singles"), value: prefs.singles, onChange: (checked) => setPrefs({ singles: Boolean(checked) }) })),
    line ? h(Text, { variant: "caption" }, line) : null,
    Object.keys(artists).length === 0
      ? h(Text, { wrap: true }, t("empty"))
      : list.length === 0 && !refreshing
        ? h(Text, { wrap: true }, t("none", { days: prefs.days }))
        : h("stack", null, ...list.map((release) => h(Release, { key: String(release.id), release }))));
}

Ampify.player.on("trackchange", () => {
  count(Ampify.player.state().track);
  changed();
});

const timer = setInterval(() => {
  if (Date.now() - checkedAt >= REFRESH_EVERY_MS) refresh();
}, HOUR_MS);
Ampify.onStop(() => clearInterval(timer));
if (Date.now() - checkedAt >= REFRESH_EVERY_MS) refresh();

globalThis.render = () => h(ReleasesPage);
const ICON = '<svg viewBox="0 0 24 24"><path d="M12 2l2.9 6.9 7.1.6-5.4 4.7 1.7 7.3L12 17.8l-6.3 3.7 1.7-7.3L2 9.5l7.1-.6z"/></svg>';
Ampify.ui.sidebar.addItem({ label: t("title"), icon: ICON, onClick: () => Ampify.ui.navigate("/new-releases") });
