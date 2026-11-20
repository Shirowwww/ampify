// AutoMix: each song flows into the next, as in a DJ set. Apple's own analysis of both songs (beats, bars, phrases,
// key, loudness, fades, vocals: the analysis Apple's AutoMix uses) picks where the next song comes in, at which bar of
// it, and how long the two play together. The kernel (Ampify.automix) brings the current song to the next one's tempo
// at its own pitch, keeps the two in step with a second player, crosses their volumes, hands the bass from one to the
// other and filters both. Songs of one album that follow each other and classical music keep Apple's gapless playback.
// A song that stops on a hit is heard to its end, then the next one comes in at once or after a breath. Of songs with
// a beat that cannot share a tempo, the current one leaves on an echo at the end of a phrase; the others get a
// crossfade, over the song's own fade when it has one. A song that stops on a hit and shares the next one's tempo may
// loop one of its last bars while the next one comes in on the beat.

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
const MAX_BEAT_GAP_S = 0.025;     // two beats further apart than this are heard as two hits: the mix is not on the beat
const RAMP_S = 8;                 // the current song reaches its new speed over this long, before the mix
const MIN_RAMP_S = 4;
const END_ROOM_S = 0.4;           // the kernel wants 0.3 s of the current song after a mix...
const STRETCH_ROOM_S = 0.3;       // ...and this much more after a stretched one
const BASS_SWAP = [0.45, 0.55];   // where in a mix the bass goes from the current song to the next
const PHRASE_SCORE = 600;         // Apple's structure scores every beat: a phrase starts where it gives this or more
const NO_MELODY = 0.25;           // two songs under this melodicness have no keys to clash
const CROSSFADE_S = 6;
const MIN_CROSSFADE_S = 3;
const MAX_CROSSFADE_S = 12;
const OWN_FADE_S = 4;             // a song quiet for this long at its end fades out by itself; less is a last note dying
const SWAP_IN_S = 0.06;          // in a swap on a bar, the next song's intro needs at least this long to come in...
const SWAP_AHEAD_S = 0.02;        // ...and it is in full this long before the bar, so its first beat is heard whole
const SWAP_INTRO_GAIN = 0.6;      // the next song's intro, under the current song's last bar
const SWAP_OUT_GAIN = 0.7;        // the current song as the next one's first bar lands, then it fades out
const SWAP_TAIL_BARS = 0.75;      // the current song's fade after the bar: three quarters of a bar...
const MIN_SWAP_TAIL_S = 1;        // ...within these bounds (half a bar, 0.75 s to silence, was heard as a cut)
const MAX_SWAP_TAIL_S = 2;
const OVER_FADE_GAIN = 0.7;       // the next song's volume once the current one starts fading out by itself
const QUIET_BELOW_DB = 18;        // this far below the song's loudness: silence or a fade, not the music
const BEAT_SWEEP = 0.35;          // in a mix on the beat the next song comes in muffled (5 kHz) and the current one
                                  // leaves thinned (100 Hz)...
const FADE_SWEEP = 0.4;           // ...a little more in a crossfade (4 kHz, 125 Hz)...
const ECHO_SWEEP = 0.6;           // ...and the current song thins out to 300 Hz over the bar it leaves on, in an echo
const ECHO_FEEDBACK = 0.5;        // each repeat of an echo half the one before
const ECHO_DELAY = [0.3, 0.8];    // seconds between repeats: a beat of the current song, doubled or halved into this
// A song that goes from its music to silence within one step of Apple's loudness curve stops on a hit, which rings on
// a little past where its loudness drops. Most songs end on a fade of one to four seconds instead (seen on Apple's
// French chart, 18/11/2026): a pause after each would sound like no AutoMix at all.
const ABRUPT_S = 0.6;
const HIT_RING_S = 0.3;
const COLD_BREATH_S = 0.12;       // after a hit, a next song that starts strong comes in after this...
const NATURAL_BREATH_S = 0.6;     // ...after this otherwise
const CUT_MARGIN_S = 0.1;         // a song left to its end still leaves Apple's player this much of it
const MIN_TRIM_S = 0.3;           // less silence than this to take away: Apple's gapless playback does as well
const LOOP_ROOM_S = 1.1;          // the kernel wants the bar looped to end a second before the song does
const MAX_LOOP_BARS = 4;          // a bar played more often than this gets old
const LOOP_CANDIDATES = 4;        // the bar looped is one of the song's last four that can be
const LOOP_VOCALS = 0.2;          // and is sung over less than this, or the singer stutters
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

// Apple's times in seconds, each one later than the one before: a time given twice or going back made a bar of no
// length, whose echo delay was doubled for ever.
function seconds(list) {
  const times = [];
  for (const value of Array.isArray(list) ? list : []) {
    if (Number.isFinite(value) && !(value / 1000 <= times.at(-1))) times.push(value / 1000);
  }
  return times;
}

function barsOf(song) {
  return seconds(song.analysis?.beats?.barsInMilliseconds);
}

function beatsOf(song) {
  const beats = seconds(song.analysis?.beats?.beatsInMilliseconds);
  return beats.length ? beats : barsOf(song);
}

function nearestIn(sorted, x) {
  let lo = 0;
  let hi = sorted.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (sorted[mid] < x) lo = mid;
    else hi = mid;
  }
  return Math.abs(sorted[lo] - x) <= Math.abs(sorted[hi] - x) ? sorted[lo] : sorted[hi];
}

// The widest gap between the two songs' beats over a mix, in seconds as heard: A plays from aStart at `rate`, B from
// bStart at its own speed, for `length` seconds. The sparser pulse is laid on the denser one (70 bpm on 140).
function worstBeatGap(beatsA, beatsB, aStart, rate, bStart, length) {
  const heardA = beatsA.map((t) => (t - aStart) / rate);
  const heardB = beatsB.map((t) => t - bStart);
  const within = (list) => list.filter((u) => u >= 0 && u <= length);
  const [sparse, dense] = within(heardA).length <= within(heardB).length ? [heardA, heardB] : [heardB, heardA];
  const laid = within(sparse);
  if (laid.length < 2 || dense.length < 2) return Infinity;
  return Math.max(...laid.map((u) => Math.abs(nearestIn(dense, u) - u)));
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
// its bass, takes it over around the middle, and the current song leaves once the next one is in full. With `sweep`,
// two more numbers, how far each song is filtered: the next one comes in muffled and opens up as it takes the bass,
// the current one thins out from there on.
function blendKeys(aStart, aEnd, sweep = 0) {
  const at = (x) => aStart + (aEnd - aStart) * x;
  const keys = [
    [aStart, 1, 0, 1, 0, 0, sweep],
    [at(BASS_SWAP[0]), 1, 0.7, 1, 0, 0, sweep / 3],
    [at(BASS_SWAP[1]), 1, 0.9, 0, 1, 0, 0],
    [at(0.8), 0.5, 1, 0, 1, sweep / 2, 0],
    [aEnd, 0, 1, 0, 1, sweep, 0],
  ];
  return sweep ? keys : keys.map((key) => key.slice(0, 5));
}

// A's bars and beats with its bar `i` played over and over after its first pass, for `turns` more bars.
function loopedGrid(bars, beats, i, turns) {
  const from = bars[i];
  const length = bars[i + 1] - from;
  const loopBars = bars.slice(0, i + 1);
  for (let k = 1; k <= turns; k++) loopBars.push(from + k * length);
  const inLoop = beats.filter((t) => t >= from && t < from + length);
  const loopBeats = beats.filter((t) => t < from);
  for (let k = 0; k <= turns; k++) loopBeats.push(...inLoop.map((t) => t + k * length));
  return { bars: loopBars, beats: loopBeats, from, length };
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

// Seconds of the current song a mix must leave after it: the kernel's second player, ahead of what is heard, is paused
// that long before the song's end, and the stretch of a song played at another speed reads a little further.
function endRoom(rate = 1) {
  return END_ROOM_S + (rate === 1 ? 0 : STRETCH_ROOM_S);
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
  const rate = Math.abs(match.rate - 1) > NO_STRETCH ? match.rate : 1;
  const latest = Math.min(spanA.end, a.duration - endRoom(rate));
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
  const grids = { barsA, barsB, beatsA: beatsOf(a), beatsB: beatsOf(b), span: match.span };
  const sweep = options.can.has("filter") ? BEAT_SWEEP : 0;
  const plain = (() => {
    for (const count of counts) {
      const from = last - count;
      if (from < 0) continue;
      for (const entry of entries) {
        const fit = beatFit(a, grids, from, last, entry, options);
        if (!fit) continue;
        // Two singers at once for long sounds like a clash: then only the shortest mix.
        const sings = vocalShare(a, fit.aStart, fit.aEnd) > 0.4;
        const clash = sings && vocalShare(b, fit.bStart, fit.bStart + fit.length) > 0.4;
        if (clash && count > 2 && counts.includes(2)) continue;
        const plan = {
          bStart: fit.bStart, keys: blendKeys(fit.aStart, fit.aEnd, sweep),
          ...(fit.rate !== 1 ? { rate: fit.rate, rateFrom: fit.aStart - fit.ramp } : {}),
        };
        return { kind: "beat", bars: count, plan, sings };
      }
    }
    return null;
  })();
  // A song that stops on a hit and would be mixed over its singer, shorter than the style wants or not at all, loops
  // a bar without its singer instead, when that gives a longer mix or one without the singer.
  if (options.can.has("loop") && spanA.silent - spanA.end < ABRUPT_S && (!plain || plain.sings || plain.bars < counts[0])) {
    const looped = loopMix(a, grids, entries, counts, options, sweep);
    if (looped && (!plain || plain.sings || looped.bars > plain.bars)) return looped;
  }
  return plain;
}

// Apple's measure looping, for a current song that stops on a hit: one of its last bars that the kernel can loop, one
// without its singer, plays over and over while the next song comes in on the beat. The next song's first bars ride
// on that groove rather than on the current song's last phrase, and the hit that would stop it dead never comes. A's
// time runs on through the loop: the keys and the speed are those of the bar looped. Null when no such bar fits.
function loopMix(a, grids, entries, counts, options, sweep) {
  const { barsA, beatsA } = grids;
  const candidates = [];
  for (let k = barsA.length - 2; k >= 1 && candidates.length < LOOP_CANDIDATES; k--) {
    if (barsA[k + 1] + LOOP_ROOM_S <= a.duration) candidates.push(k);
  }
  const i = candidates.find((k) => vocalShare(a, barsA[k], barsA[k + 1]) < LOOP_VOCALS);
  if (i === undefined) return null;
  for (const count of counts.filter((bars) => bars <= MAX_LOOP_BARS)) {
    const looped = loopedGrid(barsA, beatsA, i, count + 1);
    for (const entry of entries) {
      const fit = beatFit(a, { ...grids, barsA: looped.bars, beatsA: looped.beats }, i, i + count, entry,
                          { ...options, looped: true });
      // The kernel lets go of the second player once it holds the bar: the room it wants is after the first key.
      if (!fit || a.duration - fit.aStart < endRoom(fit.rate)) continue;
      const plan = {
        bStart: fit.bStart, keys: blendKeys(fit.aStart, fit.aEnd, sweep), loop: { from: looped.from, length: looped.length },
        ...(fit.rate !== 1 ? { rate: fit.rate, rateFrom: fit.aStart - fit.ramp } : {}),
      };
      return { kind: "loop", bars: count, plan };
    }
  }
  return null;
}

// The current song's bars `from` to `last` laid on the next song's from bar `entry`: the speed is the one at which
// those very bars of A last as long as B's (Apple's tempo moves along a song, so the tempo near either end misled
// the mix), lined up on the middle bar, and kept only when every beat of the mix falls with one of the other song's.
function beatFit(a, grids, from, last, entry, options) {
  const { barsA, barsB, beatsA, beatsB, span } = grids;
  const count = last - from;
  const middle = count / 2;
  const across = middle * span;
  if (!Number.isInteger(across) || entry + count * span >= barsB.length) return null;
  const aStart = barsA[from];
  const aEnd = barsA[last];
  const fitted = (aEnd - aStart) / (barsB[entry + count * span] - barsB[entry]);
  if (!(Math.abs(fitted - 1) <= MAX_STRETCH)) return null;
  const rate = Math.abs(fitted - 1) > NO_STRETCH ? fitted : 1;
  if (Math.abs(rate - 1) > SHORT_ABOVE && count > SHORT_BARS) return null;  // every style tries four bars or fewer
  const length = (aEnd - aStart) / rate;
  const ramp = rate !== 1 ? Math.min(RAMP_S, aStart - options.earliest) : 0;
  if (length > options.maxSeconds || aStart < options.earliest || (rate !== 1 && ramp < MIN_RAMP_S)) return null;
  if (!options.looped && aEnd > a.duration - endRoom(rate)) return null;
  const bStart = barsB[entry + across] - (barsA[from + middle] - aStart) / rate;
  if (bStart < 0) return null;
  if (worstBeatGap(beatsA, beatsB, aStart, rate, bStart, length) > MAX_BEAT_GAP_S) return null;
  return { aStart, aEnd, rate, ramp, length, bStart };
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
  // Filtered, the next song comes in muffled and opens up, fully once the current one fades by itself.
  const sweep = options.can.has("filter") ? FADE_SWEEP : 0;
  const key = (at, x, y, thin, muffled) => (sweep ? [at, x, y, 1, 1, thin * sweep, muffled * sweep] : [at, x, y]);
  const keys = fade > 0.5 && spanA.end - aStart >= 1
    ? [key(aStart, 1, 0, 0, 1), key(spanA.end, 1, OVER_FADE_GAIN, 0.5, 0), key(aEnd, 0, 1, 1, 0)]
    : [key(aStart, 1, 0, 0, 1), key(aEnd, 0, 1, 1, 0)];
  return { kind: "crossfade", bars: 0, plan: { bStart: spanB.sound, keys } };
}

// Seconds between an echo's repeats: a beat of the current song, doubled or halved until it is neither a stutter
// nor a lag.
function echoDelay(beat) {
  let delay = beat;
  while (delay < ECHO_DELAY[0]) delay *= 2;
  while (delay > ECHO_DELAY[1]) delay /= 2;
  return delay;
}

// An echo out, for songs with a beat that cannot share a tempo, as a DJ leaves a song at the end of a phrase: the
// current song plays whole up to there, thinning out over its last bar; its last beat goes into an echo, and on the
// bar the next song's first bar lands in full as the current one stops, its echo ringing on over the next song's
// first bars, each repeat half the one before and thinner. A next song whose first bar comes too soon after its start
// to be started unheard a beat before it takes the current song's first beat of the next bar instead, the current
// song fading out over half of it. Null when the current song fades out by itself (a crossfade follows its fade).
function echoSwap(a, b, spanA, spanB, options) {
  const barsA = barsOf(a);
  const barsB = barsOf(b);
  if (beatless(a, b, barsA, barsB) || spanA.silent - spanA.end >= OWN_FADE_S) return null;
  const bar = barLength(barsA, spanA.end);
  // The current song may go on half a beat past the bar.
  let last = barsA.findLastIndex((time) => time <= Math.min(spanA.end, a.duration - endRoom() - ECHO_DELAY[1] / 2));
  const phraseEnd = phraseBars(a, barsA).filter((index) => index <= last).at(-1);
  if (phraseEnd !== undefined && last - phraseEnd < 4) last = phraseEnd;
  const first = barsB.findIndex((time) => time >= spanB.start - 0.05);
  if (last < 0 || first < 0) return null;
  const swapAt = barsA[last];
  const entry = barsB[first];
  const lead = Math.min(bar, entry);  // the next song plays unheard before its first bar, from its start at most
  const aStart = swapAt - lead;
  if (!(aStart > options.earliest) || !(entry + bar < b.duration)) return null;
  const sweep = options.can.has("filter") ? ECHO_SWEEP : 0;
  const delay = echoDelay(bar / 4);
  const plan = (keys, echo) => ({ bStart: entry - lead, keys, echo: { ...echo, delay, feedback: ECHO_FEEDBACK } });
  if (lead >= delay + 2 * SWAP_AHEAD_S) {
    // The echo takes the beat before the bar, so that its first repeat lands on the bar as the song stops; the next
    // song is in full a little before the bar, so its first beat is heard whole.
    const cut = swapAt - SWAP_AHEAD_S;
    const keys = [[aStart, 1, 0, 1, 0, 0, 0], [cut - SWAP_AHEAD_S, 1, 0, 1, 0, sweep, 0], [cut, 0, 1, 0, 1, sweep, 0]];
    return { kind: "echo", bars: 0, plan: plan(keys, { from: swapAt - delay, to: cut }) };
  }
  const full = Math.max(aStart + SWAP_AHEAD_S, swapAt - SWAP_AHEAD_S);
  const gone = swapAt + delay / 2;
  const keys = [[aStart, 1, 0, 1, 0, 0, 0], [full, 1, 1, 0, 1, sweep / 2, 0], [gone, 0, 1, 0, 1, sweep, 0]];
  return { kind: "echo", bars: 0, plan: plan(keys, { from: aStart, to: gone }) };
}

// The current song to its very end, then the next one from its first sound after a breath, Apple's player alone
// moving from one to the other with the silences between them cut short: for a song that stops on a hit, its own
// end. A cold cut into a next song that starts strong, a breath before one that starts softly. Null for a song that
// fades, or when there is too little silence to take away for this to differ from Apple's gapless playback.
function naturalEnd(a, b, spanA, spanB, options) {
  if (!options.can.has("cut") || !(spanA.silent - spanA.end < ABRUPT_S)) return null;
  const breath = spanB.start - spanB.sound < ABRUPT_S ? COLD_BREATH_S : NATURAL_BREATH_S;
  const ended = Math.min(spanA.silent + HIT_RING_S, a.duration - CUT_MARGIN_S);
  const ownSilence = Math.min(breath, spanB.sound);  // what of the breath is the next song's own silence
  const at = Math.min(ended + breath - ownSilence, a.duration - CUT_MARGIN_S);
  const bStart = spanB.sound - ownSilence;
  if (!(at > options.earliest) || a.duration - at + bStart < MIN_TRIM_S) return null;
  return { kind: "natural", bars: 0, plan: { bStart, keys: [[at, 1, 0], [at, 0, 1]] }, cold: breath === COLD_BREATH_S };
}

// A swap on a bar, for songs with a beat that cannot share a tempo. The current song plays whole, bass included, up
// to the end of a phrase; under its last bar the next song's intro rises without its bass; on that bar's end the
// basses change hands and the next song's first bar lands in full, while the current song fades out, bassless, over
// three quarters of a bar, so that nothing stops dead. Null when the current song fades out by itself (a crossfade follows that
// fade) or the next song has nothing before its first bar to come in from.
function barSwap(a, b, spanA, spanB, options) {
  const barsA = barsOf(a);
  const barsB = barsOf(b);
  if (beatless(a, b, barsA, barsB) || spanA.silent - spanA.end >= OWN_FADE_S) return null;
  const bar = barLength(barsA, spanA.end);
  const tail = Math.min(Math.max(bar * SWAP_TAIL_BARS, MIN_SWAP_TAIL_S), MAX_SWAP_TAIL_S);
  const latest = Math.min(spanA.end, a.duration - endRoom()) - tail;
  let last = barsA.findLastIndex((time) => time <= latest);
  const phraseEnd = phraseBars(a, barsA).filter((index) => index <= last).at(-1);
  if (phraseEnd !== undefined && last - phraseEnd < 4) last = phraseEnd;
  const first = barsB.findIndex((time) => time >= spanB.start - 0.05);
  if (last < 0 || first < 0) return null;
  const swapAt = barsA[last];
  const entry = barsB[first];
  const lead = Math.min(bar, entry);
  const aStart = swapAt - lead;
  if (lead < SWAP_IN_S + 2 * SWAP_AHEAD_S || !(aStart > options.earliest) || !(entry + tail < b.duration)) return null;
  const handOver = Math.min(bar / 8, lead / 2);  // the basses change hands over the last half beat
  const intro = lead - handOver > SWAP_IN_S ? [[swapAt - handOver, 1, SWAP_INTRO_GAIN, 1, 0]] : [];
  const keys = [
    [aStart, 1, 0, 1, 0],
    ...intro,
    [swapAt - SWAP_AHEAD_S, SWAP_OUT_GAIN, 1, 0, 1],
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

// `options.can` holds what the kernel can play beyond volumes, lows and speed (Ampify.automix.state().features): an
// older one gets the plans it always took.
function planMix(a, b, given) {
  const options = { ...given, can: given.can ?? new Set() };
  if (options.style === "Off") return { kind: "none", why: "off" };
  if (inAtmos(a, options.atmosPlays)) return { kind: "none", why: "this song plays in Dolby Atmos" };
  if (inAtmos(b, options.atmosPlays)) return { kind: "none", why: "it plays in Dolby Atmos" };
  if (options.albums && sameAlbumInOrder(a, b)) return { kind: "none", why: "the next song of the same album" };
  if (noMixGenre(a) || noMixGenre(b)) return { kind: "none", why: "classical music or speech" };
  const spanA = musicalSpan(a);
  const spanB = musicalSpan(b);
  if (options.style !== "Crossfade only") {
    const beat = beatMix(a, b, spanA, spanB, options);
    if (beat) return beat;
    // A hit is the song's own end. Otherwise two beats that cannot share a tempo leave on an echo.
    const ending = naturalEnd(a, b, spanA, spanB, options);
    if (ending) return ending;
    const swap = options.can.has("echo") ? echoSwap(a, b, spanA, spanB, options) : barSwap(a, b, spanA, spanB, options);
    if (swap) return swap;
  }
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
let stereoAsked = false;          // Apple's player is asked for the songs' stereo versions

// Songs with a Dolby Atmos version are asked for in stereo, so that they mix too (the "stereo" setting). Asked at
// every look: the kernel acts only on a change, and on a song playing in Dolby Atmos only once a stereo one plays or
// Apple Music starts playing again (a song is never switched under way). An Ampify without it leaves Atmos as it is.
function askStereo() {
  if (typeof Ampify.automix.preferStereo !== "function") return;
  const want = Ampify.config.get("style") !== "Off" && Ampify.config.get("stereo") === true;
  Ampify.automix.preferStereo(want);
  if (want === stereoAsked) return;
  stereoAsked = want;
  Ampify.log(want ? "Songs with a Dolby Atmos version are asked for in stereo, to mix them"
                  : "Dolby Atmos left to Apple Music's choice");
}

function knownAtmosPlays() {
  if (atmosPlays === undefined) atmosPlays = Ampify.storage.get("atmosPlays", null);
  return atmosPlays;
}

// True when the song taught something new. Nothing is learnt while the stereo versions are asked for: what they play
// in then says nothing of what they play in without it.
function learnAtmos(song) {
  if (stereoAsked) return false;
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
    can: new Set(Array.isArray(state.features) ? state.features : []),
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
  askStereo();
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
  const kinds = {
    beat: `${decision.bars} bars on the beat${speedNote(decision.plan.rate)}`,
    loop: `${decision.bars} bars on the beat over this song's last bar, looped${speedNote(decision.plan.rate)}`,
    swap: "a swap on a bar",
    echo: "an echo out on a bar",
    natural: decision.cold ? "this song to its last hit, then a cold cut" : "this song to its last hit, then a breath",
  };
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
