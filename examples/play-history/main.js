// Every song played on this PC, newest first, stations included: Apple Music shows no history for a station such as
// Discovery. A song joins the list once it has played for a few seconds; each play is its own line, so a song heard
// twice shows twice. The list stays on the PC, in this element's storage. A line opens the song in Apple Music; its
// cover and link come from Apple's public iTunes search, asked only for the lines shown.
const { h, Fragment, useState, useEffect } = Ampify.React;
const { Button, Card, Text } = Ampify.components;

Ampify.i18n.define({
  en: {
    title: "Play history", empty: "Nothing yet: songs show here once they have played for a few seconds.",
    note: "Every song played on this device, stations included.", today: "Today", yesterday: "Yesterday",
    newer: "Newer", older: "Older", page: "{from}–{to} of {count}", clear: "Clear the history",
    confirm: "Click again to clear", open: "Open in Apple Music",
  },
  fr: {
    title: "Historique d'écoute", empty: "Rien pour l'instant : un morceau s'affiche ici après quelques secondes d'écoute.",
    note: "Tous les morceaux écoutés sur cet appareil, stations comprises.", today: "Aujourd'hui", yesterday: "Hier",
    newer: "Plus récents", older: "Plus anciens", page: "{from}–{to} sur {count}", clear: "Effacer l'historique",
    confirm: "Cliquez encore pour effacer", open: "Ouvrir dans Apple Music",
  },
  de: {
    title: "Wiedergabeverlauf", empty: "Noch nichts: Titel erscheinen hier, sobald sie einige Sekunden gelaufen sind.",
    note: "Alle Titel, die auf diesem Gerät liefen, Sender eingeschlossen.", today: "Heute", yesterday: "Gestern",
    newer: "Neuere", older: "Ältere", page: "{from}–{to} von {count}", clear: "Verlauf löschen",
    confirm: "Zum Löschen erneut klicken", open: "In Apple Music öffnen",
  },
  es: {
    title: "Historial de reproducción", empty: "Nada todavía: las canciones aparecen aquí tras sonar unos segundos.",
    note: "Todas las canciones que sonaron en este dispositivo, emisoras incluidas.", today: "Hoy", yesterday: "Ayer",
    newer: "Más recientes", older: "Más antiguas", page: "{from}–{to} de {count}", clear: "Borrar el historial",
    confirm: "Haz clic otra vez para borrar", open: "Abrir en Apple Music",
  },
  pt: {
    title: "Histórico de reprodução", empty: "Nada ainda: as músicas aparecem aqui depois de tocar alguns segundos.",
    note: "Todas as músicas tocadas neste aparelho, estações incluídas.", today: "Hoje", yesterday: "Ontem",
    newer: "Mais recentes", older: "Mais antigas", page: "{from}–{to} de {count}", clear: "Limpar o histórico",
    confirm: "Clique de novo para limpar", open: "Abrir no Apple Music",
  },
  ru: {
    title: "История прослушивания", empty: "Пока пусто: трек появится здесь через несколько секунд воспроизведения.",
    note: "Все треки, сыгранные на этом устройстве, включая станции.", today: "Сегодня", yesterday: "Вчера",
    newer: "Новее", older: "Старее", page: "{from}–{to} из {count}", clear: "Очистить историю",
    confirm: "Нажмите ещё раз, чтобы очистить", open: "Открыть в Apple Music",
  },
  ja: {
    title: "再生履歴", empty: "まだありません。曲は数秒再生されるとここに表示されます。",
    note: "このデバイスで再生したすべての曲（ステーションを含む）。", today: "今日", yesterday: "昨日",
    newer: "新しい順へ", older: "古い順へ", page: "{count} 曲中 {from}–{to}", clear: "履歴を消去",
    confirm: "もう一度クリックで消去", open: "Apple Music で開く",
  },
  zh: {
    title: "播放历史", empty: "暂无记录：歌曲播放几秒后会显示在这里。",
    note: "在此设备上播放的所有歌曲，包括电台。", today: "今天", yesterday: "昨天",
    newer: "较新", older: "较早", page: "第 {from}–{to} 首，共 {count} 首", clear: "清除历史",
    confirm: "再次点击以清除", open: "在 Apple Music 中打开",
  },
  it: {
    title: "Cronologia di ascolto", empty: "Ancora niente: i brani compaiono qui dopo qualche secondo di ascolto.",
    note: "Tutti i brani ascoltati su questo dispositivo, stazioni incluse.", today: "Oggi", yesterday: "Ieri",
    newer: "Più recenti", older: "Meno recenti", page: "{from}–{to} di {count}", clear: "Cancella la cronologia",
    confirm: "Clicca di nuovo per cancellare", open: "Apri in Apple Music",
  },
  ko: {
    title: "재생 기록", empty: "아직 없습니다. 곡이 몇 초 재생되면 여기에 표시됩니다.",
    note: "이 기기에서 재생한 모든 곡(스테이션 포함).", today: "오늘", yesterday: "어제",
    newer: "최신", older: "이전", page: "{count}곡 중 {from}–{to}", clear: "기록 지우기",
    confirm: "지우려면 다시 클릭", open: "Apple Music에서 열기",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const TICK_MS = 5000;
const HEARD_MS = 10000;         // a song skipped at once is not a listen
const RESTART_MS = 15000;       // a song back under this position, from further on, is played again
const MAX_ENTRIES = 1000;
const PAGE_SIZE = 30;           // a line takes 7 nodes, and a page 300 at most
const MAX_NAME = 200;
const CONFIRM_MS = 4000;
const COVER_SIZE = "120x120";   // shown at 48 pixels: sharp up to 250 %
const ITUNES = "https://itunes.apple.com/";
const APPLE_MUSIC = "https://music.apple.com/";
// A light page of Apple Music's web player, which names its script; the script holds the public token.
const WEB_PLAYER = "https://music.apple.com";
const WEB_PLAYER_PAGE = WEB_PLAYER + "/us/search";
const CATALOG = "https://amp-api.music.apple.com/v1/catalog/";
const TOKEN_KEPT_MS = 7 * 86400000;  // the token lasts a few months: read again weekly, or when refused
const RETRY_MS = 600000;
const CATALOG_ID = /^\d{1,19}$/;
const COLOR = /^#[0-9A-Fa-f]{6}$/;
const DAY_MS = 86400000;

const cut = (text) => String(text || "").trim().slice(0, MAX_NAME);

// Storage: entries = [{ title, artist, album, at, color?, catalogId?, cover?, link? }], newest first.
let entries = Ampify.storage.get("entries", []);
const watchers = new Set();
let current = null;  // { key, heardMs, positionMs, logged }

function save(next) {
  entries = next.slice(0, MAX_ENTRIES);
  Ampify.storage.set("entries", entries);
  watchers.forEach((watcher) => watcher());
}

// The playing song's catalog id from the queue's current item, which must be that song. "" when Apple gives none
// (its queue stays empty until its Up Next panel has opened once).
const plain = (text) => String(text || "").normalize("NFD").replace(/\p{M}/gu, "").toLowerCase()
  .replace(/[^\p{L}\p{N}]+/gu, " ").trim();
function playingCatalogId(track) {
  try {
    const queue = Ampify.player.queue.list();
    const items = queue && Array.isArray(queue.items) ? queue.items : [];
    const playing = items.find((item) => item && item.section === "current");
    if (!playing || plain(playing.title) !== plain(track.title)) return "";
    return CATALOG_ID.test(String(playing.catalogId || "")) ? String(playing.catalogId) : "";
  } catch (error) {
    Ampify.log("no catalog id: " + (error && error.message));
    return "";
  }
}

function log(track) {
  const catalogId = playingCatalogId(track);
  const cover = String(track.artwork || "").startsWith("https://") ? track.artwork : "";
  const color = COLOR.test(String(track.artworkColor || "")) ? track.artworkColor : "";
  save([{
    title: cut(track.title), artist: cut(track.artist), album: cut(track.album), at: Date.now(),
    ...(color ? { color } : {}), ...(catalogId ? { catalogId } : {}), ...(cover ? { cover } : {}),
  }, ...entries]);
}

// The newest line is this very play, listed before the element started again (a setting changed): it was listed
// after the song's start, so less than its position ago.
function listedThisPlay(track, position) {
  const newest = entries[0];
  return Boolean(newest) && newest.title === cut(track.title) && newest.artist === cut(track.artist) &&
    Date.now() - newest.at <= position;
}

function tick() {
  const state = Ampify.player.state();
  const track = state && state.track;
  if (!state || !state.playing || !track || !cut(track.title)) return;
  const key = track.queueItemId || track.id || track.title + "\n" + track.artist;
  const position = state.positionMs || 0;
  // Back at its start without a track change (repeat one, or Previous): a new listen.
  const restarted = current && current.key === key && position + TICK_MS < current.positionMs && position < RESTART_MS;
  if (!current || current.key !== key || restarted) {
    current = { key, heardMs: 0, logged: !restarted && listedThisPlay(track, position) };
  }
  current.positionMs = position;
  current.heardMs += TICK_MS;
  if (!current.logged && current.heardMs >= HEARD_MS) {
    current.logged = true;
    log(track);
  }
}

// A new entry of the queue is a new listen, even of the same song.
Ampify.player.on("trackchange", () => (current = null));

// --- Covers and links ------------------------------------------------------------------------------------------------
// Apple Music's own catalog, through the API of its web player with the public token its page carries, as the
// Discord presence does: the iTunes search misses many songs (explicit ones, French rap). The iTunes search is the
// fallback when the token cannot be read.

// The storefront of the Windows region ("fr-FR" -> "fr"); Apple Music redirects to the user's own.
function storefront() {
  const match = /[-_]([A-Za-z]{2})$/.exec(String(Ampify.locale || ""));
  return match ? match[1].toLowerCase() : "us";
}

function parsed(reply) {
  if (!reply || reply.status !== 200) return null;
  try {
    return JSON.parse(reply.text);
  } catch {
    return null;
  }
}

// The web player's public token, kept a week in storage; null when its page gives none (tried again in 10 minutes).
// Searches running together share one read.
let tokenRead = null;
function webToken() {
  const kept = Ampify.storage.get("webToken", null);
  if (kept && Date.now() < kept.until) return Promise.resolve(kept.token || null);
  tokenRead = tokenRead || Ampify.http.get(WEB_PLAYER_PAGE)
    .then((page) => {
      const script = page.status === 200 ? (/\/assets\/index[~\w-]*\.js/.exec(page.text) || [])[0] : undefined;
      return script ? Ampify.http.get(WEB_PLAYER + script) : { status: 0, text: "" };
    })
    .then((answer) => {
      const token = answer.status === 200 ? (/eyJ[\w-]+\.eyJ[\w-]+\.[\w-]+/.exec(answer.text) || [])[0] || null : null;
      if (!token) Ampify.log("Apple Music's web player gave no token: the iTunes search is used");
      Ampify.storage.set("webToken", { token, until: Date.now() + (token ? TOKEN_KEPT_MS : RETRY_MS) });
      return token;
    })
    .finally(() => (tokenRead = null));
  return tokenRead;
}

// The words to search: slashes and brackets make the searches miss, "A / B (Live)" is looked up as "A B Live".
const searchWords = (entry) => (entry.artist + " " + entry.title).replace(/[/()[\]]+/g, " ").replace(/\s+/g, " ").trim();

// Songs of Apple Music's catalog as { trackName, artistName, trackId, cover, link }; null when it could not be asked.
async function searchCatalog(entry) {
  const token = await webToken();
  if (!token) return null;
  const path = entry.catalogId ? "/songs/" + entry.catalogId
    : "/search?types=songs&limit=25&term=" + encodeURIComponent(searchWords(entry));
  const reply = await Ampify.http.get(CATALOG + storefront() + path, { headers: { Authorization: "Bearer " + token, Origin: WEB_PLAYER } });
  if (reply.status === 401 || reply.status === 403) Ampify.storage.remove("webToken");  // read again next time
  const answer = parsed(reply);
  if (!answer) return null;
  const songs = entry.catalogId ? answer.data || [] : ((answer.results || {}).songs || {}).data || [];
  return songs.map((song) => {
    const about = song.attributes || {};
    return { trackName: about.name, artistName: about.artistName, albumName: about.albumName, trackId: song.id,
             cover: String((about.artwork || {}).url || "").replace("{w}x{h}", COVER_SIZE), link: about.url || "" };
  });
}

async function searchItunes(entry) {
  const query = entry.catalogId ? "lookup?id=" + entry.catalogId
    : "search?media=music&entity=song&limit=25&term=" + encodeURIComponent(searchWords(entry));
  const answer = parsed(await Ampify.http.get(ITUNES + query + "&country=" + storefront()));
  return ((answer && answer.results) || []).filter((hit) => hit.trackName).map((hit) => ({
    trackName: hit.trackName, artistName: hit.artistName, albumName: hit.collectionName, trackId: hit.trackId,
    cover: String(hit.artworkUrl100 || "").replace(/\d+x\d+bb/, COVER_SIZE + "bb"), link: hit.trackViewUrl || "",
  }));
}

// The hit that is this very song: by its catalog id, else by its title and one of its credited artists, from the
// same album when the song came out more than once (a single, then the album: each has its own cover).
function matchOf(entry, hits) {
  if (entry.catalogId) return hits.find((hit) => String(hit.trackId) === entry.catalogId);
  // One of the artists credited: "A & B" or "A, B" plays as A, and B's own songs too.
  const credits = (credited) => plain(credited) === plain(entry.artist) ||
    String(credited || "").split(/\s*(?:&|,|\band\b|\bet\b|\bx\b|\bfeat\.?|\bft\.?)\s*/i).some((name) => plain(name) === plain(entry.artist));
  const songs = hits.filter((hit) => plain(hit.trackName) === plain(entry.title) && credits(hit.artistName));
  return songs.find((hit) => entry.album && plain(hit.albumName) === plain(entry.album)) || songs[0];
}

// Fills the cover and link of the entries shown that have none, once per start; the page draws again as each one
// arrives. Two at a time, under the four requests an element may have in flight.
const asked = new Set();
async function resolve(shown) {
  const missing = shown.filter((entry) => !(entry.cover && entry.link) && !asked.has(entry.at));
  missing.forEach((entry) => asked.add(entry.at));
  const next = async () => {
    const entry = missing.shift();
    if (!entry) return;
    const hit = matchOf(entry, (await searchCatalog(entry)) || (await searchItunes(entry)));
    if (hit) {
      const link = String(hit.link).startsWith(APPLE_MUSIC) ? hit.link : "";
      const cover = String(hit.cover).startsWith("https://") ? hit.cover : "";
      save(entries.map((known) => (known.at === entry.at && known.title === entry.title
        ? { ...known, cover: known.cover || cover, link } : known)));
    }
    await next();
  };
  await Promise.all([next(), next()]);
}

function open(entry) {
  if (entry.link) Ampify.http.open(entry.link);
  else if (entry.catalogId) Ampify.http.open(Ampify.uri.build("song", entry.catalogId, { storefront: storefront() }));
}

// --- The page --------------------------------------------------------------------------------------------------------

const two = (n) => String(n).padStart(2, "0");
const timeOf = (at) => two(new Date(at).getHours()) + ":" + two(new Date(at).getMinutes());
const dayKey = (at) => new Date(at).toDateString();

// A day in the interface's language, from numbers only: the script engine has no locale data.
const DATE_ORDER = { en: "mdy", de: "dmy.", ja: "ymd/", zh: "ymd/", ko: "ymd." };
function dayName(at) {
  if (dayKey(at) === dayKey(Date.now())) return t("today");
  if (dayKey(at) === dayKey(Date.now() - DAY_MS)) return t("yesterday");
  const date = new Date(at);
  const parts = { d: two(date.getDate()), m: two(date.getMonth() + 1), y: String(date.getFullYear()) };
  const order = DATE_ORDER[Ampify.i18n.language] || "dmy/";
  return order.slice(0, 3).split("").map((part) => parts[part]).join(order[3] || "/");
}

function Line({ entry }) {
  const cover = entry.cover
    ? h("image", { src: entry.cover, label: entry.album || entry.title, width: 48, height: 48, cornerRadius: 6, fit: "cover" })
    : h("stack", { width: 48, height: 48, cornerRadius: 6, background: entry.color || "#22FFFFFF" });
  const canOpen = Boolean(entry.link || entry.catalogId);
  return h(Card, { padding: "8,6,8,6", onClick: canOpen ? () => open(entry) : undefined, tooltip: canOpen ? t("open") : undefined },
    h("row", { gap: 12 },
      cover,
      h("stack", { grow: true, gap: 0, valign: "center" },
        h(Text, { size: 15, weight: 600, maxLines: 1 }, entry.title),
        h(Text, { variant: "caption", maxLines: 1 }, [entry.artist, entry.album].filter(Boolean).join(" · "))),
      h(Text, { variant: "caption", valign: "center", wrap: false }, timeOf(entry.at))));
}

// The lines of a page under the name of their day.
function Days({ shown }) {
  const out = [];
  let day = "";
  for (const entry of shown) {
    if (dayKey(entry.at) !== day) {
      day = dayKey(entry.at);
      out.push(h(Text, { key: "day:" + entry.at, variant: "subtitle" }, dayName(entry.at)));
    }
    out.push(h(Line, { key: entry.at + ":" + entry.title, entry }));
  }
  return h(Fragment, null, ...out);
}

function HistoryPage() {
  const [, setStamp] = useState(0);
  const [start, setStart] = useState(0);
  const [confirming, setConfirming] = useState(false);
  useEffect(() => {
    const watcher = () => setStamp((n) => n + 1);
    watchers.add(watcher);
    return () => watchers.delete(watcher);
  }, []);
  // The last page when the list has shrunk under the one shown (cleared, or cut at MAX_ENTRIES).
  const lastPage = Math.max(0, Math.floor((entries.length - 1) / PAGE_SIZE) * PAGE_SIZE);
  const first = Math.min(start, lastPage);
  const shown = entries.slice(first, first + PAGE_SIZE);
  useEffect(() => {
    resolve(shown).catch((error) => Ampify.log("covers not found: " + (error && error.message)));
  }, [first, entries.length]);
  useEffect(() => {
    if (!confirming) return undefined;
    const timer = setTimeout(() => setConfirming(false), CONFIRM_MS);
    return () => clearTimeout(timer);
  }, [confirming]);

  if (entries.length === 0) return h(Text, { size: 15, wrap: true }, t("empty"));
  const clear = () => {
    if (!confirming) return setConfirming(true);
    setConfirming(false);
    setStart(0);
    save([]);
  };
  return h("stack", { gap: 8, padding: "0,0,24,0" },
    h("row", { gap: 8 },
      h(Text, { variant: "caption", grow: true, valign: "center", wrap: true }, t("note")),
      h(Button, { label: t(confirming ? "confirm" : "clear"), variant: "secondary", onClick: clear })),
    h(Days, { shown }),
    h("row", { gap: 8 },
      h(Button, { label: t("newer"), variant: "secondary", disabled: first === 0, onClick: () => setStart(Math.max(0, first - PAGE_SIZE)) }),
      h(Text, { variant: "caption", grow: true, align: "center", valign: "center" },
        t("page", { from: first + 1, to: first + shown.length, count: entries.length })),
      h(Button, { label: t("older"), variant: "secondary", disabled: first + PAGE_SIZE >= entries.length,
                  onClick: () => setStart(first + PAGE_SIZE) })));
}

const timer = setInterval(tick, TICK_MS);
Ampify.onStop(() => clearInterval(timer));

// The app's "/play-history" page, and its entry in the sidebar: a clock with an arrow running back (icons take paths
// only, drawn in the text colour).
globalThis.render = () => h(HistoryPage);
const HISTORY_ICON = '<svg viewBox="0 0 24 24"><path d="' +
  "M12 3a9 9 0 1 1-8.66 11.45 1 1 0 1 1 1.92-.55A7 7 0 1 0 6.3 7.5H8a1 1 0 0 1 0 2H4a1 1 0 0 1-1-1v-4a1 1 0 0 1 2 0v1.36A8.98 8.98 0 0 1 12 3z" +
  "M12 7a1 1 0 0 1 1 1v3.59l2.7 2.7a1 1 0 0 1-1.4 1.42l-3-3A1 1 0 0 1 11 12V8a1 1 0 0 1 1-1z" +
  '"/></svg>';
Ampify.ui.sidebar.addItem({ label: t("title"), icon: HISTORY_ICON, onClick: () => Ampify.ui.navigate("/play-history") });
