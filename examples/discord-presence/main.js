// Shows the current track in your Discord status (Rich Presence), as "Listening to": title, artist, album, cover,
// a progress bar, clickable lines and up to two buttons, all chosen in the Ampify card.
//
// Nothing to set up: switching it on in the Ampify card also turns on Ampify's Discord service, and the status goes
// through Ampify's Discord application, named "Apple Music" so that Discord writes "Listening to Apple Music".
// Another application can be used (its name and images show in the status):
//   ampify config extensions.discord-presence.clientId <id from https://discord.com/developers/applications>
// It needs two Rich Presence art assets: "ampify" (shown when a song has no cover) and "paused" (the small badge
// while paused), 512 x 512 pixels at least.
//
// No server of Ampify is involved. The cover and the links come from Apple's public iTunes Search API, asked once
// per track. A track the catalog does not know (a file of your own, or a song missing from the search) has its cover
// read from Windows and, if "Share covers the catalog does not know" is on, sent to a free host that deletes it
// later (litterbox.catbox.moe, 3 days, else uguu.se, 3 hours): Discord can only show an image that is on the web.

// The Discord application: its id is public, Discord only uses it to name the status and find its images.
const DEFAULT_CLIENT_ID = "1566762712413904957";
const LOGO_IMAGE = "ampify";
const PAUSED_IMAGE = "paused";
const SEARCH = "https://itunes.apple.com/search";
const COVER_SIZE = "600x600bb";
const HOUR_MS = 3600 * 1000;
const DAY_MS = 24 * HOUR_MS;
const RETRY_MS = 10 * 60 * 1000;  // after a failed search or upload
const MAX_LINK = 512;  // bytes of a button link, for Discord
// Free hosts for a cover, tried in turn. keepsMs: a little less than the host keeps it, so it is sent again in time.
const HOSTS = [
  {
    url: "https://litterbox.catbox.moe/resources/internals/api.php",
    fields: (file) => ({ reqtype: "fileupload", time: "72h", fileToUpload: file }),
    link: (text) => text.trim(),
    prefix: "https://litter.catbox.moe/",
    keepsMs: 70 * HOUR_MS,
  },
  {
    url: "https://uguu.se/upload",
    fields: (file) => ({ "files[]": file }),
    link: (text) => JSON.parse(text).files[0].url,
    prefix: "https://h.uguu.se/",
    keepsMs: 2.5 * HOUR_MS,
  },
];
const SETTLE_MS = 2000;  // Windows takes a moment to show the new track's cover
const MAX_REMEMBERED = 200;  // tracks whose lookup is kept in storage
// The Apple store of each language (English: the US one); the links then open in it.
const STOREFRONTS = { fr: "fr", de: "de", es: "es", pt: "br", ru: "ru", ja: "jp", zh: "cn" };

Ampify.i18n.define({
  en: { paused: "Paused", unknown: "Unknown title", listen: "Listen on Apple Music", youtube: "Find on YouTube Music",
        spotify: "Find on Spotify", platforms: "Other platforms" },
  fr: { paused: "En pause", unknown: "Titre inconnu", listen: "Écouter sur Apple Music",
        youtube: "Trouver sur YouTube Music", spotify: "Trouver sur Spotify", platforms: "Autres plateformes" },
  de: { paused: "Pausiert", unknown: "Unbekannter Titel", listen: "Auf Apple Music anhören",
        youtube: "Auf YouTube Music finden", spotify: "Auf Spotify finden", platforms: "Andere Plattformen" },
  es: { paused: "En pausa", unknown: "Título desconocido", listen: "Escuchar en Apple Music",
        youtube: "Buscar en YouTube Music", spotify: "Buscar en Spotify", platforms: "Otras plataformas" },
  pt: { paused: "Pausado", unknown: "Título desconhecido", listen: "Ouvir no Apple Music",
        youtube: "Encontrar no YouTube Music", spotify: "Encontrar no Spotify", platforms: "Outras plataformas" },
  ru: { paused: "На паузе", unknown: "Неизвестное название", listen: "Слушать в Apple Music",
        youtube: "Найти в YouTube Music", spotify: "Найти в Spotify", platforms: "Другие платформы" },
  ja: { paused: "一時停止中", unknown: "不明なタイトル", listen: "Apple Music で聴く",
        youtube: "YouTube Music で探す", spotify: "Spotify で探す", platforms: "ほかのサービス" },
  zh: { paused: "已暂停", unknown: "未知标题", listen: "在 Apple Music 上收听",
        youtube: "在 YouTube Music 上查找", spotify: "在 Spotify 上查找", platforms: "其他平台" },
});
const t = (key) => Ampify.i18n.t(key);

const setting = (key, fallback) => Ampify.config.get(key, fallback);
const clientId = String(setting("clientId", "") || DEFAULT_CLIENT_ID).trim();
const options = {
  statusLine: setting("status-line", "Artist"),
  showPaused: setting("show-paused", false),
  showAlbum: setting("show-album", true),
  showProgress: setting("show-progress", true),
  showCover: setting("show-cover", true),
  shareCovers: setting("share-covers", true),
  clickable: setting("clickable-links", true),
  // "song.link" is the name the "Other platforms" choice had before 0.3.
  buttons: [setting("button-1", "Apple Music"), setting("button-2", "Other platforms")].map((name) =>
    (name === "song.link" ? "Other platforms" : name)),
};

// --- Where a track's cover and links come from ---------------------------------------------------------------------

const lookups = new Map(Object.entries(Ampify.storage.get("lookups", {})));
const asking = new Set();

// Lower case, without accents or punctuation: "Beyoncé - Halo (Live)" ~ "beyonce halo live".
function plain(text) {
  return String(text || "")
    .normalize("NFD")
    .replace(/\p{M}/gu, "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, " ")
    .trim();
}

// The same without "(feat. ...)", "[Remastered]" and the like: "Beyoncé - Halo (Live)" ~ "beyonce halo".
function simple(text) {
  return plain(String(text || "").replace(/\(.*?\)|\[.*?\]/g, " "));
}

// The same name, or one that goes on with more words: "halo" ~ "halo live", but "air" is not "airbourne".
function sameName(a, b) {
  const x = simple(a);
  const y = simple(b);
  return x !== "" && y !== "" && (x === y || x.startsWith(y + " ") || y.startsWith(x + " "));
}

// The result that is this track: same title and artist; the exact title first (not a remix), then the same album.
function bestMatch(results, track) {
  const songs = results.filter((r) => sameName(r.trackName, track.title) && sameName(r.artistName, track.artist));
  const score = (r) => (plain(r.trackName) === plain(track.title) ? 2 : 0) + (sameName(r.collectionName, track.album) ? 1 : 0);
  return songs.sort((a, b) => score(b) - score(a))[0] || null;
}

// What is known of a track: its catalog entry, an uploaded cover, null when neither, undefined when not asked yet.
function known(track) {
  const found = lookups.get(track.id);
  if (found && found.keptUntil && Date.now() > found.keptUntil) return undefined;
  return found;
}

function remember(id, found) {
  lookups.delete(id);
  lookups.set(id, found);
  while (lookups.size > MAX_REMEMBERED) lookups.delete(lookups.keys().next().value);
  Ampify.storage.set("lookups", Object.fromEntries(lookups));
}

// The catalog entry of the track, null when the catalog does not have it, undefined when it could not be asked.
function search(track) {
  // Slashes and brackets make the search miss: "A / B (Live)" is looked up as "A B Live".
  const words = (track.artist + " " + track.title).replace(/[/()[\]]+/g, " ").replace(/\s+/g, " ").trim();
  const term = encodeURIComponent(words);
  const country = STOREFRONTS[Ampify.i18n.language] || "us";
  return Ampify.http.get(SEARCH + "?media=music&entity=song&limit=25&country=" + country + "&term=" + term).then((answer) => {
    if (answer.status !== 200) {
      Ampify.log("cover: the catalog search answered " + answer.status);
      return undefined;
    }
    let results;
    try {
      results = JSON.parse(answer.text).results || [];
    } catch {
      Ampify.log("cover: the catalog search answered something else than JSON");
      return undefined;
    }
    const match = bestMatch(results, track);
    if (!match) {
      Ampify.log("cover: not in the catalog (" + results.length + " results for \"" + words + "\")");
      return null;
    }
    // The links carry a tracking parameter (uo=4) that the page does not need.
    const clean = (url) => String(url || "").replace(/([?&])uo=\d+&?/, "$1").replace(/[?&]$/, "");
    return {
      cover: String(match.artworkUrl100 || "").replace(/\d+x\d+bb(?=\.\w+$)/, COVER_SIZE),
      song: clean(match.trackViewUrl),
      artist: clean(match.artistViewUrl),
      album: clean(match.collectionViewUrl).replace(/\?i=\d+$/, ""),  // a single's album link points at its song
      songId: match.trackId || 0,
    };
  });
}

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// The cover Windows shows for the track, put on the web. null without a cover, undefined when it could not be sent.
function upload(track) {
  return wait(SETTLE_MS)
    .then(() => {
      const now = Ampify.player.state().track;
      return now && now.id === track.id ? Ampify.player.artwork() : undefined;  // the track changed meanwhile
    })
    .then((cover) => {
      if (cover === undefined) Ampify.log("cover: the track changed before its cover could be read");
      if (cover === null) Ampify.log("cover: Windows shows no cover for this track");
      if (!cover) return cover;
      const now = Ampify.player.state().track;
      if (!now || now.id !== track.id) return undefined;  // the cover read may already be the next track's
      const extension = cover.type === "image/png" ? "png" : "jpg";
      return sendTo(0, { filename: "cover." + extension, type: cover.type, base64: cover.base64 });
    });
}

// Sends the file to HOSTS[index], or to the next host when that one fails.
function sendTo(index, file) {
  const host = HOSTS[index];
  if (!host) {
    Ampify.log("cover: no host took the cover");
    return Promise.resolve(undefined);
  }
  return Ampify.http.postForm(host.url, host.fields(file)).then((answer) => {
    let url = "";
    try {
      url = answer.status === 200 ? String(host.link(answer.text) || "") : "";
    } catch {
      url = "";
    }
    if (url.startsWith(host.prefix)) return { cover: url, keptUntil: Date.now() + host.keepsMs };
    return sendTo(index + 1, file);
  });
}

function lookUp(track) {
  if (asking.has(track.id) || !track.title) return;
  asking.add(track.id);
  search(track)
    .then((found) => (found === null && options.shareCovers && options.showCover ? upload(track) : found))
    .catch((error) => {  // too many requests at once, or a host that refused
      Ampify.log("cover: lookup failed, " + error);
      return undefined;
    })
    .then((found) => {
      // Nothing found is asked again the next day (the catalog grows), a failure after a few minutes.
      if (found === undefined) remember(track.id, { keptUntil: Date.now() + RETRY_MS });
      else remember(track.id, found || { keptUntil: Date.now() + DAY_MS });
      show(Ampify.player.state());
    })
    .finally(() => asking.delete(track.id));
}

// --- What Discord shows ----------------------------------------------------------------------------------------------

const https = (url) => (String(url || "").startsWith("https://") && url.length <= 256 ? url : "");

function searchUrl(base, track) {
  return base + encodeURIComponent((track.artist + " " + track.title).trim());
}

// The two buttons chosen in the card. A track the catalog does not know still gets the search buttons.
function buttonsFor(track, found) {
  const song = found && found.songId;
  const make = {
    "Apple Music": () => ({ label: t("listen"),
      url: (song && found.song) || searchUrl("https://music.apple.com/search?term=", track) }),
    // song.link: one page with the song on Spotify, YouTube Music, Deezer, Tidal and the others.
    "Other platforms": () => (song ? { label: t("platforms"), url: "https://song.link/i/" + song } : null),
    "YouTube Music": () => ({ label: t("youtube"), url: searchUrl("https://music.youtube.com/search?q=", track) }),
    "Spotify": () => ({ label: t("spotify"), url: searchUrl("https://open.spotify.com/search/", track) }),
  };
  const seen = new Set();
  return options.buttons
    .filter((name) => make[name] && !seen.has(name) && seen.add(name))
    .map((name) => make[name]())
    .filter((button) => button && button.url.length <= MAX_LINK);  // the links are percent-encoded: 1 byte a char
}

function activityFor(state) {
  const track = state.track;
  const found = known(track) || null;
  const activity = {
    clientId,
    type: "listening",
    statusDisplay: { Artist: "state", Title: "details", "App name": "name" }[options.statusLine] || "state",
    details: track.title || t("unknown"),
    state: track.artist || "Apple Music",
    largeImage: (options.showCover && (https(track.artwork) || (found && https(found.cover)))) || LOGO_IMAGE,
    largeText: options.showAlbum && track.album ? track.album : "Apple Music",
    buttons: buttonsFor(track, found),
  };
  if (options.clickable && found && found.songId) {
    activity.detailsUrl = https(found.song) || undefined;
    activity.stateUrl = https(found.artist) || undefined;
    activity.largeUrl = https(found.album) || undefined;
  }
  if (state.playing && options.showProgress && state.durationMs > 0) {
    // Discord draws the progress bar from these two dates.
    activity.startMs = Date.now() - state.positionMs;
    activity.endMs = activity.startMs + state.durationMs;
  } else if (!state.playing) {
    activity.smallImage = PAUSED_IMAGE;
    activity.smallText = t("paused");
  }
  return activity;
}

function show(state) {
  const track = state && state.track;
  if (!track || (!state.playing && !options.showPaused)) {
    Ampify.discord.clear();
    return;
  }
  Ampify.discord.setActivity(activityFor(state));
  if (known(track) === undefined) lookUp(track);
}

Ampify.player.on("trackchange", show);
Ampify.player.on("statechange", show);
show(Ampify.player.state());
Ampify.onStop(() => Ampify.discord.clear());
