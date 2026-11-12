// The plans of the automix extension (extensions/automix/main.js), on songs made up for each case: where a mix
// starts, on which bars, how long it lasts and when there is none. Node only, no dependency:
//
//     node --test tools/test_automix.mjs

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const source = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "extensions", "automix", "main.js"), "utf8");

// The script's functions, without its loop: Ampify.process is not "engine" here.
function load() {
  const context = { Ampify: { process: "test", log() {}, locale: "en-US" }, setInterval() {}, clearInterval() {} };
  vm.runInNewContext(source, context, { filename: "main.js" });
  return context;
}

const { planMix } = load();
const OPTIONS = { style: "Auto", maxSeconds: 20, albums: true, earliest: 30 };

// A song of `duration` seconds at `bpm`, four beats a bar from `firstBeat`, loud from `loudFrom` to `loudTo`, or
// with `level(t)` as its loudness curve (dB, one value every half second). `phraseEvery` and `phraseFrom` give it
// Apple's structure: a phrase starts on every `phraseEvery`-th bar from bar `phraseFrom`.
function song({ duration = 200, bpm = 124, firstBeat = 0, loudFrom = 0, loudTo = duration, vocals = [],
                attributes = {}, danceability = 0.8, fadeIn = null, fadeOut = null, level = null, key = null,
                melodicness = 0.7, phraseEvery = 0, phraseFrom = 0 } = {}) {
  const beat = 60 / bpm;
  const beats = [];
  for (let t = firstBeat; t < duration; t += beat) beats.push(Math.round(t * 1000));
  const bars = beats.filter((_, i) => i % 4 === 0);
  const curve = [];
  for (let t = 0; t < duration; t += 0.5) curve.push(level ? level(t) : t >= loudFrom && t < loudTo ? -8 : -60);
  const score = (i) => {
    if (i % 4 !== 0) return 200;
    return phraseEvery && (i / 4 - phraseFrom) % phraseEvery === 0 ? 700 : 400;
  };
  return {
    duration,
    structure: phraseEvery
      ? { videoEvents: { score: beats.map((_, i) => score(i)), timeInSeconds: beats.map((ms) => ms / 1000 + 0.002) } }
      : null,
    attributes: { name: "Song", albumName: "Album", artistName: "Artist", trackNumber: 1, discNumber: 1,
                  genreNames: ["Electronic"], ...attributes },
    analysis: {
      beats: { beatsInMilliseconds: beats, barsInMilliseconds: bars },
      bpm: { main: bpm, beginning: bpm, ending: bpm },
      danceability: { main: danceability, beginning: danceability, ending: danceability },
      melodicness: { main: melodicness, beginning: melodicness, ending: melodicness },
      ...(key ? { key: { main: key, beginning: key, ending: key } } : {}),
      loudness: { main: { value: -8 } },
      loudnessCurve: { value: curve },
      fades: { ...(fadeIn ? { fadeIn } : {}), ...(fadeOut ? { fadeOut } : {}) },
      vocalActivity: vocals.map(([from, to]) => ({ startInMilliseconds: from * 1000, endInMilliseconds: to * 1000,
                                                   strength: "very-high" })),
    },
  };
}

// The volumes of a plan at a time of the current song, as the kernel computes them (GainsAt, mix_plan.cpp).
function gainsAt(keys, at) {
  if (!(at > keys[0][0])) return { a: keys[0][1], b: keys[0][2] };
  if (at >= keys.at(-1)[0]) return { a: keys.at(-1)[1], b: keys.at(-1)[2] };
  const next = keys.findIndex((key) => key[0] > at);
  const [from, to] = [keys[next - 1], keys[next]];
  const x = Math.min(1, Math.max(0, (at - from[0]) / (to[0] - from[0])));
  const s = x * x * (3 - 2 * x);
  const between = (p, q) => Math.sqrt(p * p + (q * q - p * p) * s);
  return { a: between(from[1], to[1]), b: between(from[2], to[2]) };
}

// How far the mix falls, at its quietest, below the quieter of the current song just before it and the next song
// just after it, in dB, from the two loudness curves.
function deepestHole(a, b, plan) {
  const level = (s, time) => {
    const curve = s.analysis.loudnessCurve.value;
    return curve[Math.min(curve.length - 1, Math.max(0, Math.floor(time / (s.duration / curve.length))))];
  };
  const first = plan.keys[0][0];
  const last = plan.keys.at(-1)[0];
  let quietest = Infinity;
  for (let t = first; t <= last; t += 0.1) {
    const g = gainsAt(plan.keys, t);
    const power = g.a ** 2 * 10 ** (level(a, t) / 10) + g.b ** 2 * 10 ** (level(b, plan.bStart + t - first) / 10);
    quietest = Math.min(quietest, 10 * Math.log10(power));
  }
  return Math.min(level(a, first - 0.5), level(b, plan.bStart + last - first + 0.5)) - quietest;
}

// The rules the kernel checks before it takes a plan (src/core/automix/mix_plan.cpp, Check).
function assertPlayable(plan, a, b) {
  const keys = plan.keys;
  assert.ok(keys.length >= 2);
  for (let i = 1; i < keys.length; i++) assert.ok(keys[i][0] >= keys[i - 1][0], "keys in time order");
  for (const key of keys) {
    assert.ok(key.length === 3 || key.length === 5, "a time and two volumes, or the lows of both songs as well");
    for (const value of key.slice(1)) assert.ok(value >= 0 && value <= 1, "volumes and lows within 0..1");
  }
  assert.deepEqual(Array.from(keys[0].slice(1, 3)), [1, 0], "B comes in from silence under A at full");
  assert.deepEqual(Array.from(keys.at(-1).slice(1, 3)), [0, 1], "A gone, B full at the end");
  assert.equal(keys.at(-1)[4] ?? 1, 1, "B has its lows at the end");
  const rate = plan.rate ?? 1;
  const span = keys.at(-1)[0] - keys[0][0];
  assert.ok(span > 0 && span <= 60);
  const stretchRoom = rate === 1 ? 0 : 0.3 + Math.max(0, 1 - rate) * (2 * (keys[0][0] - plan.rateFrom) / (1 + rate) + span / rate);
  assert.ok(a.duration - keys.at(-1)[0] >= 3 + stretchRoom, "the song goes on after the mix: the kernel's second player runs ahead");
  assert.ok(plan.bStart >= 0 && plan.bStart + span / rate < b.duration);
  assert.ok(Math.abs(rate - 1) <= 0.1, "a rate the kernel takes");
  if (rate !== 1) {
    assert.ok(plan.rateFrom < keys[0][0] && keys[0][0] - plan.rateFrom <= 30, "the speed moves before the mix");
  } else {
    assert.equal(plan.rateFrom, undefined);
  }
}

// How far B's nearest bar is from where B is while A is at `t`, in seconds.
function barsApart(a, b, plan, t) {
  const barsB = b.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  const inB = plan.bStart + (t - plan.keys[0][0]) / (plan.rate ?? 1);
  return Math.min(...barsB.map((bar) => Math.abs(bar - inB)));
}

test("two songs at one tempo mix for eight bars, bar on bar, ending on the current song's last loud bar", () => {
  const a = song({ loudTo: 190 });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assert.equal(result.bars, 8);
  assertPlayable(result.plan, a, b);
  const bars = a.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  assert.ok(bars.includes(result.plan.keys[0][0]), "the mix starts on a bar of the current song");
  assert.ok(result.plan.keys.at(-1)[0] <= 190, "and ends before its music fades");
  const bBars = b.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  assert.ok(bBars.some((t) => Math.abs(t - result.plan.bStart) < 0.002), "the next song comes in on one of its bars");
  assert.ok(result.plan.keys[0][2] === 0 && result.plan.keys[0][1] === 1, "it comes in silent");
});

test("a silent start of the next song is skipped, up to its first phrase", () => {
  const a = song();
  const b = song({ loudFrom: 5, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assert.ok(result.plan.bStart >= 5 - 0.05, `B starts at ${result.plan.bStart}`);
});

// Where the next song is first heard in a plan, and the time of the next song that plays then.
function entryOf(plan) {
  const heard = plan.keys.findIndex((key) => key[2] > 0);
  const from = plan.keys[heard - 1];
  return { key: heard, from, bTime: plan.bStart + (plan.keys[heard][0] - plan.keys[0][0]) };
}

test("songs too far apart in tempo swap on a bar: the next song's first bar lands on a bar of the current one", () => {
  const a = song({ bpm: 124 });
  const b = song({ bpm: 138, firstBeat: 2.5, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "swap");
  assertPlayable(result.plan, a, b);
  const { key, from, bTime } = entryOf(result.plan);
  const swapAt = result.plan.keys[key][0];
  const bars = a.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  // the next song is in full a few hundredths before the bar, so that its first beat is heard whole
  assert.ok(bars.some((t) => t >= swapAt && t - swapAt < 0.05), `the swap is at ${swapAt}`);
  assert.ok(Math.abs(bTime - 2.5) < 0.05, `the next song is at ${bTime} then, its first bar is at 2.5`);
  assert.equal(result.plan.keys[key][2], 1, "the next song comes in at full");
  assert.equal(from[3], 0, "the current song has lost its bass by then");
  assert.equal(from[2], 0);
  const tail = result.plan.keys.at(-1)[0] - swapAt;
  assert.ok(tail > 0.3 && tail < 0.7, `the current song is gone a beat later (${tail} s)`);
  assert.ok(result.plan.keys.at(-1)[0] <= 200 - 3, "and the swap leaves the kernel its room");
});

test("a song that fades out by itself, or hardly danceable ones, keep the crossfade", () => {
  const b = song({ bpm: 138, firstBeat: 2.5, attributes: { albumName: "Other" } });
  const fading = song({ fadeOut: { startInMilliseconds: 190000, endInMilliseconds: 198000 } });
  assert.equal(planMix(fading, b, OPTIONS).kind, "crossfade");
  assert.equal(planMix(fading, b, OPTIONS).why, "this song fades out by itself", "the log says what was in the way");
  assert.equal(planMix(song({ danceability: 0.1 }), b, OPTIONS).why, "this song ends with little beat");
  // Apple reports a short fade at the end of most songs, where the last note dies: that one is swapped before
  const dying = song({ fadeOut: { startInMilliseconds: 196000, endInMilliseconds: 198000 } });
  const swap = planMix(dying, b, OPTIONS);
  assert.equal(swap.kind, "swap");
  assert.ok(swap.plan.keys.at(-1)[0] <= 196, "the swap is over when the last note starts dying");
  assert.equal(planMix(song({ danceability: 0.1 }), b, OPTIONS).kind, "crossfade");
  assert.equal(planMix(song(), song({ bpm: 138, firstBeat: 2.5, danceability: 0.1, attributes: { albumName: "Other" } }),
                       OPTIONS).kind, "crossfade");
});

test("a next song that starts right on its first bar has no room for a swap: a crossfade", () => {
  const b = song({ bpm: 138, attributes: { albumName: "Other" } });
  assert.equal(planMix(song(), b, OPTIONS).kind, "crossfade");
});

test("the current song is brought to the next one's tempo, and their bars stay together for the whole mix", () => {
  const a = song({ bpm: 124 });
  const b = song({ bpm: 128, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assert.equal(result.bars, 8);
  assertPlayable(result.plan, a, b);
  assert.ok(Math.abs(result.plan.rate - 128 / 124) < 1e-3, `rate ${result.plan.rate}`);
  assert.ok(Math.abs(result.plan.keys[0][0] - result.plan.rateFrom - 8) < 1e-9, "eight seconds to reach that speed");
  const barsA = a.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  const [start, end] = [result.plan.keys[0][0], result.plan.keys.at(-1)[0]];
  for (const t of [start, (start + end) / 2, end]) {
    assert.ok(barsA.some((bar) => Math.abs(bar - t) < 0.002), `a bar of the current song at ${t}`);
    const apart = barsApart(a, b, result.plan, t);
    assert.ok(apart <= 0.003, `bars ${(apart * 1000).toFixed(1)} ms apart at ${t}`);
  }
});

test("a slower next song slows the current one down", () => {
  const a = song({ bpm: 128 });
  const b = song({ bpm: 122, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assertPlayable(result.plan, a, b);
  assert.ok(Math.abs(result.plan.rate - 122 / 128) < 1e-3, `rate ${result.plan.rate}`);
});

test("tempos a hair apart are left alone: the bars drift 20 ms at most", () => {
  const a = song({ bpm: 124 });
  const b = song({ bpm: 124.1, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assertPlayable(result.plan, a, b);
  assert.equal(result.plan.rate, undefined);
  const [start, end] = [result.plan.keys[0][0], result.plan.keys.at(-1)[0]];
  for (const t of [start, (start + end) / 2, end]) assert.ok(barsApart(a, b, result.plan, t) <= 0.02 + 1e-9);
});

test("more than five percent of stretch keeps the mix to four bars", () => {
  const a = song({ bpm: 120 });
  const b = song({ bpm: 128, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assert.equal(result.bars, 4);
  assertPlayable(result.plan, a, b);
});

test("a song at half the tempo of the next shares its pulse: one bar on two", () => {
  const a = song({ bpm: 70 });
  const b = song({ bpm: 141, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assertPlayable(result.plan, a, b);
  assert.ok(Math.abs(result.plan.rate - 141 / 140) < 1e-3, `rate ${result.plan.rate}`);
  const [start, end] = [result.plan.keys[0][0], result.plan.keys.at(-1)[0]];
  for (const t of [start, end]) assert.ok(barsApart(a, b, result.plan, t) <= 0.003);
});

test("no room to reach the next song's tempo before the mix: a crossfade", () => {
  const a = song({ bpm: 124 });
  const b = song({ bpm: 128, attributes: { albumName: "Other" } });
  const result = planMix(a, b, { ...OPTIONS, earliest: 196 });
  assert.notEqual(result.kind, "beat");
});

test("the basses of the two songs change hands in the middle of a mix on the beat", () => {
  const a = song();
  const b = song({ attributes: { albumName: "Other" } });
  const keys = planMix(a, b, OPTIONS).plan.keys;
  assert.deepEqual(Array.from(keys[0].slice(3)), [1, 0], "the next song comes in without its lows");
  assert.deepEqual(Array.from(keys.at(-1).slice(3)), [0, 1], "and ends with them, the current song without");
  const [start, end] = [keys[0][0], keys.at(-1)[0]];
  for (const key of keys) {
    const x = (key[0] - start) / (end - start);
    assert.ok(key[3] + key[4] <= 1 + 1e-9, "never both basses in full");
    if (x < 0.4) assert.deepEqual(Array.from(key.slice(3)), [1, 0]);
    if (x > 0.6) assert.deepEqual(Array.from(key.slice(3)), [0, 1]);
  }
});

test("keys that clash keep the mix short, keys that agree do not", () => {
  const b = (key) => song({ key, attributes: { albumName: "Other" } });
  const a = song({ key: { tonic: "C", mode: "major" } });
  assert.equal(planMix(a, b({ tonic: "G", mode: "major" }), OPTIONS).bars, 8, "a fifth apart");
  assert.equal(planMix(a, b({ tonic: "A", mode: "minor" }), OPTIONS).bars, 8, "relative keys");
  assert.equal(planMix(a, b({ tonic: "F#", mode: "major" }), OPTIONS).bars, 4, "a tritone apart");
  assert.equal(planMix(a, b({ tonic: "C", mode: "minor" }), OPTIONS).bars, 4, "major against minor");
  const drums = song({ key: { tonic: "C", mode: "major" }, melodicness: 0.1 });
  const drumsToo = song({ key: { tonic: "F#", mode: "major" }, melodicness: 0.1, attributes: { albumName: "Other" } });
  assert.equal(planMix(drums, drumsToo, OPTIONS).bars, 8, "two songs with no melody to clash");
});

test("with Apple's structure the mix ends on a phrase of the current song and starts on one of the next", () => {
  const a = song({ loudTo: 190, phraseEvery: 4, phraseFrom: 1 });
  const b = song({ phraseEvery: 4, phraseFrom: 2, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assertPlayable(result.plan, a, b);
  const bar = 4 * 60 / 124;
  const lastBar = Math.round(result.plan.keys.at(-1)[0] / bar);
  assert.equal((lastBar - 1) % 4, 0, `the mix ends on bar ${lastBar}, a phrase of the current song`);
  assert.ok(result.plan.keys.at(-1)[0] <= 190);
  assert.ok(Math.abs(result.plan.bStart - 2 * bar) < 0.003, `the next song comes in at ${result.plan.bStart}`);
});

test("two singers at once keep the mix to its shortest", () => {
  const a = song({ vocals: [[100, 200]] });
  const b = song({ vocals: [[0, 100]], attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assert.equal(result.bars, 2);
});

test("the next song of the same album keeps Apple's gapless playback", () => {
  const a = song({ attributes: { trackNumber: 3 } });
  const b = song({ attributes: { trackNumber: 4 } });
  assert.equal(planMix(a, b, OPTIONS).kind, "none");
  assert.equal(planMix(a, b, { ...OPTIONS, albums: false }).kind, "beat");
});

test("classical music is left alone", () => {
  const a = song({ attributes: { genreNames: ["Classical"] } });
  const b = song({ attributes: { albumName: "Other" } });
  assert.equal(planMix(a, b, OPTIONS).kind, "none");
});

test("songs without Apple's analysis get a six-second crossfade over the song's last seconds", () => {
  const a = { duration: 180 };
  const b = { duration: 200 };
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "crossfade");
  assertPlayable(result.plan, a, b);
  assert.ok(Math.abs(result.plan.keys[1][0] - result.plan.keys[0][0] - 6) < 1e-9);
  assert.equal(result.plan.bStart, 0);
});

test("songs Apple finds a little danceable still mix on the beat, those it finds hardly so do not", () => {
  const b = song({ attributes: { albumName: "Other" }, danceability: 0.2 });
  assert.equal(planMix(song({ danceability: 0.2 }), b, OPTIONS).kind, "beat");
  assert.equal(planMix(song({ danceability: 0.1 }), b, OPTIONS).kind, "crossfade");
  assert.equal(planMix(song({ danceability: 0.2 }), song({ attributes: { albumName: "Other" }, danceability: 0.1 }),
                       OPTIONS).kind, "crossfade");
});

test("a crossfade follows the current song's own fade-out, the next song nearly full when it begins", () => {
  const a = song({ danceability: 0.1,fadeOut: { startInMilliseconds: 190000, endInMilliseconds: 198000 } });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "crossfade");
  assertPlayable(result.plan, a, b);
  // the song lasts 200 s: the mix is over 3.1 s before its end, a second into the last of its fade-out
  assert.deepEqual(Array.from(result.plan.keys, (key) => Array.from(key)), [[189, 1, 0], [190, 1, 0.7], [196.9, 0, 1]]);
});

// Measured on 12/11/2026: a rap song that dies out within a second of its fade-out's start, then a song that fades
// in. A crossfade over the whole fade Apple reports (five seconds) left a hole of 15 dB in the recording.
test("a crossfade leaves no hole when the current song dies out fast and the next one fades in", () => {
  const a = song({
    duration: 167.36, bpm: 143, danceability: 0.76,
    fadeIn: { startInMilliseconds: 0, endInMilliseconds: 2000 },
    fadeOut: { startInMilliseconds: 161600, endInMilliseconds: 166720 },
    level: (t) => (t < 162 ? -8.5 : t < 162.5 ? -11.8 : t < 163 ? -23 : t < 163.5 ? -30.6 : t < 164 ? -38 : -60),
  });
  const b = song({
    duration: 149.48, bpm: 91, attributes: { albumName: "Other" },
    fadeIn: { startInMilliseconds: 320, endInMilliseconds: 2320 },
    level: (t) => [-19.9, -14.5, -11, -10.7, -9.9, -10.6][Math.floor(t * 2)] ?? -10,
  });
  // both have a beat, 143 against 91 bpm: a swap on a bar, before the song dies out, and no hole either
  const swap = planMix(a, b, OPTIONS);
  assert.equal(swap.kind, "swap");
  assertPlayable(swap.plan, a, b);
  assert.ok(deepestHole(a, b, swap.plan) < 3);
  const result = planMix(a, b, { ...OPTIONS, style: "Crossfade only" });
  assert.equal(result.kind, "crossfade");
  assertPlayable(result.plan, a, b);
  assert.ok(result.plan.keys.at(-1)[0] < 163.5, `the mix is over once the song is silent (${result.plan.keys.at(-1)[0]})`);
  assert.ok(Math.abs(result.plan.bStart - 0.32) < 1e-9, "the next song comes in where it is first heard");
  const hole = deepestHole(a, b, result.plan);
  assert.ok(hole < 3, `the mix falls ${hole.toFixed(1)} dB below the quieter song`);
});

test("a mix that would start before the song can prepare it is not planned", () => {
  const a = song({ duration: 60 });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, { ...OPTIONS, earliest: 59 });
  assert.equal(result.kind, "none");
});

// ---- the loop that follows the queue (check()), with a made-up Ampify: songs A and B, no analysis unless given

const flush = () => new Promise((resolve) => setImmediate(resolve));

function follower({ state, schedule, http, clock, stored = {} }) {
  const kept = { token: { value: "token", at: clock?.() ?? Date.now() }, ...stored };
  const context = {
    Ampify: {
      process: "test", locale: "en-US", log() {},
      config: { get: (key) => ({ style: "Auto", longest: 20, albums: true })[key] },
      storage: {
        get: (key, fallback) => (key in kept ? kept[key] : fallback),
        set: (key, value) => { kept[key] = stored[key] = value; },
        remove: (key) => { delete kept[key]; },
      },
      http: { get: http ?? (async () => ({ status: 404, text: "" })) },
      automix: { state, schedule, cancel() {} },
    },
    setInterval() {}, clearInterval() {},
  };
  if (clock) context.Date = class extends Date { static now() { return clock(); } };
  vm.runInNewContext(source, context, { filename: "main.js" });
  return context;
}

// Apple's queue: A playing at `time`, B next. Without an analysis the crossfade starts 6.25 s before A ends.
function queue(time, nextDuration = 210, phase = "idle") {
  return {
    available: true, playing: true,
    current: { name: "A", id: "1", storefront: "143442", time, duration: 200 },
    next: { name: "B", id: "2", storefront: "143442", time: 0, duration: nextDuration },
    mix: { phase },
  };
}

async function looks(context, count) {
  for (let i = 0; i < count; i++) {
    context.check();
    await flush();
  }
}

test("a next song whose length is not known yet gets its mix once it is", async () => {
  let nextDuration = 0;
  const sent = [];
  const context = follower({ state: () => queue(160, nextDuration), schedule: (plan) => (sent.push(plan), { ok: true }) });
  await looks(context, 3);
  assert.equal(sent.length, 0);
  nextDuration = 210;
  await looks(context, 3);
  assert.equal(sent.length, 1);
  assert.equal(sent[0].next, "B");
  assert.ok(Math.abs(sent[0].keys[0][0] - 190.9) < 1e-9);  // six seconds, over 3.1 s before the end of 200 s
});

test("a plan the kernel refuses for a passing reason is sent again", async () => {
  const answers = [{ ok: false, reason: "Apple's player is not ready" }, { ok: true }];
  const sent = [];
  const context = follower({ state: () => queue(160), schedule: (plan) => (sent.push(plan), answers.shift() ?? { ok: true }) });
  await looks(context, 6);
  assert.equal(sent.length, 2);
});

test("a plan cancelled by a pause is sent again, three times at most", async () => {
  let phase = "idle";
  const sent = [];
  const context = follower({
    state: () => queue(160, 210, phase),
    schedule: (plan) => {
      sent.push(plan);
      phase = "failed";  // the user paused before the mix every time
      return { ok: true };
    },
  });
  await looks(context, 10);
  assert.equal(sent.length, 3);
});

test("a catalog that fails is asked again later, then the songs mix without its analysis", async () => {
  let now = 1_000_000;
  let asked = 0;
  const sent = [];
  const context = follower({
    state: () => queue(160),
    schedule: (plan) => (sent.push(plan), { ok: true }),
    http: async () => (asked++, { status: 503, text: "" }),
    clock: () => now,
  });
  await looks(context, 5);
  assert.equal(sent.length, 0, "no plan while the catalog may come back");
  const firstRound = asked;
  assert.ok(firstRound >= 1);
  await looks(context, 5);
  assert.equal(asked, firstRound, "not asked again before the pause between tries");
  for (let round = 0; round < 3; round++) {
    now += 16_000;
    await looks(context, 2);
  }
  assert.equal(sent.length, 1, "planned without the analysis after the last try");
});

// ---- Dolby Atmos: it goes out as spatial objects, which no mix can take

test("a song that plays in Dolby Atmos is not mixed out of", () => {
  const a = { ...song(), atmos: true };
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "none");
  assert.match(result.why, /Dolby Atmos/);
});

test("a next song with a Dolby Atmos version is mixed into only where such songs play in stereo", () => {
  const a = { ...song(), atmos: false };
  const b = song({ attributes: { albumName: "Other", audioTraits: ["atmos", "lossless"] } });
  assert.equal(planMix(a, b, OPTIONS).kind, "none", "not known yet: it may well play in Dolby Atmos");
  assert.match(planMix(a, b, OPTIONS).why, /Dolby Atmos/);
  assert.equal(planMix(a, b, { ...OPTIONS, atmosPlays: true }).kind, "none");
  assert.notEqual(planMix(a, b, { ...OPTIONS, atmosPlays: false }).kind, "none");
});

test("what a song with a Dolby Atmos version plays in is remembered for the next ones", async () => {
  const stored = {};
  const sent = [];
  let atmos = null;  // Apple's player has not loaded the song yet
  const catalog = async () => ({ status: 200, text: JSON.stringify({ data: [{ attributes: { audioTraits: ["atmos"] } }] }) });
  const state = () => ({ ...queue(160), current: { ...queue(160).current, atmos } });
  const context = follower({ state, schedule: (plan) => (sent.push(plan), { ok: true }), http: catalog, stored });
  await looks(context, 3);
  assert.equal(sent.length, 0, "both songs may play in Dolby Atmos");
  atmos = false;  // it plays in stereo: Dolby Atmos is off here
  await looks(context, 4);
  assert.equal(stored.atmosPlays, false);
  assert.equal(sent.length, 1, "decided again with what was learnt");
  const later = follower({ state, schedule: (plan) => (sent.push(plan), { ok: true }), http: catalog, stored });
  await looks(later, 3);
  assert.equal(sent.length, 2, "known from the start the next time");
});

test("crossfade only and off are honoured", () => {
  const a = song();
  const b = song({ attributes: { albumName: "Other" } });
  assert.equal(planMix(a, b, { ...OPTIONS, style: "Crossfade only" }).kind, "crossfade");
  assert.equal(planMix(a, b, { ...OPTIONS, style: "Off" }).kind, "none");
});
