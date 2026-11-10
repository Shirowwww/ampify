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
// No server of Ampify is involved. The cover and the links come from Apple Music's catalog, asked once per track, as
// Music Presence does: through the API of Apple Music's web player, with the public token that its page carries
// (read again every few weeks), since Apple's public iTunes Search API leaves out explicit songs. Without that token,
// the iTunes search; a song Apple's search misses is looked up on Deezer's public API: its cover, and a song.link
// page for the other platforms. A track none knows (a file of your own) has its cover read from Windows and, if
// "Share covers the catalog does not know" is on, sent to a free host that deletes it later (x0.at, about 100 days for
// a cover; else uguu.se, 3 hours; else litterbox.catbox.moe, 3 days): Discord only shows an image that is on the web.
// With "Animated covers" on, an album's motion artwork (a video on Apple's host) is made a GIF on the PC
// (Ampify.http.gifFromVideo) and goes to the same hosts, once per album.

// The Discord application: its id is public, Discord only uses it to name the status and find its images.
const DEFAULT_CLIENT_ID = "1566762712413904957";
const LOGO_IMAGE = "ampify";
const PAUSED_IMAGE = "paused";
const SEARCH = "https://itunes.apple.com/search";
const DEEZER_SEARCH = "https://api.deezer.com/search";
// A light page of Apple Music's web player (about 340 KB), which names its script; the script holds the token.
const WEB_PLAYER = "https://music.apple.com";
const WEB_PLAYER_PAGE = WEB_PLAYER + "/us/search";
const CATALOG = "https://amp-api.music.apple.com/v1/catalog/";
const COVER_SIZE = "600x600bb";
const HOUR_MS = 3600 * 1000;
const DAY_MS = 24 * HOUR_MS;
const RETRY_MS = 10 * 60 * 1000;  // after a failed search or upload
const TOKEN_KEPT_MS = 7 * DAY_MS;  // the web player's token lasts a few months: read again weekly, or when refused
const MAX_LINK = 512;  // bytes of a button link, for Discord
// Free hosts for a cover, tried in turn. keepsMs: a little less than the host keeps it, so it is sent again in time.
// domain: the link must be on it or on one of its subdomains (uguu.se moved its files from h.uguu.se to d.uguu.se).
// litterbox came first until its firewall refused every request (11/2026): x0.at keeps a small file 100 days.
const HOSTS = [
  {
    url: "https://x0.at/",
    fields: (file) => ({ file, id_length: "16" }),
    link: (text) => text.trim(),
    domain: "x0.at",
    keepsMs: 60 * DAY_MS,
  },
  {
    url: "https://uguu.se/upload",
    fields: (file) => ({ "files[]": file }),
    link: (text) => JSON.parse(text).files[0].url,
    domain: "uguu.se",
    keepsMs: 2.5 * HOUR_MS,
  },
  {
    url: "https://litterbox.catbox.moe/resources/internals/api.php",
    fields: (file) => ({ reqtype: "fileupload", time: "72h", fileToUpload: file }),
    link: (text) => text.trim(),
    domain: "catbox.moe",
    keepsMs: 70 * HOUR_MS,
  },
];
// Animated covers: the album's motion artwork, a video Discord cannot show, made a GIF by Ampify on this PC and put on
// the same free hosts. 200 pixels, 15 frames a second (at 10 the motion stuttered): a 20 second loop is about 1.5 MB.
const MOTION_HOST = "mvod.itunes.apple.com";
// A busy video makes a heavy GIF, past what Ampify makes (3 MB, seen with Currents): lighter ones are tried in turn.
const GIFS = [
  { side: 200, fps: 15, seconds: 20 }, { side: 200, fps: 10, seconds: 20 }, { side: 160, fps: 8, seconds: 20 },
  { side: 120, fps: 6, seconds: 12 },
];
const MAX_ANIMATIONS = 100;  // albums whose GIF is kept in storage
const SETTLE_MS = 2000;  // Windows takes a moment to show the new track's cover
const COVER_WAIT_MS = 3000;  // how long a new track waits for its cover before it shows with the logo
const MAX_REMEMBERED = 200;  // tracks whose lookup is kept in storage
// The Apple store of each language (English: the US one); the links then open in it.
const STOREFRONTS = { fr: "fr", de: "de", es: "es", pt: "br", ru: "ru", ja: "jp", zh: "cn", it: "it", ko: "kr" };

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
  it: { paused: "In pausa", unknown: "Titolo sconosciuto", listen: "Ascolta su Apple Music",
        youtube: "Trova su YouTube Music", spotify: "Trova su Spotify", platforms: "Altre piattaforme" },
  ko: { paused: "일시정지됨", unknown: "알 수 없는 제목", listen: "Apple Music에서 듣기",
        youtube: "YouTube Music에서 찾기", spotify: "Spotify에서 찾기", platforms: "다른 플랫폼" },
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
  // An Ampify older than 1.0 cannot make GIFs: the setting does nothing there.
  animatedCovers: setting("animated-covers", false) && typeof Ampify.http.gifFromVideo === "function",
  shareCovers: setting("share-covers", true),
  clickable: setting("clickable-links", true),
  // "song.link" is the name the "Other platforms" choice had before 0.3.
  buttons: [setting("button-1", "Apple Music"), setting("button-2", "Other platforms")].map((name) =>
    (name === "song.link" ? "Other platforms" : name)),
};

// --- Where a track's cover and links come from ---------------------------------------------------------------------

// "lookups" held, before 1.0, a day of "not found" for every explicit song: dropped, they are asked again.
const LOOKUPS_KEY = "lookups.1";
Ampify.storage.remove("lookups");
const lookups = new Map(Object.entries(Ampify.storage.get(LOOKUPS_KEY, {})));
const asking = new Set();

// Lower case, without accents or punctuation, in any script: "Beyoncé - Halo (Live)" ~ "beyonce halo live",
// and a Japanese or Cyrillic title keeps its letters.
function plain(text) {
  return String(text || "")
    .normalize("NFD")
    .replace(/\p{M}/gu, "")
    .toLowerCase()
    .replace(/[^\p{L}\p{N}]+/gu, " ")
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
// An entry kept from before animated covers were on has no motion artwork field: asked again once, for its album's.
function known(track) {
  const found = lookups.get(track.id);
  if (found && found.keptUntil && Date.now() > found.keptUntil) return undefined;
  if (found && options.animatedCovers && found.songId && !found.keptUntil && !("motion" in found)) return undefined;
  return found;
}

function remember(id, found) {
  lookups.delete(id);
  lookups.set(id, found);
  while (lookups.size > MAX_REMEMBERED) lookups.delete(lookups.keys().next().value);
  Ampify.storage.set(LOOKUPS_KEY, Object.fromEntries(lookups));
}

// The words to search: slashes and brackets make the searches miss, "A / B (Live)" is looked up as "A B Live".
function searchWords(track) {
  return (track.artist + " " + track.title).replace(/[/()[\]]+/g, " ").replace(/\s+/g, " ").trim();
}

// The parsed JSON answer of a search, or undefined (logged) when it could not be read.
function answerOf(answer, source) {
  if (answer.status !== 200) {
    Ampify.log("cover: the " + source + " search answered " + answer.status);
    return undefined;
  }
  try {
    return JSON.parse(answer.text);
  } catch {
    Ampify.log("cover: the " + source + " search answered something else than JSON");
    return undefined;
  }
}

// The web player's public token, kept a week in storage: { token, fresh }, token null when it could not be read or
// when a fresh one was refused lately (an Ampify that cannot send headers yet), so that the page is not read again
// for every song.
function webToken() {
  const kept = Ampify.storage.get("webToken", null);
  if (kept && Date.now() < kept.until) return Promise.resolve({ token: kept.token || null, fresh: false });
  return Ampify.http.get(WEB_PLAYER_PAGE)
    .then((page) => {
      const script = page.status === 200 ? (/\/assets\/index[~\w-]*\.js/.exec(page.text) || [])[0] : undefined;
      return script ? Ampify.http.get(WEB_PLAYER + script) : { status: 0, text: "" };
    })
    .then((answer) => {
      const token = answer.status === 200 ? (/eyJ[\w-]+\.eyJ[\w-]+\.[\w-]+/.exec(answer.text) || [])[0] : undefined;
      if (!token) {
        Ampify.log("cover: Apple Music's web player gave no token, the iTunes search is used");
        Ampify.storage.set("webToken", { token: null, until: Date.now() + RETRY_MS });
        return { token: null, fresh: true };
      }
      Ampify.storage.set("webToken", { token, until: Date.now() + TOKEN_KEPT_MS });
      return { token, fresh: true };
    });
}

// The track in Apple Music's catalog, with its artist's page: null when the catalog does not have it, undefined when
// it could not be asked (no token, or a refused one: an old one is read again next time, a fresh one in a day).
function searchCatalog(track) {
  return webToken().then(({ token, fresh }) => {
    if (!token) return undefined;
    const country = STOREFRONTS[Ampify.i18n.language] || "us";
    // Each song's album comes with its motion artwork only (about 13 KB more for the 25 songs).
    const url = CATALOG + country + "/search?types=songs&limit=25&include[songs]=artists,albums" +
      "&fields[albums]=editorialVideo&extend[albums]=editorialVideo&term=" + encodeURIComponent(searchWords(track));
    const headers = { Authorization: "Bearer " + token, Origin: WEB_PLAYER };
    return Ampify.http.get(url, { headers }).then((answer) => {
      if (answer.status === 401 || answer.status === 403) {
        if (fresh) Ampify.storage.set("webToken", { token: null, until: Date.now() + DAY_MS });
        else Ampify.storage.remove("webToken");
      }
      const parsed = answerOf(answer, "Apple Music");
      if (!parsed) return undefined;
      const songs = ((parsed.results || {}).songs || {}).data || [];
      const results = songs.map((song) => {
        const about = song.attributes || {};
        const artist = (((song.relationships || {}).artists || {}).data || [])[0] || {};
        const album = (((song.relationships || {}).albums || {}).data || [])[0] || {};
        const video = (album.attributes || {}).editorialVideo || {};
        return {
          trackName: about.name, artistName: about.artistName, collectionName: about.albumName,
          cover: String((about.artwork || {}).url || "").replace("{w}x{h}", "600x600"),
          song: about.url || "", artist: (artist.attributes || {}).url || "", songId: song.id || 0,
          motion: https((video.motionDetailSquare || video.motionSquareVideo1x1 || {}).video),
        };
      });
      const match = bestMatch(results, track);
      if (!match) {
        Ampify.log("cover: not in Apple Music's catalog (" + results.length + " results)");
        return null;
      }
      return {
        cover: match.cover, song: match.song, artist: match.artist,
        album: match.song.replace(/\?i=\d+$/, ""),  // the song's page is its album's, at the song
        songId: match.songId, motion: match.motion,
      };
    });
  });
}

// The catalog entry of the track, null when the catalog does not have it, undefined when it could not be asked.
function searchApple(track) {
  const words = searchWords(track);
  const country = STOREFRONTS[Ampify.i18n.language] || "us";
  const url = SEARCH + "?media=music&entity=song&limit=25&country=" + country + "&term=" + encodeURIComponent(words);
  return Ampify.http.get(url).then((answer) => {
    const parsed = answerOf(answer, "catalog");
    if (!parsed) return undefined;
    const results = parsed.results || [];
    const match = bestMatch(results, track);
    if (!match) {
      Ampify.log("cover: not in the catalog (" + results.length + " results for \"" + words + "\")");
      return null;
    }
    // The links carry a tracking parameter (uo=4) that the page does not need.
    const clean = (link) => String(link || "").replace(/([?&])uo=\d+&?/, "$1").replace(/[?&]$/, "");
    return {
      cover: String(match.artworkUrl100 || "").replace(/\d+x\d+bb(?=\.\w+$)/, COVER_SIZE),
      song: clean(match.trackViewUrl),
      artist: clean(match.artistViewUrl),
      album: clean(match.collectionViewUrl).replace(/\?i=\d+$/, ""),  // a single's album link points at its song
      songId: match.trackId || 0,
      // Found while the catalog could not be asked: asked again the next day, for the catalog's motion artwork.
      keptUntil: Date.now() + DAY_MS,
    };
  });
}

// The track on Deezer: its album's cover and its id (for song.link). null when Deezer does not have it either,
// undefined when it could not be asked (Deezer answers its quota errors with a 200 and an "error" object).
function searchDeezer(track) {
  const words = searchWords(track);
  return Ampify.http.get(DEEZER_SEARCH + "?limit=25&q=" + encodeURIComponent(words)).then((answer) => {
    const parsed = answerOf(answer, "Deezer");
    if (!parsed) return undefined;
    if (parsed.error) {
      Ampify.log("cover: Deezer refused the search, " + (parsed.error.message || "no reason given"));
      return undefined;
    }
    const results = (parsed.data || []).map((song) => ({
      trackName: song.title,
      artistName: song.artist && song.artist.name,
      collectionName: song.album && song.album.title,
      cover: song.album && song.album.cover_xl,
      deezerId: song.id || 0,
    }));
    const match = bestMatch(results, track);
    if (!match || !https(match.cover)) {
      Ampify.log("cover: not on Deezer either (" + results.length + " results)");
      return null;
    }
    return { cover: match.cover, deezerId: match.deezerId };
  });
}

// Apple Music's catalog first (its links open Apple Music), the iTunes search when the catalog cannot be asked, then
// Deezer for a song Apple's searches miss.
function search(track) {
  return searchCatalog(track)
    .then((found) => (found === undefined ? searchApple(track) : found))
    .then((found) => (found === null ? searchDeezer(track) : found));
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

// True for an https link on `domain` or one of its subdomains.
function onDomain(url, domain) {
  const host = (/^https:\/\/([^/?#]+)\//.exec(url) || [])[1] || "";
  return host === domain || host.endsWith("." + domain);
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
    if (onDomain(url, host.domain)) return { cover: url, keptUntil: Date.now() + host.keepsMs };
    Ampify.log("cover: " + host.domain + " did not take the cover (" + answer.status + ")");
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

// --- Animated covers -------------------------------------------------------------------------------------------------

// The GIFs made, by motion artwork: { cover, keptUntil }, cover "" when none could be made (asked again after
// keptUntil). The songs of an album share theirs.
const ANIMATIONS_KEY = "animations";
const animations = new Map(Object.entries(Ampify.storage.get(ANIMATIONS_KEY, {})));
const animating = new Set();

// The entry of the track's album, undefined when there is none yet (or it expired).
function animation(found) {
  if (!options.animatedCovers || !options.showCover || !found || !found.motion) return undefined;
  const made = animations.get(found.motion);
  return made && Date.now() < made.keptUntil ? made : undefined;
}

function rememberAnimation(motion, made) {
  animations.delete(motion);
  animations.set(motion, made);
  while (animations.size > MAX_ANIMATIONS) animations.delete(animations.keys().next().value);
  Ampify.storage.set(ANIMATIONS_KEY, Object.fromEntries(animations));
}

// `uri` of a playlist line, against the playlist at `base` (Apple names the files of a variant relative to it).
function against(uri, base) {
  if (uri.startsWith("https://")) return uri;
  const page = base.replace(/[?#].*$/, "");
  return uri.startsWith("/") ? page.replace(/^(https:\/\/[^/]+).*$/, "$1") + uri : page.replace(/[^/]*$/, "") + uri;
}

function playlist(url) {
  return Ampify.http.get(url).then((answer) => {
    if (answer.status !== 200) throw new Error("the motion artwork answered " + answer.status);
    return answer.text.split(/\r?\n/);
  });
}

// The video of a motion artwork: of its H.264 variants (Windows decodes HEVC only with a paid extension), the smallest
// of at least the GIF's size, else the largest. Its playlist names one fragmented MP4 file, read whole. null without.
function videoOf(motion) {
  return playlist(motion).then((lines) => {
    const variants = [];
    lines.forEach((line, index) => {
      const info = /^#EXT-X-STREAM-INF:(.*)$/.exec(line);
      const size = info && /CODECS="[^"]*\bavc1\./.test(info[1]) && /RESOLUTION=(\d+)x(\d+)/.exec(info[1]);
      const uri = (lines[index + 1] || "").trim();
      if (size && uri && !uri.startsWith("#")) {
        variants.push({ side: Math.min(Number(size[1]), Number(size[2])), url: against(uri, motion) });
      }
    });
    variants.sort((a, b) => a.side - b.side);
    const chosen = variants.find((variant) => variant.side >= GIFS[0].side) || variants[variants.length - 1];
    if (!chosen) return null;
    return playlist(chosen.url).then((variant) => {
      const map = variant.map((line) => /^#EXT-X-MAP:URI="([^"]+)"/.exec(line)).find(Boolean);
      const file = map ? against(map[1], chosen.url) : "";
      return onDomain(file, MOTION_HOST) ? file : null;
    });
  });
}

// The GIF of the video, made lighter while it would be too big; any other failure is not tried again.
function gifOf(video, index = 0) {
  return Ampify.http.gifFromVideo(video, GIFS[index]).catch((error) => {
    if (index + 1 < GIFS.length && /\bover \d+ MB\b/.test(String(error))) return gifOf(video, index + 1);
    throw error;
  });
}

function animate(motion) {
  if (animating.has(motion)) return;
  animating.add(motion);
  Promise.resolve(motion)
    .then(videoOf)
    .then((video) => {
      if (!video) {
        Ampify.log("cover: the motion artwork has no video Ampify can read");
        return null;
      }
      return gifOf(video)
        .then((gif) => sendTo(0, { filename: "cover.gif", type: gif.type, base64: gif.base64 }));
    })
    .catch((error) => {
      Ampify.log("cover: no animated cover, " + error);
      return undefined;
    })
    .then((made) => {
      // Without a video, asked again the next day (Apple adds them); after a failure, a few minutes later.
      if (made === null) rememberAnimation(motion, { cover: "", keptUntil: Date.now() + DAY_MS });
      else rememberAnimation(motion, made || { cover: "", keptUntil: Date.now() + RETRY_MS });
      show(Ampify.player.state());
    })
    .finally(() => animating.delete(motion));
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
    "Other platforms": () => {
      if (song) return { label: t("platforms"), url: "https://song.link/i/" + song };
      if (found && found.deezerId) return { label: t("platforms"), url: "https://song.link/d/" + found.deezerId };
      return null;
    },
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
    largeImage: (options.showCover &&
      (https((animation(found) || {}).cover) || https(track.artwork) || (found && https(found.cover)))) || LOGO_IMAGE,
    largeText: options.showAlbum && track.album ? track.album : "Apple Music",
    buttons: buttonsFor(track, found),
  };
  if (options.clickable && found && found.songId) {
    activity.detailsUrl = https(found.song) || undefined;
    activity.stateUrl = https(found.artist) || undefined;
    activity.largeUrl = https(found.album) || undefined;
  } else if (options.clickable && found && found.deezerId) {
    // Found on Deezer only: the song and its artist through Apple Music's search.
    activity.detailsUrl = https(searchUrl("https://music.apple.com/search?term=", track)) || undefined;
    activity.stateUrl = https(searchUrl("https://music.apple.com/search?term=", { artist: track.artist, title: "" }))
      || undefined;
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

// A track not looked up yet waits a moment for its cover before Discord is told. Sent with Ampify's logo first, the
// cover that came a second later often did not show until the next song: Discord kept the first image.
let waitingFor = null;  // { id, over }: the track whose cover is awaited, and whether the wait is over

function waitsForCover(track) {
  if (!options.showCover || known(track) !== undefined) return false;
  if (!waitingFor || waitingFor.id !== track.id) {
    const wait = { id: track.id, over: false };
    waitingFor = wait;
    setTimeout(() => {
      wait.over = true;
      if (waitingFor === wait) show(Ampify.player.state());
    }, COVER_WAIT_MS);
  }
  return !waitingFor.over;
}

function show(state) {
  const track = state && state.track;
  if (!track || (!state.playing && !options.showPaused)) {
    Ampify.discord.clear();
    return;
  }
  const found = known(track);
  if (found === undefined) lookUp(track);
  if (waitsForCover(track)) return;
  Ampify.discord.setActivity(activityFor(state));
  if (found && found.motion && options.animatedCovers && options.showCover && !animation(found)) {
    animate(found.motion);
  }
}

Ampify.player.on("trackchange", show);
Ampify.player.on("statechange", show);
show(Ampify.player.state());
Ampify.onStop(() => Ampify.discord.clear());
