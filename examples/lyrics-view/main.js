// Lyrics view: the song playing over the whole window, with its lyrics in time, like Spicetify's full screen and
// lyrics apps, drawn with Ampify.ui.fullscreen. A button of the playback bar opens it; its Close button or Escape
// closes it.
//
// Lyrics: Apple Music's own when it has fetched them (it does once its lyrics panel has been open), otherwise LRCLIB, a
// free public library of synced lyrics. Cover: Apple's when it gives an https address, otherwise the iTunes Search API.

const TICK_MS = 250;
const LINES_BEFORE = 2;
const LINES_AFTER = 4;
const COVER_SIZE = 420;
const LRCLIB_TRIES = 4;
const LRCLIB_RETRY_MS = 5000;
const FADE = "opacity 350ms";
const LRC_TIME = /\[(\d+):(\d+(?:\.\d+)?)\]/g;

let timer = null;
let track = null;        // Ampify.player.state().track of the song shown
let lyrics = null;       // { source, synced, lines: [{ start, text }] } or null
let cover = "";          // https address of the cover, or ""
let searching = false;   // the lyrics of the song shown are still being looked for
let lastIndex = -2;      // the line shown as current at the last render
let clock = { positionMs: 0, at: 0, playing: false };

// ---- Sources -----------------------------------------------------------------------------------------------------

function fromApple() {
  const apple = Ampify.player.lyrics();
  if (!apple || !apple.lines || apple.lines.length === 0) return null;
  return {
    source: "Apple Music",
    synced: apple.synced,
    lines: apple.lines.map((line) => ({ start: apple.synced ? line.start : -1, text: line.text })),
  };
}

// "[01:02.50]text" lines, possibly several times in front of one text.
function parseLrc(text) {
  const lines = [];
  for (const row of text.split("\n")) {
    const times = [...row.matchAll(LRC_TIME)];
    if (times.length === 0) continue;
    const words = row.replace(LRC_TIME, "").trim();
    for (const time of times) lines.push({ start: Math.round((Number(time[1]) * 60 + Number(time[2])) * 1000), text: words });
  }
  return lines.sort((a, b) => a.start - b.start);
}

// "Song (feat. X) [Remastered 2011]" -> "Song": the libraries index the bare title.
function bareTitle(title) {
  return title.replace(/\s*[([][^)\]]*(feat\.|ft\.|with |remaster|version|edit|live)[^)\]]*[)\]]/gi, "").trim() || title;
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// One LRCLIB search; null when nothing matches, an exception when the service does not answer (it is often busy).
async function searchLrclib(query) {
  const answer = await Ampify.http.get("https://lrclib.net/api/search?" + query);
  if (answer.status !== 200) throw new Error("LRCLIB answered " + answer.status);
  const results = JSON.parse(answer.text);
  // When busy, LRCLIB may answer 200 with {"name":"ServerOverloaded"} instead of a list.
  if (!Array.isArray(results)) throw new Error("LRCLIB is busy");
  const found = results.find((item) => item.syncedLyrics || item.plainLyrics);
  if (!found) return null;
  if (found.syncedLyrics) return { source: "LRCLIB", synced: true, lines: parseLrc(found.syncedLyrics) };
  return { source: "LRCLIB", synced: false, lines: found.plainLyrics.split("\n").map((text) => ({ start: -1, text })) };
}

async function fromLrclib(wanted) {
  const title = encodeURIComponent(bareTitle(wanted.title));
  const queries = ["track_name=" + title + "&artist_name=" + encodeURIComponent(wanted.artist),
                   "q=" + encodeURIComponent(wanted.artist + " " + bareTitle(wanted.title))];
  for (let attempt = 0; attempt < LRCLIB_TRIES; attempt++) {
    try {
      for (const query of queries) {
        const found = await searchLrclib(query);
        if (found) return found;
      }
      return null;
    } catch (error) {
      await sleep(LRCLIB_RETRY_MS);
    }
  }
  return null;
}

async function coverOf(wanted) {
  if (wanted.artwork) return wanted.artwork;
  const term = encodeURIComponent(wanted.artist + " " + wanted.title);
  const answer = await Ampify.http.get("https://itunes.apple.com/search?entity=song&limit=1&term=" + term);
  if (answer.status !== 200) return "";
  const result = JSON.parse(answer.text).results[0];
  return result && result.artworkUrl100 ? result.artworkUrl100.replace("100x100bb", "1200x1200bb") : "";
}

// ---- Time --------------------------------------------------------------------------------------------------------

// The player's position moves by steps: between two readings it is carried on with the clock.
function positionNow() {
  const state = Ampify.player.state();
  const now = Date.now();
  if (state.positionMs !== clock.positionMs || state.playing !== clock.playing) {
    clock = { positionMs: state.positionMs, at: now, playing: state.playing };
  }
  return clock.playing ? clock.positionMs + (now - clock.at) : clock.positionMs;
}

function currentIndex(position) {
  if (!lyrics || !lyrics.synced) return -1;
  let index = -1;
  for (let i = 0; i < lyrics.lines.length && lyrics.lines[i].start <= position; i++) index = i;
  return index;
}

// ---- View --------------------------------------------------------------------------------------------------------

function lyricLine(line, index, current) {
  const active = index === current;
  const past = index < current;
  return {
    type: "text",
    key: "line-" + index,
    text: line.text || "♪",
    size: 30,
    weight: active ? 700 : 600,
    color: "#FFFFFFFF",
    // No scale: it grows around the centre, which would shift the start of a left-aligned line.
    opacity: active ? 1 : past ? 0.3 : 0.5,
    transition: FADE,
    maxLines: 2,
  };
}

function lyricsColumn(current) {
  if (!lyrics) {
    const message = !track ? "Nothing is playing" : searching ? "Looking for lyrics…" : "No lyrics found for this song";
    return [{ type: "text", text: message, size: 24, color: "#99FFFFFF" }];
  }
  if (!lyrics.synced) {
    const shown = lyrics.lines.slice(0, 14).map((line, index) => ({ type: "text", key: "plain-" + index, text: line.text || " ", size: 22, color: "#CCFFFFFF" }));
    return shown.concat([{ type: "text", text: "Lyrics from " + lyrics.source + ", without timing", size: 14, color: "#80FFFFFF" }]);
  }
  const from = Math.max(0, current - LINES_BEFORE);
  const to = Math.min(lyrics.lines.length, Math.max(current, 0) + LINES_AFTER + 1);
  const lines = [];
  for (let i = from; i < to; i++) lines.push(lyricLine(lyrics.lines[i], i, current));
  return lines.concat([{ type: "text", text: "Lyrics from " + lyrics.source, size: 14, color: "#80FFFFFF" }]);
}

function view(current) {
  const tint = track && track.artworkColor ? "#FF" + track.artworkColor.slice(1) : "#FF101010";
  const back = cover ? { type: "image", src: cover, fit: "cover" } : { type: "stack", background: tint };
  const details = [
    { type: "text", text: track ? track.title : "Now Playing", size: 30, weight: 700, color: "#FFFFFFFF", maxLines: 2 },
    { type: "text", text: track ? track.artist : "", size: 20, color: "#CCFFFFFF", maxLines: 1 },
    { type: "text", text: track ? track.album : "", size: 16, color: "#99FFFFFF", maxLines: 1 },
  ];
  if (cover) details.unshift({ type: "image", src: cover, fit: "cover", width: COVER_SIZE, height: COVER_SIZE, cornerRadius: 16 });
  return {
    type: "layer",
    children: [
      back,
      { type: "stack", background: "acrylic(#B3101010, 0.6)" },
      {
        type: "row",
        padding: "64,72,64,72",
        gap: 72,
        children: [
          { type: "stack", width: COVER_SIZE, valign: "center", gap: 12, children: details },
          { type: "stack", grow: true, valign: "center", gap: 18, children: lyricsColumn(current) },
        ],
      },
    ],
  };
}

function refresh(force) {
  const index = currentIndex(positionNow());
  if (!force && index === lastIndex) return;
  lastIndex = index;
  Ampify.ui.fullscreen.open(view(index));
}

// ---- Song changes ------------------------------------------------------------------------------------------------

async function load() {
  const state = Ampify.player.state();
  const next = state.track;
  if (track && next && track.id === next.id) return;
  track = next;
  lyrics = null;
  cover = "";
  searching = Boolean(track);
  refresh(true);
  if (!track) return;
  const wanted = track;
  const current = () => track && track.id === wanted.id && timer !== null;  // false once the song or the view changed
  // The cover comes on its own: LRCLIB may take a while when it is busy.
  coverOf(wanted).catch(() => "").then((image) => {
    if (!current()) return;
    cover = image;
    refresh(true);
  });
  const found = fromApple() || (await fromLrclib(wanted).catch(() => null));
  if (!current()) return;
  if (!lyrics) lyrics = found;  // Apple's may have come meanwhile (lyricschange)
  searching = false;
  refresh(true);
}

function stop() {
  if (timer !== null) clearInterval(timer);
  timer = null;
}

function open() {
  stop();
  track = null;
  lastIndex = -2;
  timer = setInterval(() => refresh(false), TICK_MS);
  load();
}

Ampify.player.on("trackchange", () => {
  if (timer !== null) load();
});
// Apple's lyrics may come after the song started: they replace LRCLIB's.
Ampify.player.on("lyricschange", () => {
  if (timer === null) return;
  const apple = fromApple();
  if (apple) {
    lyrics = apple;
    refresh(true);
  }
});
Ampify.ui.fullscreen.onClose(stop);
Ampify.onStop(stop);

Ampify.ui.playbar.addButton({
  label: "Lyrics view",
  icon: "",  // FullScreen, in Segoe Fluent Icons
  onClick: () => (timer === null ? open() : (stop(), Ampify.ui.fullscreen.close())),
});
