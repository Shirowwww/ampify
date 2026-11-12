// AutoMix: each song flows into the next, as in a DJ set. Apple's own analysis of both songs (beats, bars, phrases,
// key, loudness, fades, vocals: the analysis Apple's AutoMix uses) picks where the next song comes in, at which bar of
// it, and how long the two play together. The kernel (Ampify.automix) brings the current song to the next one's tempo
// at its own pitch, keeps the two in step with a second player, crosses their volumes and hands the bass from one to
// the other. Songs of one album that follow each other and classical music keep Apple's gapless playback; songs that
// cannot share a tempo swap on a bar when both have a beat, and get a smooth crossfade otherwise.

const CHECK_EVERY_MS = 1000;
const PLAN_AHEAD_S = 45;          // a plan is handed to the kernel once its mix is this close
const LATEST_S = 14;              // closer than this, the next song cannot be put in step in time
const MAX_TRIES = 3;               // a plan cancelled by a pause or a seek is sent again this many times at most
const RETRY_MS = 15000;            // Apple's catalog failed: asked again this much later...
const MAX_FETCH_FAILURES = 3;      // ...this many times, then the songs are planned without its analysis
const MAX_STRETCH = 0.08;         // the current song plays this much faster or slower at most to share a tempo
const NO_STRETCH = 0.002;         // tempos this close are left alone: the bars drift 20 ms at most over a long mix
const SHORT_ABOVE = 0.05;         // past this much stretch, or when the keys clash, a short mix
const SHORT_BARS = 4;
const RAMP_S = 8;                 // the current song reaches its new speed over this long, before the mix
const MIN_RAMP_S = 4;
const END_ROOM_S = 3.1;           // the kernel wants 3 s of the current song after a mix...
const STRETCH_ROOM_S = 0.3;       // ...and this much more after a stretched one
const BASS_SWAP = [0.45, 0.55];   // where in a mix the bass goes from the current song to the next
const PHRASE_SCORE = 600;         // Apple's structure scores every beat: a phrase starts where it gives this or more
const NO_MELODY = 0.25;           // two songs under this melodicness have no keys to clash
const CROSSFADE_S = 6;
const MIN_CROSSFADE_S = 3;
const MAX_CROSSFADE_S = 12;
const OWN_FADE_S = 4;             // a song quiet for this long at its end fades out by itself; less is a last note dying
const SWAP_IN_S = 0.06;          // in a swap on a bar, the next song goes from silence to full in this long...
const SWAP_AHEAD_S = 0.02;        // ...and is in full this long before the bar, so its first beat is heard whole
const OVER_FADE_GAIN = 0.7;       // the next song's volume once the current one starts fading out by itself
const QUIET_BELOW_DB = 18;        // this far below the song's loudness: silence or a fade, not the music
const MIN_DANCEABILITY = 0.15;    // Apple scores most rap 0.1 to 0.4: higher, hardly any pair mixes on the beat
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

// One bar of the current song against the next song's: over how many of them it runs (70 against 140 bpm is one
// pulse: a half, one or two), and the speed at which the current song's bars last exactly that.
function tempoMatch(barA, barB) {
  const fits = [0.5, 1, 2].map((span) => ({ span, rate: barA / (barB * span) }));
  return fits.reduce((x, y) => (Math.abs(Math.log(y.rate)) < Math.abs(Math.log(x.rate)) ? y : x));
}

// A key's place on the circle of fifths (the DJ's Camelot wheel: 1 to 12, minor or major), null when unknown.
function camelot(key) {
  const tonic = /^([A-G])([#b♯♭]?)$/.exec(key?.tonic ?? "");
  if (!tonic || (key.mode !== "major" && key.mode !== "minor")) return null;
  const shift = { "": 0, "#": 1, "♯": 1, b: -1, "♭": -1 }[tonic[2]];
  const major = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 }[tonic[1]] + shift + (key.mode === "minor" ? 3 : 0);
  return { number: (((7 + major * 7) % 12) + 12) % 12 + 1, minor: key.mode === "minor" };
}

// Keys agree when they are the same, relative (A minor and C major) or a fifth apart in the same mode. Unknown keys
// and two songs with little melody never clash.
function keysClash(a, b) {
  const part = (song, name, which) => song.analysis?.[name]?.[which] ?? song.analysis?.[name]?.main;
  const x = camelot(part(a, "key", "ending"));
  const y = camelot(part(b, "key", "beginning"));
  if (!x || !y) return false;
  if ((part(a, "melodicness", "ending") ?? 1) < NO_MELODY && (part(b, "melodicness", "beginning") ?? 1) < NO_MELODY) {
    return false;
  }
  const steps = Math.min((x.number - y.number + 12) % 12, (y.number - x.number + 12) % 12);
  return !(steps === 0 || (steps === 1 && x.minor === y.minor));
}

// The bars on which a phrase of the song starts, from Apple's structure; empty without it.
function phraseBars(song, bars) {
  const events = song.structure?.videoEvents;
  if (!Array.isArray(events?.score) || !Array.isArray(events?.timeInSeconds)) return [];
  const found = [];
  events.score.forEach((score, i) => {
    if (!(score >= PHRASE_SCORE)) return;
    const bar = bars.findIndex((time) => Math.abs(time - events.timeInSeconds[i]) < 0.1);
    if (bar >= 0 && !found.includes(bar)) found.push(bar);
  });
  return found.sort((x, y) => x - y);
}

// The volumes and the lows of a mix on the beat, as [time, A, B, A's lows, B's lows]: the next song comes in without
// its bass, takes it over around the middle, and the current song leaves once the next one is in full.
function blendKeys(aStart, aEnd) {
  const at = (x) => aStart + (aEnd - aStart) * x;
  return [
    [aStart, 1, 0, 1, 0],
    [at(BASS_SWAP[0]), 1, 0.7, 1, 0],
    [at(BASS_SWAP[1]), 1, 0.9, 0, 1],
    [at(0.8), 0.5, 1, 0, 1],
    [aEnd, 0, 1, 0, 1],
  ];
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

// Seconds of the current song a mix must leave after it: the kernel's second player runs ahead of what is heard, more
// so under a slowed song, and stops once it is through the song. `ramp` and `mix` are seconds of the song at most.
function endRoom(rate = 1, ramp = 0, mix = 0) {
  const carried = 2 * ramp / (1 + rate) + mix / rate;
  return END_ROOM_S + (rate === 1 ? 0 : STRETCH_ROOM_S) + Math.max(0, 1 - rate) * carried;
}

// Why two songs cannot be put together on their beats, or "" when they can: both need bars and a beat to hear.
function beatless(a, b, barsA, barsB) {
  if (barsA.length < 8 || barsB.length < 8) return "Apple gives one of them no bars";
  if (!danceable(a, "ending")) return "this song ends with little beat";
  if (!danceable(b, "beginning")) return "it starts with little beat";
  return "";
}

// Bars a beat mix lasts, by style, from the longest to the shortest tried.
function barChoices(style) {
  if (style === "Long blends") return [16, 8, 4];
  if (style === "Short blends") return [4, 2];
  return [8, 4, 2];
}

// A mix on the beat: the current song, brought to the next one's tempo, ends on a phrase; the next song comes in on
// one of its own phrases, bar on bar, under the current song's last bars. Null when the songs cannot share a tempo.
function beatMix(a, b, spanA, spanB, options) {
  const barsA = barsOf(a);
  const barsB = barsOf(b);
  if (beatless(a, b, barsA, barsB)) return null;
  const match = tempoMatch(barLength(barsA, spanA.end), barLength(barsB, spanB.start));
  if (!(Math.abs(match.rate - 1) <= MAX_STRETCH)) return null;
  const stretched = Math.abs(match.rate - 1) > NO_STRETCH;
  const rate = stretched ? match.rate : 1;
  const latest = Math.min(spanA.end, a.duration - endRoom(rate, RAMP_S, options.maxSeconds * rate));
  let last = barsA.length - 1;
  while (last > 0 && barsA[last] > latest) last--;
  const phraseEnd = phraseBars(a, barsA).filter((bar) => bar <= last).at(-1);
  if (phraseEnd !== undefined && last - phraseEnd < 4) last = phraseEnd;
  const first = barsB.findIndex((t) => t >= spanB.start - 0.05);
  if (first < 0) return null;
  const phrases = phraseBars(b, barsB).filter((bar) => bar >= first);
  const fourth = Math.ceil(first / 4) * 4;  // without Apple's structure, a phrase starts on a fourth bar
  const entries = phrases.length ? phrases.slice(0, 2) : [fourth, fourth + 4];
  const choices = barChoices(options.style);
  const short = Math.abs(rate - 1) > SHORT_ABOVE || keysClash(a, b);
  const counts = short && choices.some((count) => count <= SHORT_BARS)
    ? choices.filter((count) => count <= SHORT_BARS) : choices;
  for (const count of counts) {
    const from = last - count;
    if (from < 0) continue;
    const aStart = barsA[from];
    const aEnd = barsA[last];
    const length = (aEnd - aStart) / rate;
    const ramp = stretched ? Math.min(RAMP_S, aStart - options.earliest) : 0;
    if (length > options.maxSeconds || aStart < options.earliest || (stretched && ramp < MIN_RAMP_S)) continue;
    // Lined up on the middle bar: what is left of a tempo gap shows half as much at each end.
    const middle = count / 2;
    const across = middle * match.span;
    if (!Number.isInteger(across)) continue;
    const startFor = (bar) => barsB[bar + across] - (barsA[from + middle] - aStart) / rate;
    const entry = entries.find((bar) => bar + count * match.span < barsB.length && startFor(bar) >= 0);
    if (entry === undefined) continue;
    const bStart = startFor(entry);
    // Two singers at once for long sounds like a clash: then only the shortest mix.
    const clash = vocalShare(a, aStart, aEnd) > 0.4 && vocalShare(b, bStart, bStart + length) > 0.4;
    if (clash && count > 2 && counts.includes(2)) continue;
    const plan = { bStart, keys: blendKeys(aStart, aEnd), ...(stretched ? { rate, rateFrom: aStart - ramp } : {}) };
    return { kind: "beat", bars: count, plan };
  }
  return null;
}

// A crossfade on the music itself: the next song, from where it is first heard, comes in under the current song's
// last seconds and is at full once the current one has faded to nothing. When the current song fades out by itself,
// the next one is nearly full when that fade begins, so no hole opens between them.
function crossfade(a, b, spanA, spanB, options) {
  const aEnd = Math.min(a.duration - endRoom(), spanA.silent);
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

// A swap on a bar, for songs with a beat that cannot share a tempo: the current song loses its bass over its last
// bar, the next one comes in at full with its first bar on that bar's end, and the current song is gone a beat later.
// Two beats that do not agree are heard together for that one beat only. Null when the current song fades out by
// itself (a crossfade follows that fade) or the next song has nothing before its first bar to come in from.
function barSwap(a, b, spanA, spanB, options) {
  const barsA = barsOf(a);
  const barsB = barsOf(b);
  if (beatless(a, b, barsA, barsB) || spanA.silent - spanA.end >= OWN_FADE_S) return null;
  const bar = barLength(barsA, spanA.end);
  const tail = bar / 4;
  const latest = Math.min(spanA.end, a.duration - endRoom()) - tail;
  const swapAt = barsA.findLast((time) => time <= latest);
  const entry = barsB.find((time) => time >= spanB.start - 0.05);
  if (swapAt === undefined || entry === undefined) return null;
  const lead = Math.min(bar, entry);
  const aStart = swapAt - lead;
  if (lead < SWAP_IN_S + 2 * SWAP_AHEAD_S || !(aStart > options.earliest) || !(entry + tail < b.duration)) return null;
  const keys = [
    [aStart, 1, 0, 1, 1],
    [swapAt - SWAP_AHEAD_S - SWAP_IN_S, 1, 0, 0, 1],
    [swapAt - SWAP_AHEAD_S, 1, 1, 0, 1],
    [swapAt + tail, 0, 1, 0, 1],
  ];
  return { kind: "swap", bars: 0, plan: { bStart: entry - lead, keys } };
}

// The mix from song `a` into song `b`, or why there is none. A song: { duration, analysis, structure, attributes },
// where all but the duration may be missing. options: { style, maxSeconds, albums, earliest (A's earliest start) }.
// Dolby Atmos goes out as spatial objects, which no mix can take. Apple's player says what a song plays in only once
// it has started (`atmos`); before that the catalog says whether it has an Atmos version, and `atmosPlays` whether
// such songs were seen to play in it here (unknown counts as yes: a mix into Atmos comes in late and at full volume).
function inAtmos(song, atmosPlays) {
  if (typeof song.atmos === "boolean") return song.atmos;
  return atmosPlays !== false && (song.attributes?.audioTraits ?? []).includes("atmos");
}

function planMix(a, b, options) {
  if (options.style === "Off") return { kind: "none", why: "off" };
  if (inAtmos(a, options.atmosPlays)) return { kind: "none", why: "this song plays in Dolby Atmos" };
  if (inAtmos(b, options.atmosPlays)) return { kind: "none", why: "it plays in Dolby Atmos" };
  if (options.albums && sameAlbumInOrder(a, b)) return { kind: "none", why: "the next song of the same album" };
  if (noMixGenre(a) || noMixGenre(b)) return { kind: "none", why: "classical music or speech" };
  const spanA = musicalSpan(a);
  const spanB = musicalSpan(b);
  const onTheBeat = options.style === "Crossfade only"
    ? null : beatMix(a, b, spanA, spanB, options) ?? barSwap(a, b, spanA, spanB, options);
  if (onTheBeat) return onTheBeat;
  const fade = crossfade(a, b, spanA, spanB, options);
  if (!fade) return { kind: "none", why: "no room for a mix" };
  // Why not on the beat, for the log: the first thing in the way.
  const ownFade = spanA.silent - spanA.end >= OWN_FADE_S ? "this song fades out by itself" : "";
  const why = options.style === "Crossfade only"
    ? "" : beatless(a, b, barsOf(a), barsOf(b)) || ownFade || "no room for a swap on a bar";
  return { ...fade, why };
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

const songs = new Map();  // catalog id -> Promise<{ attributes, analysis, structure } | null>

// The song in the first store that has it, or null when none does. Any other answer (no network, a refused token,
// a busy server) throws, so that the song is asked again later instead of staying without its analysis.
async function fetchSong(id, places) {
  for (const place of places) {
    const url = `https://amp-api.music.apple.com/v1/catalog/${place}/songs/${id}` +
      "?include=audio-analysis,flexml-analysis&extend[audio-analysis]=fades,loudnessCurve";
    const headers = { authorization: `Bearer ${await token()}`, origin: "https://music.apple.com" };
    const reply = await Ampify.http.get(url, { headers });
    if (reply.status === 404) continue;
    if (reply.status !== 200) {
      if (reply.status === 401 || reply.status === 403) Ampify.storage.remove("token");
      throw new Error(`Apple's catalog answered ${reply.status}`);
    }
    const item = JSON.parse(reply.text).data?.[0];
    if (!item) continue;
    const part = (name) => item.relationships?.[name]?.data?.[0]?.attributes ?? null;
    return { attributes: item.attributes ?? {}, analysis: part("audio-analysis"), structure: part("flexml-analysis") };
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

function speedNote(rate) {
  if (!rate || rate === 1) return "";
  return `, this song ${Math.abs((rate - 1) * 100).toFixed(1)} % ${rate > 1 ? "faster" : "slower"}`;
}

// Whether the songs that have a Dolby Atmos version play in it on this PC: learnt from each one Apple's player plays,
// and kept, since the answer only changes with a setting of Apple Music or of Windows.
const atmosVersions = new Map();  // song name -> its catalog entry has a Dolby Atmos version
let atmosPlays;                   // true, false, or null while no such song was seen; undefined until read

function knownAtmosPlays() {
  if (atmosPlays === undefined) atmosPlays = Ampify.storage.get("atmosPlays", null);
  return atmosPlays;
}

// True when the song taught something new.
function learnAtmos(song) {
  if (typeof song.atmos !== "boolean" || !atmosVersions.get(song.name) || song.atmos === knownAtmosPlays()) return false;
  atmosPlays = song.atmos;
  Ampify.storage.set("atmosPlays", atmosPlays);
  return true;
}

function options(state) {
  return {
    style: Ampify.config.get("style"),
    maxSeconds: Ampify.config.get("longest"),
    albums: Ampify.config.get("albums"),
    earliest: state.current.time + LATEST_S + 1,
    atmosPlays: knownAtmosPlays(),
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
  const song = (played, info) => ({ duration: played.duration, analysis: info?.analysis, structure: info?.structure,
                                    attributes: info?.attributes, atmos: played.atmos });
  const a = song(now.current, infoA);
  const b = song(now.next, infoB);
  if (atmosVersions.size > 8) atmosVersions.clear();
  for (const [played, info] of [[now.current, infoA], [now.next, infoB]]) {
    atmosVersions.set(played.name, (info?.attributes?.audioTraits ?? []).includes("atmos"));
  }
  learnAtmos(now.current);
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
  if (learnAtmos(state.current)) decidedFor = "";  // what was decided for the next song may no longer hold
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
  // The kernel takes the song over where its speed starts to move, before the mix itself.
  const lead = (decision.plan.rateFrom ?? decision.plan.keys[0][0]) - state.current.time;
  if (lead > PLAN_AHEAD_S || lead < LATEST_S) return;
  // A pause, a seek or a skip cancels a plan, and the kernel may refuse one for a passing reason: sent again once the
  // song is back before its mix, a few times at most.
  if ((decision.sent && state.mix.phase !== "failed") || decision.tries >= MAX_TRIES) return;
  decision.tries++;
  const result = Ampify.automix.schedule(decision.plan);
  decision.sent = result.ok;
  const kinds = { beat: `${decision.bars} bars on the beat${speedNote(decision.plan.rate)}`, swap: "a swap on a bar" };
  const what = kinds[decision.kind] ?? (decision.why ? `a crossfade (${decision.why})` : "a crossfade");
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
