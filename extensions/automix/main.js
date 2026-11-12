// AutoMix: each song flows into the next, as in a DJ set. Apple's own analysis of both songs (beats, bars, loudness,
// fades, vocals, the analysis Apple's AutoMix uses) picks where the next song comes in, at which bar of it, and how long
// the two play together; the kernel keeps them in step to the millisecond with a second player and crosses their
// volumes (Ampify.automix). Songs of one album that follow each other and classical music keep Apple's gapless
// playback; songs that cannot be put in step get a smooth crossfade instead.

const CHECK_EVERY_MS = 1000;
const PLAN_AHEAD_S = 45;          // a plan is handed to the kernel once its mix is this close
const LATEST_S = 14;              // closer than this, the next song cannot be put in step in time
const MAX_TRIES = 3;               // a plan cancelled by a pause or a seek is sent again this many times at most
const RETRY_MS = 15000;            // Apple's catalog failed: asked again this much later...
const MAX_FETCH_FAILURES = 3;      // ...this many times, then the songs are planned without its analysis
const END_DRIFT_S = 0.04;         // songs a little apart in tempo drift this much at each end of a mix, no more
const MAX_TEMPO_GAP = 0.03;       // past 3 % apart in tempo, no beat mix
const CROSSFADE_S = 6;
const MIN_CROSSFADE_S = 3;
const MAX_CROSSFADE_S = 12;
const OVER_FADE_GAIN = 0.7;       // the next song's volume once the current one starts fading out by itself
const QUIET_BELOW_DB = 18;        // this far below the song's loudness: silence or a fade, not the music
const MIN_DANCEABILITY = 0.45;
const TOKEN_MAX_AGE_MS = 5 * 24 * 3600 * 1000;
const NO_MIX_GENRES = ["Classical", "Opera", "Spoken Word", "Audiobooks", "Comedy"];
const ACTIVE = ["waiting", "preparing", "aligning", "ready", "taking over", "mixing", "recovering"];

// ---- the plan: pure functions of Apple's analysis (tools/test_automix.mjs runs them)

function median(values) {
  if (!values.length) return NaN;
  const sorted = [...values].sort((x, y) => x - y);
  return sorted[Math.floor(sorted.length / 2)];
}

function seconds(list) {
  return Array.isArray(list) ? list.filter((v) => Number.isFinite(v)).map((v) => v / 1000) : [];
}

function barsOf(song) {
  return seconds(song.analysis?.beats?.barsInMilliseconds);
}

// The usual length of a bar around `time`: the median of the eight bars nearest to it.
function barLength(bars, time) {
  let nearest = 0;
  for (let i = 0; i < bars.length; i++) {
    if (Math.abs(bars[i] - time) < Math.abs(bars[nearest] - time)) nearest = i;
  }
  const from = Math.max(1, nearest - 4);
  const steps = [];
  for (let i = from; i < Math.min(bars.length, from + 8); i++) steps.push(bars[i] - bars[i - 1]);
  return median(steps);
}

// Where a song is first heard (`sound`), where its music is in full (`start`, past its fade-in), where it starts
// fading out (`end`) and where it has faded to nothing (`silent`). Apple's loudness curve has a value every half
// second (a second's window each); without an analysis the whole song is music.
function musicalSpan(song) {
  const analysis = song.analysis ?? {};
  const fades = analysis.fades ?? {};
  let sound = (fades.fadeIn?.startInMilliseconds ?? 0) / 1000;
  let start = (fades.fadeIn?.endInMilliseconds ?? 0) / 1000;
  let end = (fades.fadeOut?.startInMilliseconds ?? song.duration * 1000) / 1000;
  let silent = (fades.fadeOut?.endInMilliseconds ?? song.duration * 1000) / 1000;
  const curve = analysis.loudnessCurve?.value;
  if (Array.isArray(curve) && curve.length > 8 && song.duration > 0) {
    const loud = analysis.loudness?.main?.value ?? median(curve);
    const step = song.duration / curve.length;
    const first = curve.findIndex((db) => db > loud - QUIET_BELOW_DB);
    let last = curve.length - 1;
    while (last > 0 && !(curve[last] > loud - QUIET_BELOW_DB)) last--;
    if (first >= 0) {
      sound = Math.max(sound, first * step);
      start = Math.max(start, first * step);
    }
    end = Math.min(end, (last + 1) * step);
    silent = Math.min(silent, (last + 1) * step);
  }
  if (!(end > start)) return { sound: 0, start: 0, end: song.duration, silent: song.duration };
  end = Math.min(end, song.duration);
  return { sound: Math.min(sound, start), start, end, silent: Math.min(Math.max(silent, end), song.duration) };
}

// How much of [from, to] the singer fills with "high" or "very-high" activity, 0 to 1.
function vocalShare(song, from, to) {
  const spans = song.analysis?.vocalActivity;
  if (!Array.isArray(spans) || !(to > from)) return 0;
  let sung = 0;
  for (const span of spans) {
    if (span.strength !== "high" && span.strength !== "very-high") continue;
    const a = Math.max(from, span.startInMilliseconds / 1000);
    const b = Math.min(to, span.endInMilliseconds / 1000);
    if (b > a) sung += b - a;
  }
  return sung / (to - from);
}

function danceable(song, part) {
  const value = song.analysis?.danceability?.[part] ?? song.analysis?.danceability?.main;
  return Number.isFinite(value) && value >= MIN_DANCEABILITY;
}

function sameAlbumInOrder(a, b) {
  const x = a.attributes ?? {};
  const y = b.attributes ?? {};
  return Boolean(x.albumName) && x.albumName === y.albumName && x.artistName === y.artistName &&
    (y.discNumber ?? 1) === (x.discNumber ?? 1) && y.trackNumber === (x.trackNumber ?? -1) + 1;
}

function noMixGenre(song) {
  return (song.attributes?.genreNames ?? []).some((genre) => NO_MIX_GENRES.includes(genre));
}

// Bars a beat mix lasts, by style, from the longest to the shortest tried.
function barChoices(style) {
  if (style === "Long blends") return [16, 8, 4];
  if (style === "Short blends") return [4, 2];
  return [8, 4, 2];
}

// A mix on the beat: the next song comes in on a bar, its own bars on the current song's, under the current song's
// last bars; the two cross three quarters in. Null when the songs cannot share a beat.
function beatMix(a, b, spanA, spanB, options) {
  const barsA = barsOf(a);
  const barsB = barsOf(b);
  if (barsA.length < 8 || barsB.length < 8 || !danceable(a, "ending") || !danceable(b, "beginning")) return null;
  const barA = barLength(barsA, spanA.end);
  const barB = barLength(barsB, spanB.start);
  const gap = Math.abs(1 - barA / barB);
  if (!(gap <= MAX_TEMPO_GAP)) return null;
  // Lined up on the middle bar, the beats drift apart by gap * time on each side: the most at the ends, where one
  // song is nearly silent, half that where both are loud.
  const longest = Math.min(options.maxSeconds, gap > 0 ? END_DRIFT_S / (0.5 * gap) : Infinity);
  let last = barsA.length - 1;
  while (last > 0 && barsA[last] > spanA.end) last--;
  let first = barsB.findIndex((t) => t >= spanB.start - 0.05);
  if (first < 0) return null;
  first = Math.ceil(first / 4) * 4;  // a phrase starts on a fourth bar
  for (const count of barChoices(options.style)) {
    const from = last - count;
    const aStart = barsA[from];
    const aEnd = barsA[last];
    const length = aEnd - aStart;
    if (from < 0 || length > longest || aStart < options.earliest) continue;
    const middle = count / 2;
    // Lined up on the middle bar, a slightly faster next song would start before its first bar: then a phrase later.
    const entry = [first, first + 4].find((bar) => bar + count < barsB.length &&
      barsB[bar + middle] - (barsA[from + middle] - aStart) >= 0);
    if (entry === undefined) continue;
    const bStart = barsB[entry + middle] - (barsA[from + middle] - aStart);
    // Two singers at once for long sounds like a clash: then only the shortest mix.
    const clash = vocalShare(a, aStart, aEnd) > 0.4 && vocalShare(b, bStart, bStart + length) > 0.4;
    if (clash && count > 2 && barChoices(options.style).includes(2)) continue;
    const keys = [
      [aStart, 1, 0],
      [aStart + length * 0.5, 1, 0.5],
      [aStart + length * 0.75, 0.5, 1],
      [aEnd, 0, 1],
    ];
    return { kind: "beat", bars: count, plan: { bStart, keys } };
  }
  return null;
}

// A crossfade on the music itself: the next song, from where it is first heard, comes in under the current song's
// last seconds and is at full once the current one has faded to nothing. When the current song fades out by itself,
// the next one is nearly full when that fade begins, so no hole opens between them.
function crossfade(a, b, spanA, spanB, options) {
  const aEnd = Math.min(a.duration - 0.25, spanA.silent);
  const fade = aEnd - spanA.end;
  const wanted = fade > 0.5 ? fade + 1 : CROSSFADE_S;
  const length = Math.min(options.maxSeconds, MAX_CROSSFADE_S, Math.max(MIN_CROSSFADE_S, wanted));
  const aStart = aEnd - length;
  if (!(aStart > options.earliest) || !(spanB.sound + length < b.duration)) return null;
  const keys = fade > 0.5 && spanA.end - aStart >= 1
    ? [[aStart, 1, 0], [spanA.end, 1, OVER_FADE_GAIN], [aEnd, 0, 1]]
    : [[aStart, 1, 0], [aEnd, 0, 1]];
  return { kind: "crossfade", bars: 0, plan: { bStart: spanB.sound, keys } };
}

// The mix from song `a` into song `b`, or why there is none. A song: { duration, analysis, attributes }, where the
// analysis and the attributes may be missing. options: { style, maxSeconds, albums, earliest (A's earliest start) }.
function planMix(a, b, options) {
  if (options.style === "Off") return { kind: "none", why: "off" };
  if (options.albums && sameAlbumInOrder(a, b)) return { kind: "none", why: "the next song of the same album" };
  if (noMixGenre(a) || noMixGenre(b)) return { kind: "none", why: "classical music or speech" };
  const spanA = musicalSpan(a);
  const spanB = musicalSpan(b);
  const beat = options.style === "Crossfade only" ? null : beatMix(a, b, spanA, spanB, options);
  return beat ?? crossfade(a, b, spanA, spanB, options) ?? { kind: "none", why: "no room for a mix" };
}

// ---- Apple's catalog: who the songs are and their analysis

let tokenPromise = null;

// The token of Apple's web player, as music.apple.com hands it to every visitor. Kept a few days.
async function webToken() {
  const saved = Ampify.storage.get("token", null);
  if (saved && Date.now() - saved.at < TOKEN_MAX_AGE_MS) return saved.value;
  const page = await Ampify.http.get("https://music.apple.com/us/browse");
  const script = /\/assets\/index[~-][\w.-]+\.js/.exec(page.text ?? "");
  if (!script) throw new Error(`Apple's web page has no player script (status ${page.status})`);
  const code = await Ampify.http.get(`https://music.apple.com${script[0]}`);
  const token = /"(eyJ[\w-]+\.[\w-]+\.[\w-]+)"/.exec(code.text ?? "");
  if (!token) throw new Error(`no token in Apple's player script (status ${code.status})`);
  Ampify.storage.set("token", { value: token[1], at: Date.now() });
  return token[1];
}

function token() {
  if (!tokenPromise) tokenPromise = webToken().finally(() => { tokenPromise = null; });
  return tokenPromise;
}

// Apple numbers its stores; the catalog wants their country codes.
const STORES = {
  143441: "us", 143442: "fr", 143443: "de", 143444: "gb", 143445: "at", 143446: "be", 143447: "fi", 143448: "gr",
  143449: "ie", 143450: "it", 143451: "lu", 143452: "nl", 143453: "pt", 143454: "es", 143455: "ca", 143456: "se",
  143457: "no", 143458: "dk", 143459: "ch", 143460: "au", 143461: "nz", 143462: "jp", 143463: "hk", 143464: "sg",
  143465: "cn", 143466: "kr", 143467: "in", 143468: "mx", 143469: "ru", 143470: "tw", 143478: "pl", 143480: "tr",
  143503: "br", 143505: "ar",
};

function storefronts(song) {
  const local = (Ampify.locale ?? "").split("-")[1]?.toLowerCase();
  return [...new Set([STORES[song.storefront], local, "us"].filter(Boolean))];
}

const songs = new Map();  // catalog id -> Promise<{ attributes, analysis } | null>

// The song in the first store that has it, or null when none does. Any other answer (no network, a refused token,
// a busy server) throws, so that the song is asked again later instead of staying without its analysis.
async function fetchSong(id, places) {
  for (const place of places) {
    const url = `https://amp-api.music.apple.com/v1/catalog/${place}/songs/${id}` +
      "?include=audio-analysis&extend[audio-analysis]=fades,loudnessCurve";
    const headers = { authorization: `Bearer ${await token()}`, origin: "https://music.apple.com" };
    const reply = await Ampify.http.get(url, { headers });
    if (reply.status === 404) continue;
    if (reply.status !== 200) {
      if (reply.status === 401 || reply.status === 403) Ampify.storage.remove("token");
      throw new Error(`Apple's catalog answered ${reply.status}`);
    }
    const item = JSON.parse(reply.text).data?.[0];
    if (!item) continue;
    const analysis = item.relationships?.["audio-analysis"]?.data?.[0]?.attributes ?? null;
    return { attributes: item.attributes ?? {}, analysis };
  }
  return null;
}

function catalogSong(song) {
  if (!song.id) return Promise.resolve(null);
  if (!songs.has(song.id)) {
    const asked = fetchSong(song.id, storefronts(song));
    asked.catch(() => songs.delete(song.id));
    songs.set(song.id, asked);
    while (songs.size > 24) songs.delete(songs.keys().next().value);
  }
  return songs.get(song.id);
}

// ---- following the queue

let decidedFor = "";   // current and next song a decision was made for
let decision = null;   // { plan, kind, bars, title, sent, tries }
let retryAt = 0;       // no new decision before this time (Date.now()), after the catalog failed
let failures = { key: "", count: 0 };

function keyOf(state) {
  return `${state.current.name}\n${state.next.name}`;
}

function minutes(time) {
  const whole = Math.max(0, Math.round(time));
  return `${Math.floor(whole / 60)}:${String(whole % 60).padStart(2, "0")}`;
}

function options(state) {
  return {
    style: Ampify.config.get("style"),
    maxSeconds: Ampify.config.get("longest"),
    albums: Ampify.config.get("albums"),
    earliest: state.current.time + LATEST_S + 1,
  };
}

// Both songs' analysis; a failed request throws so that check() asks again later, until the last try, after which
// the songs are planned without it.
async function analyses(state, key) {
  const results = await Promise.allSettled([catalogSong(state.current), catalogSong(state.next)]);
  const failed = results.find((result) => result.status === "rejected");
  if (failed) {
    if (failures.key !== key) failures = { key, count: 0 };
    if (++failures.count < MAX_FETCH_FAILURES) throw failed.reason;
    Ampify.log(`Apple's analysis could not be read (${failed.reason?.message ?? failed.reason}): mixing without it`);
  }
  return results.map((result) => (result.status === "fulfilled" ? result.value : null));
}

async function decide(state, key) {
  // "Repeat one" queues the same song again: it starts over in full, as asked.
  if (state.next.name === state.current.name) {
    Ampify.log("No mix: the song repeats");
    decision = { plan: null };
    return;
  }
  const [infoA, infoB] = await analyses(state, key);
  if (key !== decidedFor) return;  // the queue moved on meanwhile
  const now = Ampify.automix.state();  // the time and lengths as they are after the wait
  if (!now.current || !now.next || keyOf(now) !== key) return;
  const a = { duration: now.current.duration, analysis: infoA?.analysis, attributes: infoA?.attributes };
  const b = { duration: now.next.duration, analysis: infoB?.analysis, attributes: infoB?.attributes };
  if (!(a.duration > 0) || !(b.duration > 0)) {
    decidedFor = "";  // a length Apple's player does not know yet: decided again at the next look
    return;
  }
  const result = planMix(a, b, options(now));
  const title = infoB?.attributes?.name ?? "the next song";
  if (!result.plan) {
    Ampify.log(`No mix into "${title}": ${result.why}`);
    decision = { plan: null };
    return;
  }
  decision = { ...result, title, plan: { next: now.next.name, ...result.plan }, sent: false, tries: 0 };
}

function check() {
  const state = Ampify.automix.state();
  if (!state.available || !state.current || !state.next || !state.playing) return;
  if (ACTIVE.includes(state.mix.phase)) return;
  const key = keyOf(state);
  if (key !== decidedFor) {
    if (Date.now() < retryAt) return;
    decidedFor = key;
    decision = null;
    decide(state, key).catch((error) => {
      Ampify.log(`AutoMix could not plan: ${error.message ?? error}`);
      if (decidedFor === key) decidedFor = "";
      retryAt = Date.now() + RETRY_MS;
    });
    return;
  }
  if (!decision?.plan) return;
  const lead = decision.plan.keys[0][0] - state.current.time;
  if (lead > PLAN_AHEAD_S || lead < LATEST_S) return;
  // A pause, a seek or a skip cancels a plan, and the kernel may refuse one for a passing reason: sent again once the
  // song is back before its mix, a few times at most.
  if ((decision.sent && state.mix.phase !== "failed") || decision.tries >= MAX_TRIES) return;
  decision.tries++;
  const result = Ampify.automix.schedule(decision.plan);
  decision.sent = result.ok;
  const what = decision.kind === "beat" ? `${decision.bars} bars on the beat` : "a crossfade";
  Ampify.log(result.ok
    ? `Mix into "${decision.title}" at ${minutes(decision.plan.keys[0][0])}: ${what}, from its ${minutes(decision.plan.bStart)}`
    : `Mix into "${decision.title}" refused: ${result.reason}`);
}

if (typeof Ampify.automix?.state === "function" && Ampify.process === "engine") {
  const timer = setInterval(() => {
    try {
      check();
    } catch (error) {
      Ampify.log(`AutoMix: ${error.message ?? error}`);
    }
  }, CHECK_EVERY_MS);
  Ampify.onStop(() => {
    clearInterval(timer);
    Ampify.automix.cancel("AutoMix was switched off");
  });
}
