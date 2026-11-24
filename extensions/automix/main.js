// AutoMix: each song flows into the next, as in a DJ set. Apple's own analysis of both songs (beats, bars, phrases,
// key, loudness, fades, vocals: the analysis Apple's AutoMix uses) picks where the next song comes in, at which bar of
// it, and how long the two play together. The kernel (Ampify.automix) brings the current song to the next one's tempo
// at its own pitch, keeps the two in step with a second player, sets both volumes, hands the bass from one to the
// other and filters both. Songs of one album that follow each other and classical music keep Apple's gapless playback.
// Songs that share a tempo mix as a DJ does, on the bar where the current song's phrase ends. With a kernel that has
// the crossover, both play at full volume while a falling cutoff hands the current song's sound to the next one's
// from the top down, its bass last, as Apple's AutoMix does; otherwise the next song's intro rises under the current
// song's last bars without its bass and drops in full, bass and all, on that bar, the current one gone within a bar.
// A song that stops on a hit is heard to its end, then the next one comes in at once, after a breath, or with the
// styles in a short crossfade under its last ring; a soft ending (a ballad, an acoustic or ambient end) is left to
// breathe the same way. Of songs that cannot share a tempo, the current one leaves on an echo at the end of a phrase,
// the next landing on the bar; a crossfade is left for songs without bars and for songs that fade out by themselves.
// A song that stops on a hit and shares the next one's tempo may loop one of its last bars while the next one comes
// in on the beat.

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
const MS_SLACK_S = 0.002;         // Apple's times are rounded to the millisecond
const RAMP_S = 8;                 // the current song reaches its new speed over this long, before the mix
const MIN_RAMP_S = 4;
const END_ROOM_S = 0.4;           // the kernel wants 0.3 s of the current song after a mix...
const STRETCH_ROOM_S = 0.3;       // ...and this much more after a stretched one
const DROP_DB = 4;                // the next song is in full where Apple's loudness curve comes this close to its
const DROP_HOLD_S = 3;            // loudness and stays there this long: the bar it drops on in a mix on the beat
const BUILD_BELOW_DB = 6;         // under the current song's last bars, the next song's music this far below its level
const MIN_BUILD_GAIN = 0.5;       // (a quiet intro is left as it is)...
const BUILD_PUSH = 1.25;          // ...a little louder by the drop,
const BUILD_MUFFLE = 0.5;         // muffled (2.8 kHz) as it comes in and open by the drop
const DROP_THIN = 0.6;            // the current song thins out to 300 Hz as it leaves after the drop
// The crossover, with a kernel that has it ("crossover" in its features): both songs at full volume, the next one
// through a high-pass and the current one through a low-pass at one cutoff, which comes down from the top to the
// bass evenly in Hz along the square of the time, as Apple's "filter high to low" does: the next song's highs come
// in first, its mids and its bass only near the end. The current song, muffled down to its bass by then, is cut
// within CROSS_CUT_S as the next one's bass lands on the bar.
const CROSS_TOP_HZ = 16000;
const CROSS_BASS_HZ = 150;
const CROSS_CUT_S = 0.3;
const CROSS_IN_BEATS = 2;         // the next song's rise to full volume, its very top only, at the crossover's start
const THIN_HZ = [2, 16000];      // the kernel's ranges (mix_deck.cpp): the next song's high-pass at 0 and at 1...
const MUFFLE_HZ = [20000, 30];    // ...and the current song's low-pass
const MAX_SWEEP_KEYS = 48;        // of the kernel's 64
const CROSS_HAND_S = 1.5;         // the crossover's last seconds, where the voices change hands...
const MAX_HAND_SINGING_S = 1;     // ...two singers at once longer than this there are heard as a clash
const HAND_STEP_S = 0.05;
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
const TAIL_STEP_S = 0.25;
const OVER_FADE_GAIN = 0.7;       // the next song's volume once the current one starts fading out by itself
const FADED_DB = 8;               // a song fading out by itself is as good as gone this far below its loudness (12 left
                                  // a second of near silence, 24/11)...
const QUIET_INTRO_S = 2;          // ...where a next song in full this long after its first sound brings its drop in
const QUIET_BELOW_DB = 18;        // this far below the song's loudness: silence or a fade, not the music
const FADE_SWEEP = 0.4;           // in a crossfade the next song comes in muffled (4 kHz), the current one leaves thin
                                  // (125 Hz)...
const ECHO_SWEEP = 0.6;           // ...and the current song thins out to 300 Hz over the bar it leaves on, in an echo
const ECHO_FEEDBACK = 0.5;        // each repeat of an echo half the one before
const ECHO_LEVEL = 1;             // with the styles, its first repeat against what was thrown (24/11: the kernel's own
                                  // 0.7 hid under the drop, 1.3 at 60 % a repeat was too loud and rang too long)
const ECHO_PEEK = [0.3, 0.5, 0.65]; // the next song creeping in, thin: its volume at the last bar, the half and the end
const ECHO_CREEP_S = 4.5;         // over two bars at most this long, else over the last one
const ECHO_MUFFLE = 0.25;         // the current song darkens to about 7 kHz over its last half bar, its echo still bright
const ECHO_RISE_MUFFLE = 0.5;     // without the crossover's filters, the next song creeps in muffled (about 2.8 kHz)
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
// A next song much louder at its start than the soft end it follows came in as a jump (24/11, 19 dB): it comes in
// muffled and low, and rises to full.
const NATURAL_JUMP_DB = 6;
const NATURAL_IN_GAIN = 0.3;
const NATURAL_IN_MUFFLE = 0.5;    // its low-pass at about 3 kHz, opening as it rises...
const NATURAL_IN_S = 1.5;         // ...over this long
const LEVEL_SPAN_S = 2;           // the seconds either side of the jump that are measured
// With the styles every natural end joins the next song in a short crossfade ("could have had a little crossfade",
// 24/11): the next song from its first sound, under the current song's last ring, muffled a little, in full after...
const JOIN_OVERLAP_S = 0.5;       // ...starting this long before that ring ends...
const JOIN_S = 1;                 // ...over this long (NATURAL_IN_S and NATURAL_IN_MUFFLE for a much louder next song)
const JOIN_MUFFLE = 0.3;          // its low-pass at about 6 kHz at first
const LOOP_ROOM_S = 1.1;          // the kernel wants the bar looped to end a second before the song does
const MAX_LOOP_BARS = 4;          // a bar played more often than this gets old
const LOOP_CANDIDATES = 4;        // the bar looped is one of the song's last four that can be
const LOOP_VOCALS = 0.2;          // and is sung over less than this, or the singer stutters
const MIN_LOOP_S = 0.2;           // the kernel's shortest loop (kMinLoopSeconds): a roll starts on a beat this long
const ROLL_BACK_BARS = 8;         // past a sung or fading last bar, a roll takes one up to this many bars earlier...
// ...skipping no more singing than this share of what it skips. Rolls that cut a verse's last lines off (14 and 26 s
// before the song's end) were heard as too early on 24/11; one that skipped an instrumental end was better than the
// crossfade. A quieter outro may go whole.
const SKIP_VOCALS = 0.25;
// A soft end of a song: Apple finds hardly any beat there, and little energy or mostly acoustic instruments (a ballad,
// an acoustic or ambient outro). Apple scores most rap 0.1 to 0.4 for its beat: low alone, it is not soft.
const SOFT_DANCEABILITY = 0.2;
const SOFT_ENERGY = 0.3;
const SOFT_ACOUSTIC = 0.7;
// With a kernel that has the gate and the roll (its "gate" and "roll" features), each pair of songs gets a style of
// its own, always the same for that pair, so that a playlist does not mix the same way twice in a row. The styles
// least like a crossfade come up most.
//   stabs: the next song comes in as stabs on its beats, half a beat each, over the current song played whole, which
//     dips a little under each; without its bass and muffled, it opens up until it drops in full on the bar.
//   drop: the same without the stabs, the next song rising to 0.8 under the current one.
//   crossover: both at full volume, a falling cutoff handing one to the other from the top down.
//   roll: the current song's last bar rolled faster and faster (a beat, again, half beats, quarter beats), its bass
//     gone and thinning out, and the next song lands on the bar: no beat of the two heard together, any tempos.
//   echo: the current song leaves on an echo of its last beat (the old way, for songs that cannot share a tempo).
const BEAT_STYLES = [["stabs", 3], ["drop", 1], ["crossover", 1], ["roll", 1]];
// Keys that clash are heard as wrong notes where both songs play: no crossover, the style that holds both longest in
// full, and more rolls, which have no overlap at all.
const CLASH_STYLES = [["stabs", 2], ["drop", 1], ["roll", 2]];
const FREE_STYLES = [["roll", 9], ["echo", 1]]; // the echo dies away like a fade: one pair in ten
const IN_LEVEL = 0.8;             // the next song under the current one, until the drop (Songate's tuning, by ear)
const LP_FROM_HZ = 1000;          // its low-pass at first, opening to 20 kHz by the drop
const LOW_PASS_HZ = [20000, 400]; // the kernel's low-pass of the next song (bFilter), at 0 and at 1
const BRIDGE_SHARE = 0.25;        // the current song's bass goes this share of the build before the drop, on a bar,
const BASS_CUT_S = 0.03;          // within this: a bridge without bass that the drop fills
const EXIT_POWER = 5;             // after the drop the current song goes along a cosine to this power: mostly gone in
const EXIT_BARS = 1;              // half a bar, all in one
const STAB_ON = 0.5;              // a stab lasts half a beat...
const STAB_DUCK = 0.3;            // ...and the current song dips this much under it
const STAB_SINGING = 0.3;         // a next song sung over more of its build than this is not chopped into stabs
const ROLL_SWEEP = 0.45;          // the rolled bar thins out to about 160 Hz (A's high-pass) by the drop...
const ROLL_SWEEP_MID = 0.25;      // ...from the loop's start, 60 Hz or so once the quarter beats come (older kernels)
// With the crossover's filters the roll darkens instead, its bass kept: thinned, it fell 15 dB in its last second
// (24/11, n-1), and the next song, silent until the drop, never plays a second bass line under it.
const ROLL_MUFFLE = 0.45;         // A's low-pass at about 1 kHz by the drop...
const ROLL_MUFFLE_MID = 0.25;     // ...from the loop's start, about 7 kHz once the quarter beats come
const SLOW_BEAT_S = 0.75;         // a beat this long (80 bpm or slower) rolls on half beats, over half a bar
const PEEK_GAIN = 0.5;            // "a bit of the next song before the transition" (24/11): a roll lets it creep in
const PEEK_THIN = 0.6;            // over the last two beats, up to this volume, through a high-pass at about 450 Hz,
                                  // as an echo does (ECHO_PEEK)
const ROLL_ECHO_FEEDBACK = 0.4;   // the roll's last beat rings on under the drop, each repeat 40 % of the one before
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
  // A beat on the mix's very edge counts: Apple's millisecond times may put it a hair outside.
  const within = (list) => list.filter((u) => u >= -MS_SLACK_S && u <= length + MS_SLACK_S);
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

// Apple's loudness curve of a song in dB, a value every `step` seconds (a second's window each), and the song's
// loudness; null without the curve.
function loudnessOf(song) {
  const curve = song.analysis?.loudnessCurve?.value;
  if (!Array.isArray(curve) || curve.length <= 8 || !(song.duration > 0)) return null;
  return { curve, step: song.duration / curve.length, loud: song.analysis?.loudness?.main?.value ?? median(curve) };
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
  const loudness = loudnessOf(song);
  if (loudness) {
    const { curve, step, loud } = loudness;
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

// Where the singer sings with "high" or "very-high" activity, as [from, to] in seconds.
function sungSpans(song) {
  const spans = song.analysis?.vocalActivity;
  if (!Array.isArray(spans)) return [];
  return spans.filter((span) => span.strength === "high" || span.strength === "very-high")
    .map((span) => [span.startInMilliseconds / 1000, span.endInMilliseconds / 1000]);
}

// How much of [from, to] the singer fills, 0 to 1.
function vocalShare(song, from, to) {
  if (!(to > from)) return 0;
  let sung = 0;
  for (const [x, y] of sungSpans(song)) sung += Math.max(0, Math.min(to, y) - Math.max(from, x));
  return sung / (to - from);
}

// Seconds, as heard, both singers sing over the last CROSS_HAND_S before a fit's drop: where a crossover hands the
// mids, and the voices with them, from one song to the other.
function handover(a, b, fit) {
  const [spansA, spansB] = [sungSpans(a), sungSpans(b)];
  const sings = (spans, t) => spans.some(([x, y]) => x <= t && t < y);
  let both = 0;
  for (let u = 0; u < CROSS_HAND_S; u += HAND_STEP_S) {
    if (sings(spansA, fit.aEnd - u * fit.rate) && sings(spansB, fit.bDrop - u)) both += HAND_STEP_S;
  }
  return both;
}

// Whether a song's `part` ("beginning" or "ending") is soft; unknown is not.
function soft(song, part) {
  const value = (name) => {
    const given = song.analysis?.[name]?.[part] ?? song.analysis?.[name]?.main;
    return Number.isFinite(given) ? given : NaN;  // null < 0.2 is true: a value Apple left out would read as soft
  };
  if (!(value("danceability") < SOFT_DANCEABILITY)) return false;
  return value("energy") < SOFT_ENERGY || value("acousticness") > SOFT_ACOUSTIC;
}

// Where the song's music is first in full: Apple's loudness curve within DROP_DB of the song's loudness there and,
// for the most part, for DROP_HOLD_S after. Its start without the curve.
function fullFrom(song) {
  const loudness = loudnessOf(song);
  if (!loudness) return 0;
  const { curve, step, loud } = loudness;
  const floor = loud - DROP_DB;
  const hold = Math.max(1, Math.round(DROP_HOLD_S / step));
  for (let i = 0; i + hold <= curve.length; i++) {
    if (curve[i] > floor && median(curve.slice(i, i + hold)) > floor) return i * step;
  }
  return 0;
}

// How loud a song plays from `from` to `to`, in dB against its loudness; 0 without Apple's curve.
function levelOver(song, from, to) {
  const loudness = loudnessOf(song);
  if (!loudness) return 0;
  const { curve, step, loud } = loudness;
  const first = Math.max(0, Math.floor(from / step));
  return median(curve.slice(first, Math.max(first + 1, Math.ceil(to / step)))) - loud;
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

// The volumes and the lows of a mix on the beat, as [time, A, B, A's lows, B's lows], in A's time, A playing at
// `rate`: within a beat of the mix's start the next song is up at `gain` without its bass, under the current song in
// full; it rises a little until the drop, where over the last half beat the basses change hands and the next song is
// in full, its downbeat whole; the current song, from 0.7 there and without its bass, is gone at `end`. With
// `filtered`, two more numbers, how far each song is filtered: the next one comes in muffled and opens up by the drop,
// the current one thins out as it leaves.
function dropKeys(aStart, dropAt, end, beat, gain, rate, filtered) {
  const ahead = SWAP_AHEAD_S * rate;
  const room = (dropAt - aStart) / 4;
  const rise = Math.min(beat, room);
  const hand = Math.min(beat / 2, room);
  const keys = [
    [aStart, 1, 0, 1, 0, 0, BUILD_MUFFLE],
    [aStart + rise, 1, gain, 1, 0, 0, BUILD_MUFFLE],
    [dropAt - ahead - hand, 1, Math.min(1, gain * BUILD_PUSH), 1, 0, 0, BUILD_MUFFLE / 4],
    [dropAt - ahead, SWAP_OUT_GAIN, 1, 0, 1, 0, 0],
    [end, 0, 1, 0, 1, DROP_THIN, 0],
  ];
  return filtered ? keys : keys.map((key) => key.slice(0, 5));
}

// How far a crossover filter (THIN_HZ or MUFFLE_HZ) is set for a cutoff of `hz`.
function amountFor(hz, [open, shut]) {
  return Math.min(1, Math.max(0, Math.log(hz / open) / Math.log(shut / open)));
}

// The keys of a crossover, in A's time, A playing at `rate`: [time, A, B, A's lows, B's lows, A's high-pass, B's
// low-pass, A's low-pass, B's high-pass]. The next song rises to full volume over CROSS_IN_BEATS (at full within
// half a beat, its highs were heard as the new song coming in too soon, 24/11), only its very top heard; the cutoff
// comes down on every beat or so, and from CROSS_CUT_S before the bar the current song, muffled to its bass, goes as
// the next one's high-pass opens: whole, bass included, on the bar.
function crossoverKeys(aStart, dropAt, beat, rate) {
  const end = dropAt - SWAP_AHEAD_S * rate;
  const swept = end - CROSS_CUT_S * rate;
  const up = Math.min(CROSS_IN_BEATS * beat, (swept - aStart) / 3);
  const cutoff = (t) => CROSS_TOP_HZ - (CROSS_TOP_HZ - CROSS_BASS_HZ) * ((t - aStart) / (swept - aStart)) ** 2;
  const key = (t, hz) => [t, 1, 1, 1, 1, 0, 0, amountFor(hz, MUFFLE_HZ), amountFor(hz, THIN_HZ)];
  const keys = [[aStart, 1, 0, 1, 1, 0, 0, 0, 1], key(aStart + up, cutoff(aStart + up))];
  const step = beat * Math.ceil((swept - aStart) / (beat * MAX_SWEEP_KEYS));
  for (let t = aStart + up + step; t < swept - step / 2; t += step) keys.push(key(t, cutoff(t)));
  keys.push(key(swept, CROSS_BASS_HZ), [end, 0, 1, 1, 1, 0, 0, amountFor(CROSS_BASS_HZ, MUFFLE_HZ), 0]);
  return keys;
}

// How far the kernel's low-pass of the next song (bFilter) is set for a cutoff of `hz`.
function lowPassAmount(hz) {
  const [open, shut] = LOW_PASS_HZ;
  return Math.min(1, Math.max(0, Math.log(open / hz) / Math.log(open / shut)));
}

// The current song's way out after a drop, as heard: a bar, within a second and two and a half.
function exitTail(bar) {
  return Math.min(Math.max(bar * EXIT_BARS, 1), 2.5);
}

// The keys of a drop with a kernel that has the gate, as Songate's trained Automix plays one (V1.2.0, tuned by ear):
// [time, A, B, A's lows, B's lows, A's high-pass, B's low-pass], in A's time. The next song comes in without its bass,
// under a low-pass from LP_FROM_HZ that opens by the drop, rising to IN_LEVEL, or `gated` at full level, the gate
// chopping it into stabs. The current song plays whole until a bar BRIDGE_SHARE of the build before the drop, where
// its bass goes at once (a bridge without bass); on the drop the next song's bass comes in at once and the current
// song goes out over `exit` seconds of its time along a steep cosine.
function buildKeys(aStart, drop, bar, exit, rate, gated) {
  const bars = Math.max(1, Math.round((drop - aStart) / bar));
  const bridge = Math.max(0.5, Math.min(Math.max(1, Math.round(bars * BRIDGE_SHARE)), bars / 2));
  const ahead = SWAP_AHEAD_S * rate;  // whole a hair before the bar, so that the drop's first beat is
  const full = drop - ahead;
  const cutAt = full - bridge * bar;
  const lp = lowPassAmount(LP_FROM_HZ);
  const muffled = (t) => lp * Math.max(0, 1 - (t - aStart) / (full - aStart));
  const level = (t) => {
    if (gated) return 1;
    if (t <= cutAt) return IN_LEVEL * Math.sin(Math.min(1, (t - aStart) / (cutAt - aStart)) * Math.PI / 2);
    return IN_LEVEL + (1 - IN_LEVEL) * (t - cutAt) / (full - cutAt);
  };
  const key = (t, a, aLow, bLow) => [t, a, level(t), aLow, bLow, 0, muffled(t)];
  const keys = [[aStart, 1, 0, 1, 0, 0, lp]];
  keys.push(gated ? key(aStart + 0.002, 1, 1, 0) : key((aStart + cutAt) / 2, 1, 1, 0));
  keys.push(key(cutAt - BASS_CUT_S, 1, 1, 0), key(cutAt, 1, 0, 0), key(full - BASS_CUT_S, 1, 0, 0));
  keys.push([full, 1, 1, 0, 1, 0, 0]);
  for (const share of [0.25, 0.5]) keys.push([full + share * exit, Math.cos(share * Math.PI / 2) ** EXIT_POWER, 1, 0, 1, 0, 0]);
  keys.push([full + exit, 0, 1, 0, 1, 0, 0]);
  return keys;
}

// Whether stabs would chop the next song's singer mid-word: sung over more of its build than STAB_SINGING.
function choppedSinger(b, fit, style) {
  return style === "stabs" && vocalShare(b, fit.bStart, fit.bDrop) > STAB_SINGING;
}

// The next song's beats over its build, laid on the current song's time, each a stab until the drop.
function stabsOf(grids, fit, beat) {
  const until = fit.aEnd - SWAP_AHEAD_S * fit.rate;
  const beats = grids.beatsB.map((t) => fit.aStart + (t - fit.bStart) * fit.rate)
    .filter((t) => t >= fit.aStart - MS_SLACK_S && t < until - beat * STAB_ON)
    .map((t) => Math.max(t, fit.aStart));
  return beats.length ? { beats, on: beat * STAB_ON, duck: STAB_DUCK, until } : null;
}

// A number for the pair of songs, the same at every play: the styles are drawn from it.
function pairHash(text) {
  let hash = 2166136261;
  for (let i = 0; i < text.length; i++) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  // Mixed once more (MurmurHash3's finish), or names that differ in their last letters draw alike.
  hash ^= hash >>> 16;
  hash = Math.imul(hash, 0x85ebca6b);
  hash ^= hash >>> 13;
  hash = Math.imul(hash, 0xc2b2ae35);
  hash ^= hash >>> 16;
  return hash >>> 0;
}

// The styles in the order a pair tries them: drawn by their weights, the same order for the same pair. Only the
// styles the kernel can play; `prefer` (tests) first.
function styleOrder(a, b, weighted, options) {
  const pair = `${a.id ?? a.attributes?.name ?? ""}|${b.id ?? b.attributes?.name ?? ""}`;
  const drawn = weighted.filter(([name]) => canPlay(name, options.can)).map(([name, weight]) => {
    const u = (pairHash(`${pair}|${name}`) + 0.5) / 4294967296;
    return { name, rank: -Math.log(u) / weight };
  });
  drawn.sort((x, y) => x.rank - y.rank);
  const names = drawn.map((style) => style.name);
  if (options.prefer && names.includes(options.prefer)) return [options.prefer, ...names.filter((n) => n !== options.prefer)];
  return names;
}

function canPlay(style, can) {
  if (style === "stabs") return can.has("gate");
  if (style === "roll") return can.has("roll") && can.has("loop");
  if (style === "crossover") return can.has("crossover");
  if (style === "echo") return can.has("echo");
  return true;
}

// The current song's last bar rolled into the next song's drop, as a DJ rolls a loop: its first beat twice, then its
// first half beat twice, then its first quarter beat four times, darkening from the start, bass and all (darkRoll),
// and the next song lands in full on the bar, where the current one stops, the roll's last beat ringing on over the
// drop as an echo. A slow song rolls the second half of its bar
// on half beats (rollUnit). No beat of the two is heard together, so any tempos roll. The bar rolled is the one that
// ends the current song's phrase (or its last before its own end, or before its fade when it fades out by itself), or
// one a little earlier (rollBar); the next song drops where its music is in full when that is within its first
// quarter, else on its first bar a bar in.
function rollMix(a, b, spanA, spanB, options) {
  const barsA = barsOf(a);
  const barsB = barsOf(b);
  if (beatless(barsA, barsB)) return null;
  const latest = Math.min(spanA.end, a.duration - endRoom(), a.duration - LOOP_ROOM_S);
  let last = barsA.findLastIndex((time) => time <= latest);
  const phraseEnd = phraseBars(a, barsA).filter((index) => index <= last).at(-1);
  if (phraseEnd !== undefined && last - phraseEnd < 4) last = phraseEnd;
  const at = rollBar(a, barsA, last, options.earliest);
  if (at < 1) return null;
  const { from, unit } = rolled(barsA, at);
  const drop = barsA[at];
  const span = drop - from;
  const firstB = barsB.findIndex((time) => time >= spanB.start - 0.05);
  if (firstB < 0) return null;
  const full = fullFrom(b);
  const fullBar = barAtFull(barsB, full);
  const entries = [fullBar, ...barsB.keys()].filter((i) => i >= firstB && barsB[i] >= span && barsB[i] <= b.duration / 4);
  if (!entries.length) return null;
  const entry = barsB[entries[0]];
  const cut = drop - SWAP_AHEAD_S;
  const rolls = [[from + 2 * unit, unit / 2], [from + 3 * unit, unit / 4]];
  const keys = options.can.has("crossover") ? darkRoll(from, rolls, cut) : thinRoll(from, rolls, cut);
  const loop = { from, length: unit, rolls };
  // A cut straight into the drop was heard as too raw (24/11): the quarter beats ring on, a beat apart.
  const echo = options.can.has("echo") ? { from: cut - unit, to: cut, delay: unit, feedback: ROLL_ECHO_FEEDBACK } : null;
  const plan = { bStart: entry - span, keys, loop, ...(echo ? { echo } : {}) };
  return { kind: "roll", bars: 1, style: "roll", plan };
}

// The index of the bar where a song is first in full (`full`, from fullFrom), half a bar early at most.
function barAtFull(bars, full) {
  return bars.findIndex((time) => time >= full - barLength(bars, full) / 2);
}

// A roll's keys with the crossover's filters, [time, A, B, A's lows, B's lows, A's high-pass, B's low-pass, A's
// low-pass, B's high-pass]: the roll darkens from its start, bass and all, while the next song creeps in thin from the
// half beats, then cuts in whole on the drop.
function darkRoll(from, rolls, cut) {
  const [halves, quarters] = [rolls[0][0], rolls[1][0]];
  const muffle = ROLL_MUFFLE_MID * (halves - from) / (quarters - from);  // A's low-pass, on its way down
  const peek = PEEK_GAIN * (quarters - halves) / (cut - SWAP_AHEAD_S - halves);
  return [
    [from, 1, 0, 1, 0, 0, 0, 0, PEEK_THIN],
    [halves, 1, 0, 1, 0, 0, 0, muffle, PEEK_THIN],
    [quarters, 1, peek, 1, 0, 0, 0, ROLL_MUFFLE_MID, PEEK_THIN],
    [cut - SWAP_AHEAD_S, 1, PEEK_GAIN, 1, 0, 0, 0, ROLL_MUFFLE, PEEK_THIN],
    [cut, 0, 1, 1, 1, 0, 0, ROLL_MUFFLE, 0],
  ];
}

// The same for a kernel without them: thinning out, its bass gone with the quarter beats, not before (a rap song is
// mostly its bass: two seconds without it were heard as a hole, 12 to 18 dB down in the 24/11 series).
function thinRoll(from, rolls, cut) {
  const bassOut = rolls[1][0];
  return [
    [from, 1, 0, 1, 1, 0, 0],
    [bassOut, 1, 0, 1, 1, ROLL_SWEEP_MID, 0],
    [bassOut + BASS_CUT_S, 1, 0, 0, 1, ROLL_SWEEP_MID, 0],
    [cut - SWAP_AHEAD_S, 1, 0, 0, 1, ROLL_SWEEP, 0],
    [cut, 0, 1, 0, 1, ROLL_SWEEP, 0],
  ];
}

// What of the bar before `bars[at]` a roll plays: from its first beat, a beat at a time, or for a slow song (a beat
// of SLOW_BEAT_S or more, a whole bar rolled lasting four seconds and more) from its third, half a beat at a time.
function rolled(bars, at) {
  const beat = (bars[at] - bars[at - 1]) / 4;
  const unit = beat >= SLOW_BEAT_S ? beat / 2 : beat;
  return { from: bars[at] - 4 * unit, unit };
}

// The bar a roll lands on (the index of the bar after the one rolled), -1 when none fits: the current song's `last`,
// else, when that one is sung or already fading (an outro), the latest up to ROLL_BACK_BARS earlier that ends one of
// its phrases, else any of them, as a DJ rolls out before the end: what it skips is an instrumental end or a quieter
// outro, never the singer's last lines (SKIP_VOCALS). What is rolled is unsung on its first turn, the only part heard
// again, and in full; a turn no shorter than the kernel's shortest loop.
function rollBar(a, bars, last, earliest) {
  const skips = (k) => vocalShare(a, bars[k], bars[last]) < SKIP_VOCALS || levelOver(a, bars[k], bars[last]) < -DROP_DB;
  const near = (k) => k >= 1 && k < last && last - k <= ROLL_BACK_BARS && skips(k);
  const latestFirst = (list) => list.filter(near).reverse();
  const tries = [last, ...latestFirst(phraseBars(a, bars)), ...latestFirst([...bars.keys()])];
  const found = tries.find((k) => {
    if (!(k >= 1)) return false;
    const { from, unit } = rolled(bars, k);
    return unit >= MIN_LOOP_S && from > earliest && vocalShare(a, from, from + unit) < LOOP_VOCALS &&
      levelOver(a, from, bars[k]) >= -DROP_DB;
  });
  return found ?? -1;
}

// The mix in the style the pair draws among those on the beat (stabs, drop, crossover, roll), with a kernel that has
// the gate; null when none fits.
function beatStyled(a, b, spanA, spanB, options) {
  const order = styleOrder(a, b, keysClash(a, b) ? CLASH_STYLES : BEAT_STYLES, options);
  const roll = order.indexOf("roll");
  for (let i = 0; i < order.length; i++) {
    if (i === roll) continue;
    const mix = beatMix(a, b, spanA, spanB, options, order[i]);
    if (!mix) continue;
    // The roll needs no common tempo: among these only for songs that share one, when the pair drew it first.
    return (roll >= 0 && roll < i && rollMix(a, b, spanA, spanB, options)) || mix;
  }
  return null;
}

// Then among those that need no common tempo (roll, echo).
function freeStyled(a, b, spanA, spanB, options) {
  for (const style of styleOrder(a, b, FREE_STYLES, options)) {
    const mix = style === "roll" ? rollMix(a, b, spanA, spanB, options) : echoSwap(a, b, spanA, spanB, options);
    if (mix) return mix;
  }
  return null;
}

// The next song's volume under the current song's last bars, from its bar `from` to its bar `to`: its music there
// BUILD_BELOW_DB below its own level, so that it rises behind the current song; a quiet intro is left as it is.
function buildGain(song, from, to) {
  return Math.min(1, Math.max(MIN_BUILD_GAIN, 10 ** ((-BUILD_BELOW_DB - levelOver(song, from, to)) / 20)));
}

// The next song's bars a mix on the beat may drop it on, best first: the phrase where its music is in full (`full`),
// then the two phrases after it. Without Apple's structure a phrase starts on every fourth bar.
function dropBars(song, bars, first, full) {
  const starts = phraseBars(song, bars).filter((bar) => bar >= first);
  if (!starts.length) {
    for (let bar = Math.ceil(first / 4) * 4; bar < bars.length; bar += 4) starts.push(bar);
  }
  const at = starts.findIndex((bar) => bars[bar] >= full - barLength(bars, full) / 2);
  return at < 0 ? [] : starts.slice(at, at + 3);
}

// The current song's fade after a swap or a drop, as heard: three quarters of a bar within MIN and MAX_SWAP_TAIL_S.
function swapTail(bar) {
  return Math.min(Math.max(bar * SWAP_TAIL_BARS, MIN_SWAP_TAIL_S), MAX_SWAP_TAIL_S);
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

// Why two songs cannot be put together on their bars, or "" when they can. A part with little beat still has Apple's
// bars, and the beats a mix lays together must fall together (worstBeatGap): it mixes as well.
function beatless(barsA, barsB) {
  return barsA.length < 8 || barsB.length < 8 ? "Apple gives one of them no bars" : "";
}

// Bars a beat mix lasts, by style, from the longest to the shortest tried.
function barChoices(style) {
  if (style === "Long blends") return [16, 8, 4];
  if (style === "Short blends") return [4, 2];
  return [8, 4, 2];
}

// A mix on the beat, as a DJ plays one: the current song, brought to the next one's tempo, plays on in full to the end
// of a phrase; under its last bars the next song's intro rises without its bass, and on the bar that ends the phrase
// it drops in full, bass and all, on one of its own phrases, the one where its music is in full when it can. Null
// when the songs cannot share a tempo.
function beatMix(a, b, spanA, spanB, options, style) {
  const barsA = barsOf(a);
  const barsB = barsOf(b);
  if (beatless(barsA, barsB)) return null;
  const match = tempoMatch(barLength(barsA, spanA.end), barLength(barsB, spanB.start));
  if (!(Math.abs(match.rate - 1) <= MAX_STRETCH)) return null;
  const rate = Math.abs(match.rate - 1) > NO_STRETCH ? match.rate : 1;
  // Without a style (a kernel without the gate), the crossover when the kernel has it, else the drop it always took.
  const crossover = style ? style === "crossover" : options.can.has("crossover");
  const styled = style === "stabs" || style === "drop";
  const barA = barLength(barsA, spanA.end);
  const tail = crossover ? 0 : styled ? exitTail(barA) : swapTail(barA);  // the crossover is over on the bar
  const latest = Math.min(spanA.end, a.duration - endRoom(rate) - tail * (1 + MAX_STRETCH));
  let last = barsA.length - 1;
  while (last > 0 && barsA[last] > latest) last--;
  const phraseEnd = phraseBars(a, barsA).filter((bar) => bar <= last).at(-1);
  if (phraseEnd !== undefined && last - phraseEnd < 4) last = phraseEnd;
  const first = barsB.findIndex((t) => t >= spanB.start - 0.05);
  if (first < 0) return null;
  const full = fullFrom(b);
  const drops = dropBars(b, barsB, first, full);
  const choices = barChoices(options.style);
  const short = Math.abs(rate - 1) > SHORT_ABOVE || keysClash(a, b);
  const counts = short && choices.some((count) => count <= SHORT_BARS)
    ? choices.filter((count) => count <= SHORT_BARS) : choices;
  const grids = { barsA, barsB, beatsA: beatsOf(a), beatsB: beatsOf(b), span: match.span };
  const name = style ?? (crossover ? "crossover" : "drop");
  const shape = { tail, filtered: options.can.has("filter"), crossover, style: name, styled };
  const tries = fitOrder(counts, drops, full > barsB[first] + barLength(barsB, barsB[first]) / 2);
  const plain = (() => {
    let clashing = null;  // the best crossover whose singers meet as it hands over, kept if no other fits
    for (const [count, drop] of tries) {
      const from = last - count;
      if (from < 0) continue;
      const fit = beatFit(a, grids, from, last, drop, { ...options, tail });
      if (!fit) continue;
      // Two singers at once for long sounds like a clash: then only the shortest mix.
      const sings = vocalShare(a, fit.aStart, fit.aEnd) > 0.4;
      const clash = sings && vocalShare(b, fit.bStart, fit.bDrop) > 0.4;
      if (clash && count > 2 && counts.includes(2)) continue;
      if (choppedSinger(b, fit, style)) continue;
      const mix = { kind: "beat", bars: count, plan: dropPlan(b, fit, grids, shape), sings, style: name };
      if (!crossover || handover(a, b, fit) <= MAX_HAND_SINGING_S) return mix;
      clashing ??= mix;
    }
    return clashing;
  })();
  // A song that stops on a hit and would be mixed over its singer, shorter than the style wants or not at all, loops
  // a bar without its singer instead, when that gives a longer mix or one without the singer.
  if (options.can.has("loop") && spanA.silent - spanA.end < ABRUPT_S && (!plain || plain.sings || plain.bars < counts[0])) {
    const looped = loopMix(a, b, grids, tries, options, shape);
    if (looped && (!plain || plain.sings || looped.bars > plain.bars)) return looped;
  }
  return plain;
}

// The [bars, drop] a mix on the beat tries, best first. A next song whose music arrives in full after a quieter intro
// drops there, over as many bars as fit; one in full from its start takes the longest mix, on whichever phrase.
function fitOrder(counts, drops, intro) {
  return intro ? drops.flatMap((drop) => counts.map((count) => [count, drop]))
    : counts.flatMap((count) => drops.map((drop) => [count, drop]));
}

// The plan of a mix on the beat from its fit: the keys of the drop, or of the crossover with a kernel that has it,
// and the speed when the current song changes it.
function dropPlan(b, fit, grids, { filtered, crossover, style, styled }) {
  const beat = barLength(grids.barsA, fit.aEnd) / 4;
  const gain = buildGain(b, fit.bStart, fit.bDrop);
  const gate = style === "stabs" ? stabsOf(grids, fit, beat) : null;
  let keys;
  if (crossover) keys = crossoverKeys(fit.aStart, fit.aEnd, beat, fit.rate);
  else if (styled) keys = buildKeys(fit.aStart, fit.aEnd, beat * 4, fit.tail * fit.rate, fit.rate, Boolean(gate));
  else keys = dropKeys(fit.aStart, fit.aEnd, fit.aEnd + fit.tail * fit.rate, beat, gain, fit.rate, filtered);
  return {
    bStart: fit.bStart,
    keys,
    ...(fit.rate !== 1 ? { rate: fit.rate, rateFrom: fit.aStart - fit.ramp } : {}),
    ...(gate ? { gate } : {}),
  };
}

// Apple's measure looping, for a current song that stops on a hit: one of its last bars that the kernel can loop, one
// without its singer, plays over and over while the next song comes in on the beat. The next song's first bars ride
// on that groove rather than on the current song's last phrase, and the hit that would stop it dead never comes. A's
// time runs on through the loop: the keys and the speed are those of the bar looped. Null when no such bar fits.
function loopMix(a, b, grids, tries, options, shape) {
  const { barsA, beatsA } = grids;
  const candidates = [];
  for (let k = barsA.length - 2; k >= 1 && candidates.length < LOOP_CANDIDATES; k--) {
    if (barsA[k + 1] + LOOP_ROOM_S <= a.duration) candidates.push(k);
  }
  const i = candidates.find((k) => vocalShare(a, barsA[k], barsA[k + 1]) < LOOP_VOCALS);
  if (i === undefined) return null;
  for (const [count, drop] of tries.filter(([bars]) => bars <= MAX_LOOP_BARS)) {
    // One turn more than the mix's bars: the current song fades out over it after the drop.
    const looped = loopedGrid(barsA, beatsA, i, count + 1);
    const loopGrids = { ...grids, barsA: looped.bars, beatsA: looped.beats };
    const fit = beatFit(a, loopGrids, i, i + count, drop, { ...options, looped: true, tail: shape.tail });
    // The kernel lets go of the second player once it holds the bar: the room it wants is after the first key.
    if (!fit || a.duration - fit.aStart < endRoom(fit.rate) || choppedSinger(b, fit, shape.style)) continue;
    const plan = { ...dropPlan(b, fit, loopGrids, shape), loop: { from: looped.from, length: looped.length } };
    return { kind: "loop", bars: count, plan, style: shape.style };
  }
  return null;
}

// The current song's bars `from` to `last` laid on the next song's bars that end on bar `drop`, the next song then
// going on alone after `options.tail` seconds: the speed is the one at which those very bars of A last as long as B's
// (Apple's tempo moves along a song, so the tempo near either end misled the mix), lined up on the middle bar, and
// kept only when every beat of the mix falls with one of the other song's.
function beatFit(a, grids, from, last, drop, options) {
  const { barsA, barsB, beatsA, beatsB, span } = grids;
  const count = last - from;
  const middle = count / 2;
  const across = middle * span;
  const entry = drop - count * span;
  if (!Number.isInteger(across) || entry < 0 || drop >= barsB.length) return null;
  const aStart = barsA[from];
  const aEnd = barsA[last];
  const fitted = (aEnd - aStart) / (barsB[drop] - barsB[entry]);
  if (!(Math.abs(fitted - 1) <= MAX_STRETCH)) return null;
  const rate = Math.abs(fitted - 1) > NO_STRETCH ? fitted : 1;
  if (Math.abs(rate - 1) > SHORT_ABOVE && count > SHORT_BARS) return null;  // every style tries four bars or fewer
  const build = (aEnd - aStart) / rate;
  const ramp = rate !== 1 ? Math.min(RAMP_S, aStart - options.earliest) : 0;
  if (build + options.tail > options.maxSeconds || aStart < options.earliest || (rate !== 1 && ramp < MIN_RAMP_S)) {
    return null;
  }
  if (!options.looped && aEnd + options.tail * rate > a.duration - endRoom(rate)) return null;
  // Apple's times are whole milliseconds: a next song that comes in on its very first bar may land a hair before 0.
  const lined = barsB[entry + across] - (barsA[from + middle] - aStart) / rate;
  if (lined < -MS_SLACK_S) return null;
  const bStart = Math.max(0, lined);
  const gap = (length) => worstBeatGap(beatsA, beatsB, aStart, rate, bStart, length);
  if (gap(build) > MAX_BEAT_GAP_S) return null;
  // Past the drop the current song's beats may drift from the next song's (the speed was fitted to the bars before
  // it): then it fades out sooner, never within less than MIN_SWAP_TAIL_S.
  let tail = options.tail;
  while (tail > MIN_SWAP_TAIL_S && gap(build + tail) > MAX_BEAT_GAP_S) tail = Math.max(MIN_SWAP_TAIL_S, tail - TAIL_STEP_S);
  if (gap(build + tail) > MAX_BEAT_GAP_S) return null;
  return { aStart, aEnd, rate, ramp, tail, bStart, bDrop: bStart + build };
}

// A crossfade on the music itself: the next song, from where it is first heard, comes in under the current song's
// last seconds and is at full once the current one has faded to nothing. When the current song fades out by itself,
// the next one is nearly full when that fade begins, so no hole opens between them; a next song with a quiet intro
// plays it under that fade, its drop landing where the fade has taken the current song away.
function crossfade(a, b, spanA, spanB, options) {
  const aEnd = Math.min(a.duration - endRoom(), spanA.silent);
  const fade = aEnd - spanA.end;
  const wanted = fade > 0.5 ? fade + 1 : CROSSFADE_S;
  const length = Math.min(options.maxSeconds, MAX_CROSSFADE_S, Math.max(MIN_CROSSFADE_S, wanted));
  const aStart = aEnd - length;
  const bStart = fadeEntry(a, b, spanA, spanB, aStart, aEnd);
  if (!(aStart > options.earliest) || !(bStart + length < b.duration)) return null;
  // Filtered, the next song comes in muffled and opens up, fully once the current one fades by itself.
  const sweep = options.can.has("filter") ? FADE_SWEEP : 0;
  const key = (at, x, y, thin, muffled) => (sweep ? [at, x, y, 1, 1, thin * sweep, muffled * sweep] : [at, x, y]);
  const keys = fade > 0.5 && spanA.end - aStart >= 1
    ? [key(aStart, 1, 0, 0, 1), key(spanA.end, 1, OVER_FADE_GAIN, 0.5, 0), key(aEnd, 0, 1, 1, 0)]
    : [key(aStart, 1, 0, 0, 1), key(aEnd, 0, 1, 1, 0)];
  return { kind: "crossfade", bars: 0, plan: { bStart, keys } };
}

// Where a crossfade starts the next song: where it is first heard, or, for a quiet intro (24/11: seven seconds 17 dB
// down under the current song's fade left four of near silence), so that its drop lands as that fade takes the
// current song FADED_DB down.
function fadeEntry(a, b, spanA, spanB, aStart, aEnd) {
  const full = fullFrom(b);
  if (full - spanB.sound < QUIET_INTRO_S || full > b.duration / 4) return spanB.sound;
  const gone = fadedBy(a, spanA.end, aEnd);
  // Already that far down where the crossfade starts (a long fade, the crossfade capped): the drop lands there.
  return gone === null ? spanB.sound : Math.max(spanB.sound, full - Math.max(0, gone - aStart));
}

// The bar of the next song a mix lands on: where its music is first in full (its drop) when that is within its first
// quarter, else `first`.
function dropBar(song, bars, first) {
  const full = fullFrom(song);
  const at = barAtFull(bars, full);
  return at >= first && bars[at] <= song.duration / 4 ? at : first;
}

// When the current song's own fade has taken it FADED_DB below its loudness, between `from` and `to`; null without
// Apple's curve or when it has not by then.
function fadedBy(song, from, to) {
  const loudness = loudnessOf(song);
  if (!loudness) return null;
  const { curve, step, loud } = loudness;
  for (let i = Math.max(0, Math.ceil(from / step)); i < curve.length && i * step <= to; i++) {
    if (curve[i] < loud - FADED_DB) return i * step;
  }
  return null;
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
  if (beatless(barsA, barsB) || spanA.silent - spanA.end >= OWN_FADE_S) return null;
  const bar = barLength(barsA, spanA.end);
  // The current song may go on half a beat past the bar.
  let last = barsA.findLastIndex((time) => time <= Math.min(spanA.end, a.duration - endRoom() - ECHO_DELAY[1] / 2));
  const phraseEnd = phraseBars(a, barsA).filter((index) => index <= last).at(-1);
  if (phraseEnd !== undefined && last - phraseEnd < 4) last = phraseEnd;
  const first = barsB.findIndex((time) => time >= spanB.start - 0.05);
  if (last < 0 || first < 0) return null;
  const swapAt = barsA[last];
  if (options.can.has("gate")) return smoothEcho(a, b, barsA, barsB, last, first, options);
  const entry = barsB[first];
  const lead = Math.min(bar, entry);  // the next song plays unheard before the bar it lands on, from its start at most
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

// With the styles the echo out goes as a roll does (n5-4, 24/11: "let it come in lightly a little before, filtered,
// then the drop right as the transition is full"): over the current song's last two bars the next one creeps in thin
// and without its bass, heard from the last one on (n6-1: "it comes in a bit too late"), while the current one
// darkens a little over the half bar, bass and all; its last two beats go into an echo on the next song's beats,
// louder than the kernel's own (n6-1: "you don't hear the echo enough"), and on the bar the current one stops and the
// next one is in full on its drop. Without the crossover's filters the next one creeps in muffled. Null when too
// little comes before that drop.
function smoothEcho(a, b, barsA, barsB, last, first, options) {
  const swapAt = barsA[last];
  const delay = echoDelay(barLength(barsB, barsB[first]) / 4);
  const lastBar = barLength(barsA, swapAt);
  const need = Math.max(lastBar / 2, 2 * delay) + 2 * SWAP_AHEAD_S;  // the least of the next song before its drop
  const dropAt = dropBar(b, barsB, first);
  const at = barsB.findIndex((time, i) => i >= dropAt && time >= need && time <= b.duration / 4);
  if (at < 0) return null;
  const drop = barsB[at];
  // From the current song's bar two before the last, or the last when two are long, as far back as the next song has
  // room.
  const aStart = [last - 2, last - 1]
    .filter((i) => i >= 0 && (i === last - 1 || swapAt - barsA[i] <= ECHO_CREEP_S))
    .map((i) => Math.max(barsA[i], swapAt - drop))
    .find((time) => swapAt - time >= need && time > options.earliest);
  if (aStart === undefined || !(drop + lastBar < b.duration)) return null;
  const [dark, cut] = [swapAt - lastBar / 2, swapAt - SWAP_AHEAD_S];
  // Each step of the rise a quarter bar at least after where it starts (no jump of the volume), in time order even
  // when the bar before the last is a short one.
  const rise = [[barsA[last - 1], ECHO_PEEK[0]], [dark, ECHO_PEEK[1]], [cut - SWAP_AHEAD_S, ECHO_PEEK[2]]]
    .filter(([time], i, steps) => time - aStart >= lastBar / 4 && steps.slice(i + 1).every(([later]) => later > time));
  const thin = options.can.has("crossover");
  const muffle = (time) => (time >= dark ? ECHO_MUFFLE * (time - dark) / (cut - SWAP_AHEAD_S - dark) : 0);
  const keys = [
    thin ? [aStart, 1, 0, 1, 0, 0, 0, 0, PEEK_THIN] : [aStart, 1, 0, 1, 0, 0, ECHO_RISE_MUFFLE],
    ...rise.map(([time, gain]) =>
      thin ? [time, 1, gain, 1, 0, 0, 0, muffle(time), PEEK_THIN] : [time, 1, gain, 1, 0, 0, ECHO_RISE_MUFFLE]),
    thin ? [cut, 0, 1, 1, 1, 0, 0, ECHO_MUFFLE, 0] : [cut, 0, 1, 1, 1, 0, 0],
  ];
  const echo = { from: swapAt - 2 * delay, to: cut, delay, feedback: ECHO_FEEDBACK, level: ECHO_LEVEL };
  return { kind: "echo", bars: 0, plan: { bStart: Math.max(0, drop - (swapAt - aStart)), keys, echo } };
}

// The current song to its very end, then the next one from its first sound after a breath, Apple's player alone
// moving from one to the other with the silences between them cut short: for a song that stops on a hit, its own
// end, and for a soft ending (a ballad, an acoustic or ambient outro), left to breathe down to its last sound. A cold
// cut from a hit into a next song that starts strong, a breath otherwise. Null for another song that fades, or when
// there is too little silence to take away for this to differ from Apple's gapless playback.
function naturalEnd(a, b, spanA, spanB, options) {
  const abrupt = spanA.silent - spanA.end < ABRUPT_S;
  const gentle = soft(a, "ending");
  if (!options.can.has("cut") || !(abrupt || gentle)) return null;
  const breath = abrupt && !gentle && spanB.start - spanB.sound < ABRUPT_S ? COLD_BREATH_S : NATURAL_BREATH_S;
  const ended = Math.min(spanA.silent + HIT_RING_S, a.duration - CUT_MARGIN_S);
  const ownSilence = Math.min(breath, spanB.sound);  // what of the breath is the next song's own silence
  const at = Math.min(ended + breath - ownSilence, a.duration - CUT_MARGIN_S);
  const bStart = spanB.sound - ownSilence;
  if (!(at > options.earliest) || a.duration - at + bStart < MIN_TRIM_S) return null;
  const joined = softJoin(a, b, spanA, spanB, ended, options);
  if (joined) return { kind: "natural", bars: 0, plan: joined, joined: true, soft: gentle };
  const keys = risingIn(a, b, spanA, bStart, at, options) ?? [[at, 1, 0], [at, 0, 1]];
  return { kind: "natural", bars: 0, plan: { bStart, keys }, cold: breath === COLD_BREATH_S, soft: gentle };
}

// How much louder the next song plays from `bStart` than the current one's last seconds, in dB.
function levelJump(a, b, spanA, bStart) {
  return levelOver(b, bStart, bStart + LEVEL_SPAN_S) - levelOver(a, spanA.end - LEVEL_SPAN_S, spanA.end);
}

// With the styles a natural end joins the next song in a short crossfade over its last ring: the next song from its
// first sound, muffled a little, in full JOIN_S later (NATURAL_IN_S and more muffled when it is NATURAL_JUMP_DB
// louder) as the current one fades out. Null without the filters or the room the kernel wants after the mix.
function softJoin(a, b, spanA, spanB, ended, options) {
  if (!options.can.has("gate") || !options.can.has("filter")) return null;
  const bStart = spanB.sound;
  const jump = levelJump(a, b, spanA, bStart);
  const [length, muffle] = jump >= NATURAL_JUMP_DB ? [NATURAL_IN_S, NATURAL_IN_MUFFLE] : [JOIN_S, JOIN_MUFFLE];
  const at = Math.min(ended - JOIN_OVERLAP_S, a.duration - endRoom() - length);
  if (!(at > options.earliest)) return null;
  return { bStart, keys: [[at, 1, 0, 1, 1, 0, muffle], [at + length, 0, 1, 1, 1, 0, 0]] };
}

// After the end, a next song NATURAL_JUMP_DB louder at its start than the current song's last seconds comes in
// muffled and low and rises to full over NATURAL_IN_S, the current song silent by then; null for the cut, when it is
// not that much louder, without the filters, or without the room the kernel wants after the mix.
function risingIn(a, b, spanA, bStart, at, options) {
  if (!options.can.has("filter") || a.duration - (at + NATURAL_IN_S) < endRoom()) return null;
  const jump = levelJump(a, b, spanA, bStart);
  if (!(jump >= NATURAL_JUMP_DB)) return null;
  return [
    [at, 1, 0, 1, 1, 0, 0],
    [at + SWAP_AHEAD_S, 0, NATURAL_IN_GAIN, 1, 1, 0, NATURAL_IN_MUFFLE],
    [at + NATURAL_IN_S, 0, 1, 1, 1, 0, 0],
  ];
}

// A swap on a bar, for songs with a beat that cannot share a tempo. The current song plays whole, bass included, up
// to the end of a phrase; under its last bar the next song's intro rises without its bass; on that bar's end the
// basses change hands and the next song's first bar lands in full, while the current song fades out, bassless, over
// three quarters of a bar, so that nothing stops dead. Null when the current song fades out by itself (a crossfade follows that
// fade) or the next song has nothing before its first bar to come in from.
function barSwap(a, b, spanA, spanB, options) {
  const barsA = barsOf(a);
  const barsB = barsOf(b);
  if (beatless(barsA, barsB) || spanA.silent - spanA.end >= OWN_FADE_S) return null;
  const bar = barLength(barsA, spanA.end);
  const tail = swapTail(bar);
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
  const gentle = soft(a, "ending");
  if (options.style !== "Crossfade only") {
    const mix = shapedMix(a, b, spanA, spanB, options, gentle);
    if (mix) return mix;
  }
  const fade = crossfade(a, b, spanA, spanB, options);
  if (!fade) return { kind: "none", why: "no room for a mix" };
  return { ...fade, why: crossfadeWhy(a, b, spanA, options, gentle) };
}

// The mix other than a crossfade, or null. A soft ending breathes before the next song (or fades as Apple's player
// would when it has no silence to breathe in); otherwise a mix on the beat, a hit's own end when none follows it, and
// for two songs that cannot share a tempo an echo out, the next landing on the bar.
function shapedMix(a, b, spanA, spanB, options, gentle) {
  const ending = naturalEnd(a, b, spanA, spanB, options);
  if (gentle) return ending;
  if (options.can.has("gate")) {
    // A song that stops on a hit and no mix on the beat: its own end, else the styles that need no common tempo.
    const beat = beatStyled(a, b, spanA, spanB, options);
    if (beat || ending) return beat ?? ending;
    return freeStyled(a, b, spanA, spanB, options);
  }
  const beat = beatMix(a, b, spanA, spanB, options);
  if (beat || ending) return beat ?? ending;
  return options.can.has("echo") ? echoSwap(a, b, spanA, spanB, options) : barSwap(a, b, spanA, spanB, options);
}

// Why a crossfade rather than a mix on the beat, for the log: the first thing in the way.
function crossfadeWhy(a, b, spanA, options, gentle) {
  if (options.style === "Crossfade only") return "";
  if (gentle) return "this song ends softly";
  const ownFade = spanA.silent - spanA.end >= OWN_FADE_S ? "this song fades out by itself" : "";
  return beatless(barsOf(a), barsOf(b)) || ownFade || "no room for a swap on a bar";
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

// How a song left to its own end goes into the next one, for the log.
function endingNote(decision) {
  if (decision.joined) {
    return decision.soft ? "this song left to end softly, the next one crossfading in under its last sound"
                         : "this song to its last hit, the next one crossfading in under its ring";
  }
  if (decision.soft) return "this song left to end softly, then a breath";
  return decision.cold ? "this song to its last hit, then a cold cut" : "this song to its last hit, then a breath";
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
  const song = (played, info) => ({ id: played.id || played.name, duration: played.duration, analysis: info?.analysis,
                                    structure: info?.structure, attributes: info?.attributes, atmos: played.atmos });
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
  const through = { crossover: ", through the crossover", stabs: ", the next song in stabs until its drop",
                    drop: ", the next song dropping in full" }[decision.style] ?? "";
  const kinds = {
    beat: `${decision.bars} bars on the beat${through}${speedNote(decision.plan.rate)}`,
    loop: `${decision.bars} bars on the beat over this song's last bar, looped${through}${speedNote(decision.plan.rate)}`,
    roll: "this song's last bar rolled into the next song's drop",
    swap: "a swap on a bar",
    echo: "an echo out on a bar",
    natural: endingNote(decision),
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
