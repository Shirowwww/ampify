// Mini player: Apple's own mini player, easier to reach and to live with. A shortcut opens and closes it from any
// program, its window can be see-through, a star marks the song playing as a favourite, and the album's animated
// cover, which Apple only shows on album pages, plays over its artwork. Ampify's tray icon has its own entry too.

const SHORTCUTS = ["Ctrl+Shift+Alt+M", "Ctrl+Alt+M"];  // the setting's choices, "None" aside
const WEB_PLAYER = "https://music.apple.com";
const WEB_PLAYER_PAGE = WEB_PLAYER + "/us/browse";
const CATALOG = "https://amp-api.music.apple.com/v1/catalog/";
const MOTION_HOST = "mvod.itunes.apple.com";
const STOREFRONTS = { fr: "fr", de: "de", es: "es", pt: "br", ru: "ru", ja: "jp", zh: "cn", it: "it", ko: "kr" };
const HOUR_MS = 60 * 60 * 1000;
const TOKEN_KEPT_MS = 7 * 24 * HOUR_MS;
// The artwork grows into a 350 px album view, up to 525 pixels at 150 %: the smallest H.264 video at least this
// side (Windows decodes HEVC only with a paid extension), else the largest one.
const VIDEO_SIDE = 486;

// The song's album video, by catalog id of the song: the MP4 file's address, or "" when its album has none.
const motions = new Map();
let wanted = "";  // the catalog id whose video is being looked for

const options = {
  opacity: parseInt(String(Ampify.config.get("opacity")), 10) / 100 || 1,  // "80 %"
  favorite: Ampify.config.get("favorite") === true,
  motion: null,
};

function apply() {
  Ampify.ui.miniPlayer.set(options);
}

const shortcut = String(Ampify.config.get("shortcut"));
if (SHORTCUTS.includes(shortcut)) {
  try {
    Ampify.keyboard.bind(shortcut, () => Ampify.ui.miniPlayer.toggle());
  } catch (error) {
    Ampify.log("shortcut " + shortcut + " not available: " + error);
  }
}
apply();

// The web player's public token, kept a week in storage; null when it could not be read (asked again in an hour).
function webToken() {
  const kept = Ampify.storage.get("webToken", null);
  if (kept && Date.now() < kept.until) return Promise.resolve(kept.token || null);
  return Ampify.http.get(WEB_PLAYER_PAGE)
    .then((page) => {
      const script = page.status === 200 ? (/\/assets\/index[~\w-]*\.js/.exec(page.text) || [])[0] : undefined;
      return script ? Ampify.http.get(WEB_PLAYER + script) : { status: 0, text: "" };
    })
    .then((answer) => {
      const token = answer.status === 200 ? (/eyJ[\w-]+\.eyJ[\w-]+\.[\w-]+/.exec(answer.text) || [])[0] : undefined;
      Ampify.storage.set("webToken", { token: token || null, until: Date.now() + (token ? TOKEN_KEPT_MS : HOUR_MS) });
      return token || null;
    });
}

// `uri` of a playlist line, against the playlist at `base` (Apple names the files of a variant relative to it).
function against(uri, base) {
  if (uri.startsWith("https://")) return uri;
  if (uri.startsWith("//")) return "https:" + uri;
  const page = base.replace(/[?#].*$/, "");
  return uri.startsWith("/") ? page.replace(/^(https:\/\/[^/]+).*$/, "$1") + uri : page.replace(/[^/]*$/, "") + uri;
}

function onMotionHost(url) {
  return typeof url === "string" && url.startsWith("https://" + MOTION_HOST + "/");
}

function playlist(url) {
  return Ampify.http.get(url).then((answer) => {
    if (answer.status !== 200) throw new Error("the animated cover answered " + answer.status);
    return answer.text.split(/\r?\n/);
  });
}

// The MP4 file of the motion artwork's playlist: its H.264 variant closest above VIDEO_SIDE names one fragmented MP4
// file, read whole by Ampify. "" without one.
function videoOf(master) {
  return playlist(master).then((lines) => {
    const variants = [];
    lines.forEach((line, index) => {
      const info = /^#EXT-X-STREAM-INF:(.*)$/.exec(line);
      const size = info && /CODECS="[^"]*\bavc1\./.test(info[1]) && /RESOLUTION=(\d+)x(\d+)/.exec(info[1]);
      const uri = (lines[index + 1] || "").trim();
      if (size && uri && !uri.startsWith("#")) {
        variants.push({ side: Math.min(Number(size[1]), Number(size[2])), url: against(uri, master) });
      }
    });
    variants.sort((a, b) => a.side - b.side);
    const chosen = variants.find((variant) => variant.side >= VIDEO_SIDE) || variants[variants.length - 1];
    if (!chosen) return "";
    return playlist(chosen.url).then((variant) => {
      const map = variant.map((line) => /^#EXT-X-MAP:(?:.*,)?URI="([^"]+)"/.exec(line)).find(Boolean);
      const file = map ? against(map[1], chosen.url) : "";
      return onMotionHost(file) && file.endsWith(".mp4") ? file : "";
    });
  });
}

// The motion artwork of the song's album in the catalog: the master playlist's address, "" when it has none, null when
// the catalog could not say (no token, an error): asked again for the next song.
function albumMotion(catalogId) {
  return webToken().then((token) => {
    if (!token) return null;
    const country = STOREFRONTS[Ampify.i18n.language] || "us";
    const url = CATALOG + country + "/songs/" + catalogId +
      "?include=albums&fields[albums]=editorialVideo&extend[albums]=editorialVideo";
    const headers = { Authorization: "Bearer " + token, Origin: WEB_PLAYER };
    return Ampify.http.get(url, { headers }).then((answer) => {
      if (answer.status === 401 || answer.status === 403) Ampify.storage.remove("webToken");
      if (answer.status !== 200) return null;
      const song = (JSON.parse(answer.text).data || [])[0] || {};
      const album = (((song.relationships || {}).albums || {}).data || [])[0] || {};
      const video = (album.attributes || {}).editorialVideo || {};
      const master = (video.motionDetailSquare || video.motionSquareVideo1x1 || {}).video;
      return onMotionHost(master) ? master : "";
    });
  });
}

function show(catalogId, file) {
  if (catalogId !== wanted) return;  // another song started meanwhile
  const motion = file || null;
  if (motion === options.motion) return;
  options.motion = motion;
  apply();
}

// The catalog id of the song playing, once Apple's queue names it: "" for a file of one's own, null while the queue
// still names the song before it (it follows the player by a moment, longer right after Apple Music starts).
function catalogIdOf(track) {
  const queue = Ampify.player.queue.list();
  const current = (queue.items || []).find((item) => item.section === "current");
  if (!current || current.title !== track.title) return null;
  return /^\d+$/.test(String(current.catalogId || "")) ? String(current.catalogId) : "";
}

function lookUp(catalogId) {
  wanted = catalogId;
  if (!catalogId) return show(catalogId, "");  // Apple's still cover
  if (motions.has(catalogId)) return show(catalogId, motions.get(catalogId));
  // The cover of the song before stays until this one's is known: an album's songs share one, without a flash.
  albumMotion(catalogId)
    .then((master) => (master ? videoOf(master) : master))
    .then((file) => {
      if (file !== null) motions.set(catalogId, file);
      show(catalogId, file);
    })
    .catch((error) => {
      Ampify.log("animated cover not found: " + error);
      show(catalogId, "");
    });
}

// Called on every song change and progress tick: settles each song once, when the queue names it, or gives up after a
// few ticks and shows Apple's still cover (never the cover of the song the queue still names).
const QUEUE_TRIES = 8;
let followed = null;  // the song (Apple's queue entry) being followed
let tries = 0;
let settled = false;

function followSong() {
  const track = Ampify.player.state().track;
  const key = track ? String(track.queueItemId || track.id) : "";
  if (key !== followed) {
    followed = key;
    tries = 0;
    settled = !track;
    if (!track) lookUp("");
  }
  if (settled) return;
  tries += 1;
  const catalogId = catalogIdOf(track);
  if (catalogId === null && tries < QUEUE_TRIES) return;
  settled = true;
  lookUp(catalogId || "");
}

if (Ampify.config.get("motion") === true) {
  followSong();
  Ampify.player.on("trackchange", followSong);
  Ampify.player.on("progress", followSong);
}
