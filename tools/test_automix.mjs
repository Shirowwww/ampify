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
// with `level(t)` as its loudness curve (dB, one value every half second).
function song({ duration = 200, bpm = 124, firstBeat = 0, loudFrom = 0, loudTo = duration, vocals = [],
                attributes = {}, danceability = 0.8, fadeIn = null, fadeOut = null, level = null } = {}) {
  const beat = 60 / bpm;
  const beats = [];
  for (let t = firstBeat; t < duration; t += beat) beats.push(Math.round(t * 1000));
  const bars = beats.filter((_, i) => i % 4 === 0);
  const curve = [];
  for (let t = 0; t < duration; t += 0.5) curve.push(level ? level(t) : t >= loudFrom && t < loudTo ? -8 : -60);
  return {
    duration,
    attributes: { name: "Song", albumName: "Album", artistName: "Artist", trackNumber: 1, discNumber: 1,
                  genreNames: ["Electronic"], ...attributes },
    analysis: {
      beats: { beatsInMilliseconds: beats, barsInMilliseconds: bars },
      bpm: { main: bpm, beginning: bpm, ending: bpm },
      danceability: { main: danceability, beginning: danceability, ending: danceability },
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
  for (const [, x, y] of keys) assert.ok(x >= 0 && x <= 1 && y >= 0 && y <= 1, "volumes within 0..1");
  assert.deepEqual(Array.from(keys[0].slice(1)), [1, 0], "B comes in from silence under A at full");
  assert.deepEqual(Array.from(keys.at(-1).slice(1)), [0, 1], "A gone, B full at the end");
  const span = keys.at(-1)[0] - keys[0][0];
  assert.ok(span > 0 && span <= 60 && keys.at(-1)[0] < a.duration);
  assert.ok(plan.bStart >= 0 && plan.bStart + span < b.duration);
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

test("songs too far apart in tempo get a crossfade instead of a beat mix", () => {
  const a = song({ bpm: 124 });
  const b = song({ bpm: 128, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "crossfade");
  assertPlayable(result.plan, a, b);
});

test("a small tempo gap only allows a short mix, so the beats never drift apart audibly", () => {
  const a = song({ bpm: 124 });
  const b = song({ bpm: 126, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assert.equal(result.bars, 2);
  assertPlayable(result.plan, a, b);
  // Bar on bar in the middle of the mix, the bars at its ends no more than 40 ms apart.
  const barsA = a.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  const barsB = b.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  const [start, end] = [result.plan.keys[0][0], result.plan.keys.at(-1)[0]];
  for (const t of [start, (start + end) / 2, end]) {
    const inB = result.plan.bStart + t - start;
    const nearest = barsB.reduce((x, y) => (Math.abs(y - inB) < Math.abs(x - inB) ? y : x));
    assert.ok(barsA.some((bar) => Math.abs(bar - t) < 0.002), `a bar of the current song at ${t}`);
    assert.ok(Math.abs(nearest - inB) <= 0.04 + 1e-9, `bars ${((nearest - inB) * 1000).toFixed(1)} ms apart at ${t}`);
  }
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

test("songs without Apple's analysis get a six-second crossfade at the very end", () => {
  const a = { duration: 180 };
  const b = { duration: 200 };
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "crossfade");
  assertPlayable(result.plan, a, b);
  assert.ok(Math.abs(result.plan.keys[1][0] - result.plan.keys[0][0] - 6) < 1e-9);
  assert.equal(result.plan.bStart, 0);
});

test("a crossfade follows the current song's own fade-out, the next song nearly full when it begins", () => {
  const a = song({ danceability: 0.2, fadeOut: { startInMilliseconds: 190000, endInMilliseconds: 198000 } });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "crossfade");
  assertPlayable(result.plan, a, b);
  assert.deepEqual(Array.from(result.plan.keys, (key) => Array.from(key)), [[189, 1, 0], [190, 1, 0.7], [198, 0, 1]]);
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
  const result = planMix(a, b, OPTIONS);
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

function follower({ state, schedule, http, clock }) {
  const context = {
    Ampify: {
      process: "test", locale: "en-US", log() {},
      config: { get: (key) => ({ style: "Auto", longest: 20, albums: true })[key] },
      storage: { get: () => ({ value: "token", at: clock?.() ?? Date.now() }), set() {}, remove() {} },
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
  assert.ok(Math.abs(sent[0].keys[0][0] - 193.75) < 1e-9);
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

test("crossfade only and off are honoured", () => {
  const a = song();
  const b = song({ attributes: { albumName: "Other" } });
  assert.equal(planMix(a, b, { ...OPTIONS, style: "Crossfade only" }).kind, "crossfade");
  assert.equal(planMix(a, b, { ...OPTIONS, style: "Off" }).kind, "none");
});
