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

// Seconds of the current song a plan leaves after its last key (main.js's endRoom): the kernel pauses its second
// player 0.3 s before the end, and a stretched song reads 0.3 s further ahead.
function roomAfter(rate) {
  return 0.4 + (rate === 1 ? 0 : 0.3);
}

// A song of `duration` seconds at `bpm`, four beats a bar from `firstBeat`, loud from `loudFrom` to `loudTo`, or
// with `level(t)` as its loudness curve (dB, one value every half second). `phraseEvery` and `phraseFrom` give it
// Apple's structure: a phrase starts on every `phraseEvery`-th bar from bar `phraseFrom`.
function song({ duration = 200, bpm = 124, firstBeat = 0, loudFrom = 0, loudTo = duration, vocals = [],
                attributes = {}, danceability = 0.8, fadeIn = null, fadeOut = null, level = null, key = null,
                melodicness = 0.7, phraseEvery = 0, phraseFrom = 0, beatAt = null, energy = null } = {}) {
  // beatAt(t, i): the length of the beat that starts at t, the i-th one, when the tempo moves along the song.
  const beats = [];
  for (let t = firstBeat, i = 0; t < duration; t += beatAt ? beatAt(t, i) : 60 / bpm, i++) beats.push(Math.round(t * 1000));
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
      ...(danceability === null ? {} : { danceability: { main: danceability, beginning: danceability, ending: danceability } }),
      ...(energy === null ? {} : { energy: { main: energy, beginning: energy, ending: energy } }),
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
  const room = roomAfter(rate);
  assert.ok(a.duration - keys.at(-1)[0] >= room, "the song goes on a little after the mix: the kernel's second player is paused");
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

// The next song is in full this long before the bar it drops on, so that its downbeat is heard whole (main.js's
// SWAP_AHEAD_S).
const SWAP_AHEAD_S = 0.02;

// The key where the next song is in full with its bass.
const landingOf = (keys) => keys.find((k) => k[2] === 1 && k[4] === 1);

// The drop of a mix on the beat, in A's time: the bar on which the next song lands in full.
function dropOf(plan) {
  return landingOf(plan.keys)[0] + SWAP_AHEAD_S * (plan.rate ?? 1);
}

// Where the next song is, in its own time, on the drop.
function bAtDrop(plan) {
  return plan.bStart + (dropOf(plan) - plan.keys[0][0]) / (plan.rate ?? 1);
}

test("two songs at one tempo mix for eight bars, the next one dropping in full on the current song's last loud bar", () => {
  const a = song({ loudTo: 190 });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assert.equal(result.bars, 8);
  assertPlayable(result.plan, a, b);
  const bars = a.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  assert.ok(bars.includes(result.plan.keys[0][0]), "the mix starts on a bar of the current song");
  const drop = dropOf(result.plan);
  assert.ok(bars.some((t) => Math.abs(t - drop) < 1e-9) && drop <= 190, `and drops on its last loud bar (${drop})`);
  assert.equal(drop, bars.filter((t) => t <= 190).at(-1));
  const bBars = b.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  assert.ok(bBars.some((t) => Math.abs(t - result.plan.bStart) < 0.002), "the next song comes in on one of its bars");
  assert.ok(result.plan.keys[0][2] === 0 && result.plan.keys[0][1] === 1, "it comes in silent");
});

test("a mix on the beat is a drop: the next song rises without its bass, then lands in full on the bar", () => {
  const a = song({ loudTo: 190 });
  const b = song({ attributes: { albumName: "Other" } });
  const { plan } = planMix(a, b, OPTIONS);
  const keys = plan.keys;
  const beat = 60 / 124;
  const ms = 0.002;  // Apple's bars are whole milliseconds
  const drop = dropOf(plan);
  const up = keys[1];
  assert.ok(up[0] - keys[0][0] <= beat + ms, "the next song is up within a beat of the mix's start");
  assert.ok(Math.abs(up[2] - 0.5) < 0.01, "at half its volume, as it plays in full in its first bars");
  assert.deepEqual([up[1], up[3], up[4]], [1, 1, 0], "without its bass, under the current song in full");
  for (const key of keys.filter((k) => k[0] < drop - beat / 2 - SWAP_AHEAD_S - ms)) {
    assert.deepEqual([key[1], key[3], key[4]], [1, 1, 0], `nothing moves for the current song before the drop (${key})`);
  }
  const landed = landingOf(keys);
  assert.deepEqual([landed[1], landed[3]], [0.7, 0], "on the drop the current song steps back, without its bass");
  const before = keys[keys.indexOf(landed) - 1];
  assert.ok(Math.abs(landed[0] - before[0] - beat / 2) < ms, "the basses change hands over half a beat");
  assert.ok(before[2] > up[2] && before[2] < 1, "the next song a little louder by then");
  const tail = keys.at(-1)[0] - drop;
  assert.ok(Math.abs(tail - 0.75 * 4 * beat) < ms, `the current song gone three quarters of a bar later (${tail} s)`);
});

test("past the drop, a current song whose beats drift from the next one's fades out sooner, a second at least", () => {
  // 100 bpm, then five percent slower from the beat after its last bar: the third beat after the drop is 30 ms late.
  const a = song({ bpm: 100, beatAt: (t) => (t < 197 ? 0.6 : 0.63) });
  const b = song({ bpm: 100, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assertPlayable(result.plan, a, b);
  const drop = dropOf(result.plan);
  assert.ok(Math.abs(drop - 196.8) < 0.002, `the drop on the current song's last bar (${drop})`);
  const tail = result.plan.keys.at(-1)[0] - drop;
  assert.ok(tail >= 1 - 1e-9 && tail < 1.2, `gone ${tail.toFixed(2)} s after the drop, not three quarters of a bar`);
  const gap = widestBeatGap(a, b, result.plan);
  assert.ok(gap <= 0.025, `beats ${(gap * 1000).toFixed(1)} ms apart at worst`);
});

test("a quiet intro of the next song rises under the current song's last bars, and drops where it is in full", () => {
  const a = song({ loudTo: 190 });
  const bar = 4 * 60 / 124;
  // Four quiet bars, then the song in full: the drop of a DJ set, on the fifth.
  const b = song({ level: (t) => (t < 4 * bar - 0.25 ? -20 : -8), attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assertPlayable(result.plan, a, b);
  const inB = bAtDrop(result.plan);
  assert.ok(Math.abs(inB - 4 * bar) < 0.002, `the next song drops on its fifth bar (${inB})`);
  assert.equal(result.bars, 4, "its four quiet bars under the current song's last four");
  assert.equal(result.plan.keys[1][2], 1, "the quiet intro at its own level");
});

test("a silent start of the next song goes under the current song, which drops it on a bar where it plays", () => {
  const a = song();
  const b = song({ loudFrom: 5, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assertPlayable(result.plan, a, b);
  const inB = bAtDrop(result.plan);
  const bBars = b.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  assert.ok(inB >= 5 && bBars.some((t) => Math.abs(t - inB) < 0.002), `B drops on a bar at ${inB}`);
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
  const { from } = entryOf(result.plan);
  const landed = result.plan.keys.find((k) => k[2] === 1);
  const swapAt = landed[0];
  const bars = a.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  // the next song is in full a few hundredths before the bar, so that its first beat is heard whole
  assert.ok(bars.some((t) => t >= swapAt && t - swapAt < 0.05), `the swap is at ${swapAt}`);
  const bTime = result.plan.bStart + (swapAt + 0.02 - result.plan.keys[0][0]);
  assert.ok(Math.abs(bTime - 2.5) < 0.05, `the next song is at ${bTime} on the bar, its first bar is at 2.5`);
  assert.equal(landed[3], 0, "the basses have changed hands by then");
  assert.equal(landed[4], 1);
  assert.equal(from[3], 1, "the current song keeps its bass while the next one's intro comes in");
  assert.equal(from[4], 0, "and that intro comes in without its own");
  const bar = 60 / 124 * 4;
  const handOver = landed[0] - result.plan.keys[result.plan.keys.indexOf(landed) - 1][0];
  assert.ok(handOver < bar / 4, `the basses change hands within half a beat (${handOver} s)`);
  // Measured on 13/11/2026: a current song gone a quarter of a bar after the swap was heard as a cut.
  assert.ok(landed[1] > 0.5, "the current song is still there when the next one lands");
  const tail = result.plan.keys.at(-1)[0] - swapAt;
  assert.ok(tail >= bar * 0.75 - 0.05 && tail <= 2.05, `then it fades out over three quarters of a bar (${tail} s)`);
  assert.ok(result.plan.keys.at(-1)[0] <= 200 - 0.4, "and the swap leaves the kernel its room");
});

test("a swap ends the current song on a phrase when one ends within its last bars", () => {
  const a = song({ bpm: 124, phraseEvery: 8, phraseFrom: 4 });
  const b = song({ bpm: 138, firstBeat: 2.5, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "swap");
  assertPlayable(result.plan, a, b);
  const landed = result.plan.keys.find((k) => k[2] === 1)[0] + 0.02;
  const bars = a.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);
  const index = bars.findIndex((t) => Math.abs(t - landed) < 0.01);
  assert.equal((index - 4) % 8, 0, `the next song lands on the start of a phrase (bar ${index})`);
  assert.ok(index < bars.length - 3, "a few bars before the last one");
});

test("a song that fades out by itself keeps the crossfade; songs with little beat still swap on a bar", () => {
  const b = song({ bpm: 138, firstBeat: 2.5, attributes: { albumName: "Other" } });
  const fading = song({ fadeOut: { startInMilliseconds: 190000, endInMilliseconds: 198000 } });
  assert.equal(planMix(fading, b, OPTIONS).kind, "crossfade");
  assert.equal(planMix(fading, b, OPTIONS).why, "this song fades out by itself", "the log says what was in the way");
  // Apple reports a short fade at the end of most songs, where the last note dies: that one is swapped before
  const dying = song({ fadeOut: { startInMilliseconds: 196000, endInMilliseconds: 198000 } });
  const swap = planMix(dying, b, OPTIONS);
  assert.equal(swap.kind, "swap");
  assert.ok(swap.plan.keys.at(-1)[0] <= 196, "the swap is over when the last note starts dying");
  // Apple finds hardly any beat in much rap (danceability 0.1 to 0.4): its bars still carry a swap.
  assert.equal(planMix(song({ danceability: 0.1 }), b, OPTIONS).kind, "swap");
  assert.equal(planMix(song(), song({ bpm: 138, firstBeat: 2.5, danceability: 0.1, attributes: { albumName: "Other" } }),
                       OPTIONS).kind, "swap");
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
  const [start, drop] = [result.plan.keys[0][0], dropOf(result.plan)];
  for (const t of [start, (start + drop) / 2, drop]) {
    assert.ok(barsA.some((bar) => Math.abs(bar - t) < 0.002), `a bar of the current song at ${t}`);
    const apart = barsApart(a, b, result.plan, t);
    assert.ok(apart <= 0.003, `bars ${(apart * 1000).toFixed(1)} ms apart at ${t}`);
  }
  const gap = widestBeatGap(a, b, result.plan);
  assert.ok(gap <= 0.025, `and the beats, the current song's fade after the drop too (${(gap * 1000).toFixed(1)} ms)`);
});

// The widest gap, in seconds as heard, between a beat of one song and the nearest beat of the other over the mix.
function widestBeatGap(a, b, plan) {
  const rate = plan.rate ?? 1;
  const start = plan.keys[0][0];
  const length = (plan.keys.at(-1)[0] - start) / rate;
  const heardA = a.analysis.beats.beatsInMilliseconds.map((ms) => (ms / 1000 - start) / rate);
  const heardB = b.analysis.beats.beatsInMilliseconds.map((ms) => ms / 1000 - plan.bStart);
  return Math.max(...heardB.filter((u) => u >= 0 && u <= length)
    .map((u) => Math.min(...heardA.map((v) => Math.abs(v - u)))));
}

test("the speed comes from the bars mixed: a next song whose tempo moves after its start still mixes beat on beat", () => {
  const a = song({ bpm: 126 });
  // 120 bpm for its first eight seconds, then 126: the tempo near its start is not the one of most of the mix.
  const b = song({ beatAt: (t) => (t < 8 ? 0.5 : 60 / 126), attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assertPlayable(result.plan, a, b);
  const gap = widestBeatGap(a, b, result.plan);
  assert.ok(gap <= 0.025, `beats ${(gap * 1000).toFixed(1)} ms apart at worst`);
});

test("a beat grid too uneven for one speed is not mixed on the beat", () => {
  const a = song({ bpm: 124 });
  // Every other bar six percent faster: no speed keeps the beats of any two bars together.
  const b = song({ beatAt: (t, i) => (Math.floor(i / 4) % 2 ? 0.47 : 0.5), attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.notEqual(result.kind, "beat");
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
  const [start, drop] = [result.plan.keys[0][0], dropOf(result.plan)];
  for (const t of [start, (start + drop) / 2, drop]) assert.ok(barsApart(a, b, result.plan, t) <= 0.02 + 1e-9);
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
  for (const t of [result.plan.keys[0][0], dropOf(result.plan)]) assert.ok(barsApart(a, b, result.plan, t) <= 0.003);
});

test("no room to reach the next song's tempo before the mix: a crossfade", () => {
  const a = song({ bpm: 124 });
  const b = song({ bpm: 128, attributes: { albumName: "Other" } });
  const result = planMix(a, b, { ...OPTIONS, earliest: 196 });
  assert.notEqual(result.kind, "beat");
});

test("the basses of the two songs change hands on the drop of a mix on the beat", () => {
  const a = song();
  const b = song({ attributes: { albumName: "Other" } });
  const { plan } = planMix(a, b, OPTIONS);
  const keys = plan.keys;
  assert.deepEqual(Array.from(keys[0].slice(3)), [1, 0], "the next song comes in without its lows");
  assert.deepEqual(Array.from(keys.at(-1).slice(3)), [0, 1], "and ends with them, the current song without");
  const drop = dropOf(plan);
  const halfBeat = 30 / 124 + 0.002;
  for (const key of keys) {
    assert.ok(key[3] + key[4] <= 1 + 1e-9, "never both basses in full");
    if (key[0] < drop - SWAP_AHEAD_S - halfBeat) assert.deepEqual(Array.from(key.slice(3)), [1, 0], "the current song's, before");
    if (key[0] >= drop - SWAP_AHEAD_S) assert.deepEqual(Array.from(key.slice(3)), [0, 1], "the next song's, from the drop on");
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

test("with Apple's structure the next song drops on one of its phrases, where a phrase of the current song ends", () => {
  const a = song({ loudTo: 190, phraseEvery: 4, phraseFrom: 1 });
  const b = song({ phraseEvery: 4, phraseFrom: 2, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "beat");
  assertPlayable(result.plan, a, b);
  const bar = 4 * 60 / 124;
  const drop = dropOf(result.plan);
  const lastBar = Math.round(drop / bar);
  assert.equal((lastBar - 1) % 4, 0, `the drop on bar ${lastBar}, a phrase of the current song`);
  assert.ok(drop <= 190);
  const inB = Math.round(bAtDrop(result.plan) / bar);
  assert.equal((inB - 2) % 4, 0, `and on bar ${inB} of the next song, one of its phrases`);
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

test("songs Apple finds hardly danceable, much rap among them, still mix on the beat, and so do songs it gives no score", () => {
  const b = song({ attributes: { albumName: "Other" }, danceability: 0.1 });
  assert.equal(planMix(song({ danceability: 0.1 }), b, OPTIONS).kind, "beat");
  assert.equal(planMix(song({ danceability: 0.1, energy: 0.6 }), b, OPTIONS).kind, "beat", "with energy, not soft");
  const unscored = song({ danceability: null, attributes: { albumName: "Other" } });
  assert.equal(planMix(song({ danceability: null }), unscored, OPTIONS).kind, "beat");
});

test("a soft ending, hardly danceable and quiet, is left to breathe, or fades as Apple's player would", () => {
  const b = song({ attributes: { albumName: "Other" } });
  // A ballad that ends on its last note at 195 s, then silence: the next song after a breath, even one that starts
  // strong and shares the tempo.
  const ballad = song({ danceability: 0.1, energy: 0.1, loudTo: 195 });
  const breath = planMix(ballad, b, CAN);
  assert.equal(breath.kind, "natural");
  assert.ok(breath.soft);
  assertKernelTakes(breath.plan, ballad, b);
  assert.ok(Math.abs(breath.plan.keys[0][0] - 195.9) < 1e-9, `to its end, 0.6 s of its own silence (${breath.plan.keys[0][0]})`);
  // One that plays to the last second has no silence to breathe in: a crossfade, never a drop.
  const fade = planMix(song({ danceability: 0.1, energy: 0.1 }), b, CAN);
  assert.equal(fade.kind, "crossfade");
  assert.equal(fade.why, "this song ends softly");
  assert.equal(planMix(ballad, b, OPTIONS).kind, "crossfade", "an older kernel cannot cut: a crossfade");
});

test("a crossfade follows the current song's own fade-out, the next song nearly full when it begins", () => {
  const a = song({ fadeOut: { startInMilliseconds: 190000, endInMilliseconds: 198000 } });
  const b = song({ bpm: 138, firstBeat: 2.5, attributes: { albumName: "Other" } });
  const result = planMix(a, b, OPTIONS);
  assert.equal(result.kind, "crossfade");
  assertPlayable(result.plan, a, b);
  // the song lasts 200 s: the mix ends with its fade-out, at 198 s
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

function follower({ state, schedule, http, clock, stored = {}, config = {}, preferStereo, logs = [] }) {
  const kept = { token: { value: "token", at: clock?.() ?? Date.now() }, ...stored };
  const settings = { style: "Auto", longest: 20, albums: true, stereo: false, ...config };
  const context = {
    Ampify: {
      process: "test", locale: "en-US", log: (text) => logs.push(text),
      config: { get: (key) => settings[key] },
      storage: {
        get: (key, fallback) => (key in kept ? kept[key] : fallback),
        set: (key, value) => { kept[key] = stored[key] = value; },
        remove: (key) => { delete kept[key]; },
      },
      http: { get: http ?? (async () => ({ status: 404, text: "" })) },
      automix: { state, schedule, cancel() {}, ...(preferStereo ? { preferStereo } : {}) },
    },
    setInterval() {}, clearInterval() {},
  };
  if (clock) context.Date = class extends Date { static now() { return clock(); } };
  vm.runInNewContext(source, context, { filename: "main.js" });
  context.settings = settings;
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
  assert.ok(Math.abs(sent[0].keys[0][0] - 193.6) < 1e-9);  // six seconds, over 0.4 s before the end of 200 s
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

test("songs with a Dolby Atmos version are asked for in stereo only with the setting and AutoMix on", async () => {
  const asked = [];
  const logs = [];
  const preferStereo = (want) => asked.push(want);
  const context = follower({ state: () => queue(100), schedule: () => ({ ok: true }), preferStereo, logs });
  await looks(context, 1);
  context.settings.stereo = true;
  await looks(context, 2);
  context.settings.style = "Off";
  await looks(context, 1);
  assert.deepEqual(asked, [false, true, true, false], "at every look: the kernel acts on a stereo song once one plays");
  assert.deepEqual(logs.filter((text) => /stereo|Dolby Atmos left/.test(text)), [
    "Songs with a Dolby Atmos version are asked for in stereo, to mix them",
    "Dolby Atmos left to Apple Music's choice",
  ]);
});

test("nothing is learnt of Dolby Atmos while the songs are asked for in stereo", async () => {
  const stored = {};
  const catalog = async () => ({ status: 200, text: JSON.stringify({ data: [{ attributes: { audioTraits: ["atmos"] } }] }) });
  const state = () => ({ ...queue(160), current: { ...queue(160).current, atmos: false } });
  const context = follower({ state, schedule: () => ({ ok: true }), http: catalog, stored, preferStereo() {},
    config: { stereo: true } });
  await looks(context, 4);
  assert.equal(stored.atmosPlays, undefined, "in stereo because it was asked for, not because Dolby Atmos is off");
});

test("an Ampify without preferStereo leaves Dolby Atmos alone and still mixes", async () => {
  const sent = [];
  const context = follower({ state: () => queue(160), schedule: (plan) => (sent.push(plan), { ok: true }) });
  await looks(context, 3);
  assert.equal(sent.length, 1);
});

test("crossfade only and off are honoured", () => {
  const a = song();
  const b = song({ attributes: { albumName: "Other" } });
  assert.equal(planMix(a, b, { ...OPTIONS, style: "Crossfade only" }).kind, "crossfade");
  assert.equal(planMix(a, b, { ...OPTIONS, style: "Off" }).kind, "none");
});

// ---- with a kernel that can cut, filter, echo and loop (Ampify.automix.state().features)

const CAN = { ...OPTIONS, can: new Set(["cut", "filter", "echo", "loop"]) };

// Check of src/core/automix/mix_plan.cpp, cuts, filters, echoes and loops included.
function assertKernelTakes(plan, a, b) {
  const keys = plan.keys;
  const [first, last] = [keys[0], keys.at(-1)];
  assert.ok(keys.length >= 2 && keys.length <= 64);
  for (let i = 1; i < keys.length; i++) assert.ok(keys[i][0] >= keys[i - 1][0], "keys in time order");
  for (const key of keys) {
    assert.ok([3, 5, 7, 9].includes(key.length), `a key of ${key.length} numbers`);
    for (const value of key.slice(1)) assert.ok(value >= 0 && value <= 1, "volumes, lows and filters within 0..1");
  }
  assert.deepEqual([first[1], first[2]], [1, 0]);
  assert.deepEqual([last[1], last[2]], [0, 1]);
  assert.equal(last[4] ?? 1, 1, "B has its lows at the end");
  assert.equal(last[6] ?? 0, 0, "and is no longer filtered");
  assert.equal(last[8] ?? 0, 0, "from below either");
  const cut = keys.length === 2 && first[0] === last[0];
  const rate = plan.rate ?? 1;
  if (cut) {
    assert.ok(rate === 1 && !plan.echo && !plan.loop, "a cut has no deck");
    assert.ok(first[0] < a.duration);
  } else {
    assert.ok(last[0] > first[0] && last[0] - first[0] <= 60);
  }
  const room = roomAfter(rate);
  if (plan.loop) {
    assert.ok(a.duration - first[0] >= room, "the mix starts with the kernel's room after it");
    assert.ok(plan.loop.from >= first[0] && plan.loop.from < last[0], "the loop starts within the mix");
    assert.ok(plan.loop.length >= 0.2 && plan.loop.length <= 8);
    assert.ok(plan.loop.from + plan.loop.length + 1 <= a.duration, "and ends a second before the song does");
    let [at, length] = [plan.loop.from, plan.loop.length];
    for (const [rollAt, rollLength] of plan.loop.rolls ?? []) {
      assert.ok(rollLength >= 0.03 && rollLength < length, "each roll shorter than the one before");
      const turns = (rollAt - at) / length;
      assert.ok(turns >= 0.998 && Math.abs(turns - Math.round(turns)) * length <= 0.002, "on a turn of the one before");
      assert.ok(rollAt < last[0], "within the mix");
      [at, length] = [rollAt, rollLength];
    }
  } else if (!cut) {
    assert.ok(a.duration - last[0] >= room, "the song goes on after the mix");
  }
  if (plan.echo) {
    const { from, to, delay, feedback, level = 0.7 } = plan.echo;
    assert.ok(from >= first[0] && from < to && to <= last[0], "the echo is thrown within the mix");
    assert.ok(delay >= 0.05 && delay <= 2 && feedback >= 0 && feedback <= 0.9);
    assert.ok(level > 0 && level <= 2, "an echo level the kernel takes");
  }
  if (plan.gate) {
    const { beats, on, duck, until } = plan.gate;
    assert.ok(beats.length >= 1 && beats.length <= 128, "stabs the kernel takes");
    for (let i = 1; i < beats.length; i++) assert.ok(beats[i] > beats[i - 1], "stabs in time order");
    assert.ok(beats[0] >= first[0] && beats.at(-1) < last[0], "stabs within the mix");
    assert.ok(on >= 0.02 && on <= 2 && duck >= 0 && duck <= 1);
    assert.ok(until > beats[0] && until <= last[0], "the gate ends within the mix");
  }
  assert.ok(plan.bStart >= 0 && plan.bStart + (last[0] - first[0]) / rate < b.duration);
}

const barTimes = (s) => s.analysis.beats.barsInMilliseconds.map((ms) => ms / 1000);

test("a kernel that filters gets mixes on the beat where the next song comes in muffled and the current one leaves thin", () => {
  const a = song({ loudTo: 190 });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, CAN);
  assert.equal(result.kind, "beat");
  assertKernelTakes(result.plan, a, b);
  const keys = result.plan.keys;
  assert.deepEqual([keys[0][5], keys[0][6]], [0, 0.5], "the next song muffled as it comes in");
  assert.deepEqual([keys.at(-1)[5], keys.at(-1)[6]], [0.6, 0], "the current one thin as it goes");
  const swap = keys.find((key) => key[4] === 1);
  assert.equal(swap[6], 0, "the next song is open once it has the bass");
  assert.ok(keys[keys.indexOf(swap) - 1][6] < keys[1][6], "and opens up as the drop nears");
  // An older kernel gets the very same mix, unfiltered.
  const old = planMix(a, b, OPTIONS);
  assert.deepEqual(Array.from(old.plan.keys, (key) => Array.from(key)), Array.from(keys, (key) => Array.from(key.slice(0, 5))));
});

test("songs too far apart in tempo leave on an echo of the current song's last beat, the next landing on the bar", () => {
  const a = song({ bpm: 124 });
  const b = song({ bpm: 138, firstBeat: 2.5, attributes: { albumName: "Other" } });
  const result = planMix(a, b, CAN);
  assert.equal(result.kind, "echo");
  assertKernelTakes(result.plan, a, b);
  const { keys, echo, bStart } = result.plan;
  const swapAt = keys.at(-1)[0] + 0.02;
  assert.ok(barTimes(a).some((t) => Math.abs(t - swapAt) < 1e-6), `the current song stops on its bar (${swapAt})`);
  assert.ok(Math.abs(bStart + swapAt - keys[0][0] - 2.5) < 1e-6, "where the next song's first bar lands");
  assert.ok(Math.abs(echo.delay - 60 / 124) < 1e-3, "the repeats a beat of the current song apart");
  assert.ok(Math.abs(echo.from - (swapAt - echo.delay)) < 1e-9 && echo.to === keys.at(-1)[0],
            "its last beat thrown, the first repeat on the bar");
  assert.equal(echo.feedback, 0.5);
  assert.equal(keys.at(-1)[5], 0.6, "the current song thins out over the bar it leaves on");
  assert.deepEqual([keys.at(-1)[3], keys.at(-1)[4]], [0, 1], "the next song lands with its bass");
  assert.equal(planMix(a, b, OPTIONS).kind, "swap", "an older kernel keeps the swap on a bar");
});

test("a bar given several times in Apple's analysis counts once, and its echo is planned at once", () => {
  // Bars that did not move on made a bar of no length, and an echo delay doubled for ever: the timeout catches that.
  const a = song({ bpm: 124 });
  const repeated = song({ bpm: 124 });
  repeated.analysis.beats.barsInMilliseconds = repeated.analysis.beats.barsInMilliseconds.flatMap((ms) => [ms, ms, ms]);
  const b = song({ bpm: 138, firstBeat: 2.5, attributes: { albumName: "Other" } });
  const context = Object.assign(load(), { a, repeated, b, options: CAN });
  const [clean, plan] = vm.runInContext("[planMix(a, b, options), planMix(repeated, b, options)]", context,
                                        { timeout: 2000 });
  assert.equal(clean.kind, "echo");
  assert.deepEqual(plan, clean);
});

test("a next song that starts right on its first bar takes the current song's next downbeat into the echo", () => {
  const a = song({ bpm: 124 });
  const b = song({ bpm: 138, attributes: { albumName: "Other" } });
  const result = planMix(a, b, CAN);
  assert.equal(result.kind, "echo");
  assertKernelTakes(result.plan, a, b);
  const { keys, echo, bStart } = result.plan;
  assert.equal(bStart, 0);
  assert.ok(barTimes(a).includes(keys[0][0]), "the next song starts on a bar of the current one");
  assert.ok(keys.at(-1)[0] - keys[0][0] < 0.3, "which is gone half a beat later");
  assert.equal(echo.from, keys[0][0], "its downbeat going into the echo");
});

test("a song that stops on a hit is heard to its end, then a next song that starts strong comes in at once", () => {
  // Another tempo: no mix on the beat goes over the hit.
  const a = song({ loudTo: 195 });
  const b = song({ bpm: 138, loudFrom: 1, attributes: { albumName: "Other" } });
  const result = planMix(a, b, CAN);
  assert.equal(result.kind, "natural");
  assertKernelTakes(result.plan, a, b);
  // the hit rings 0.3 s past where the loudness drops, then 0.12 s of silence, the next song's own
  assert.deepEqual(Array.from(result.plan.keys, (key) => Array.from(key)), [[195.3, 1, 0], [195.3, 0, 1]]);
  assert.ok(Math.abs(result.plan.bStart - 0.88) < 1e-9);
  assert.equal(planMix(a, b, OPTIONS).kind, "swap", "an older kernel cannot cut: a swap on a bar");
});

test("after a hit, a next song that starts softly gets a breath; a short fade gets a mix", () => {
  const soft = song({ bpm: 138, loudFrom: 2, fadeIn: { startInMilliseconds: 2000, endInMilliseconds: 5000 },
                      attributes: { albumName: "Other" } });
  const hit = planMix(song({ loudTo: 195 }), soft, CAN);
  assert.equal(hit.kind, "natural");
  assert.ok(Math.abs(hit.plan.keys[0][0] - 195.3) < 1e-9 && Math.abs(hit.plan.bStart - 1.4) < 1e-9,
            "0.6 s before a soft start, of its own silence");
  // Most songs end on a fade of a few seconds (Apple's French chart, 18/11/2026): a pause after each would sound
  // like no AutoMix at all.
  const dying = { loudTo: 196.5, fadeOut: { startInMilliseconds: 194000, endInMilliseconds: 196500 } };
  assert.equal(planMix(song(dying), song({ attributes: { albumName: "Other" } }), CAN).kind, "beat");
});

// Heard on 24/11 (s2-7): a soft end, then a next song that starts loud, came in 19 dB above it, all at once.
test("after a soft end, a next song much louder than it comes in muffled and rises, the old song silent by then", () => {
  const quietEnd = song({ danceability: 0.1, energy: 0.1, level: (t) => (t < 185 ? -8 : t < 195 ? -17 : -60) });
  const loud = song({ attributes: { albumName: "Other" } });
  const result = planMix(quietEnd, loud, CAN);
  assert.equal(result.kind, "natural");
  assertKernelTakes(result.plan, quietEnd, loud);
  const { keys } = result.plan;
  assert.ok(keys.length > 2 && keys.at(-1)[0] > keys[0][0], "a fade, not a cut");
  assert.ok(keys[0][0] > 195, "after the song's last sound");
  assert.ok(keys[1][1] === 0 && keys[1][2] > 0 && keys[1][2] < 0.5 && keys[1][6] > 0, "the next song low and muffled");
  assert.ok(Math.abs(keys.at(-1)[0] - keys[0][0] - 1.5) < 1e-9, "in full a second and a half later");
  // A next song no louder than the end it follows still comes in at once.
  const even = planMix(song({ danceability: 0.1, energy: 0.1, loudTo: 195 }), loud, CAN);
  assert.deepEqual(Array.from(even.plan.keys, (key) => key[0]), [even.plan.keys[0][0], even.plan.keys[0][0]]);
});

// Heard on 24/11 (fondu-2): a song fading out, then one whose drop comes after seven quiet seconds: four seconds of
// near silence between them.
test("a crossfade into a quiet intro lands the next song's drop where the current song has faded out", () => {
  const a = song({ fadeOut: { startInMilliseconds: 190000, endInMilliseconds: 198000 },
                   level: (t) => (t < 190 ? -8 : t < 193 ? -14 : t < 198 ? -24 : -60) });
  const b = song({ bpm: 138, firstBeat: 2.5, level: (t) => (t < 9 ? -24 : -8), attributes: { albumName: "Other" } });
  const result = planMix(a, b, CAN);
  assert.equal(result.kind, "crossfade");
  assertKernelTakes(result.plan, a, b);
  const { keys, bStart } = result.plan;
  const dropAt = keys[0][0] + (9 - bStart);
  assert.ok(Math.abs(dropAt - 193) < 0.6, `the drop lands where the current song falls away (${dropAt})`);
  // The current song's own fade is 6 dB down before the drop; the next song's intro started at once left 16 dB.
  const hole = deepestHole(a, b, result.plan);
  const before = deepestHole(a, b, { ...result.plan, bStart: 0 });
  assert.ok(hole < 8 && before > 12, `no hole between them (${hole.toFixed(1)} dB, ${before.toFixed(1)} before)`);
});

test("a beat song that fades within a few seconds leaves on an echo, before its fade, into a song of another tempo", () => {
  const ending = { bpm: 124, loudTo: 196.5, fadeOut: { startInMilliseconds: 194000, endInMilliseconds: 196500 } };
  const b = song({ bpm: 138, firstBeat: 2.5, attributes: { albumName: "Other" } });
  const echo = planMix(song(ending), b, CAN);
  assert.equal(echo.kind, "echo");
  assert.ok(echo.plan.keys.at(-1)[0] <= 194, "the song leaves before its fade begins");
  const soft = song({ bpm: 138, firstBeat: 2.5, loudFrom: 2, fadeIn: { startInMilliseconds: 2000, endInMilliseconds: 5000 },
                      attributes: { albumName: "Other" } });
  assert.equal(planMix(song(ending), soft, CAN).kind, "echo", "into a soft start too: it comes in on its first bar");
  assert.equal(planMix(song({ bpm: 124, loudTo: 195 }), b, CAN).kind, "natural", "a hit is left to end the song");
});

test("a song that fades out by itself keeps its crossfade, filtered", () => {
  const a = song({ fadeOut: { startInMilliseconds: 190000, endInMilliseconds: 198000 } });
  const b = song({ bpm: 138, firstBeat: 2.5, attributes: { albumName: "Other" } });
  const result = planMix(a, b, CAN);
  assert.equal(result.kind, "crossfade");
  assertKernelTakes(result.plan, a, b);
  assert.deepEqual(Array.from(result.plan.keys, (key) => Array.from(key)),
                   [[189, 1, 0, 1, 1, 0, 0.4], [190, 1, 0.7, 1, 1, 0.2, 0], [198, 0, 1, 1, 1, 0.4, 0]]);
  assert.equal(planMix(a, b, { ...CAN, style: "Crossfade only" }).kind, "crossfade");
});

test("a song that stops on a hit, sung to its last bars, loops a bar without its singer while the next comes in", () => {
  const a = song({ vocals: [[100, 195]] });
  const b = song({ vocals: [[0, 60]], attributes: { albumName: "Other" } });
  assert.equal(planMix(a, b, OPTIONS).bars, 2, "two singers: without a loop, the shortest mix");
  const after = planMix(song({ vocals: [[100, 191]] }), b, OPTIONS);
  assert.ok(after.bars === 4 && !after.sings, "a singer done four bars before the end: the whole mix after it");
  const result = planMix(a, b, CAN);
  assert.equal(result.kind, "loop");
  assert.equal(result.bars, 4);
  assertKernelTakes(result.plan, a, b);
  const { loop, keys } = result.plan;
  const bars = barTimes(a);
  const at = bars.indexOf(loop.from);
  assert.ok(at > 0 && loop.from >= 195, `a bar of the current song after its singer (${loop.from})`);
  assert.ok(Math.abs(loop.length - (bars[at + 1] - bars[at])) < 1e-9, "one bar long");
  assert.equal(keys[0][0], loop.from, "the mix starts with the loop");
  assert.ok(keys.at(-1)[0] > loop.from + 3 * loop.length, "and goes on over its turns");
  for (let turn = 0; turn <= 4; turn++) {
    const apart = barsApart(a, b, result.plan, loop.from + turn * loop.length);
    assert.ok(apart <= 0.003, `the next song on a bar at each turn (${(apart * 1000).toFixed(1)} ms)`);
  }
  assert.equal(planMix(song(), song({ attributes: { albumName: "Other" } }), CAN).kind, "beat",
               "without a singer in the way, the mix on the beat as it was");
});

// ---- with a kernel that also has the crossover: A's low-pass and B's high-pass (added after Ampify 1.5.7)

const CROSS = { ...OPTIONS, can: new Set(["cut", "filter", "echo", "loop", "crossover"]) };
// The kernel's ranges (mix_deck.cpp): B's high-pass from 2 Hz to 16 kHz, A's low-pass from 20 kHz down to 30 Hz.
const thinHz = (amount) => 2 * (16000 / 2) ** amount;
const muffleHz = (amount) => 20000 * (30 / 20000) ** amount;
// Where the next song is, in its own time, when a crossover is over: its drop, a hair after the last key.
const bAtDropOf = (plan) => plan.bStart + (plan.keys.at(-1)[0] - plan.keys[0][0]) / (plan.rate ?? 1) + SWAP_AHEAD_S;

test("with the crossover, a mix on the beat keeps both songs at full volume and hands one to the other from the top down", () => {
  const a = song({ loudTo: 190 });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, CROSS);
  assert.equal(result.kind, "beat");
  assert.equal(result.style, "crossover");
  assertKernelTakes(result.plan, a, b);
  const keys = result.plan.keys;
  assert.ok(keys.every((k) => k.length === 9), "keys with the crossover's two numbers");
  const beat = 60 / 124;
  const [first, last] = [keys[0], keys.at(-1)];
  assert.equal(first[8], 1, "the next song comes in with only its very top");
  const body = keys.slice(1, -1);
  // At full volume within half a beat, its highs were heard as the new song coming in too soon (24/11, n-3).
  assert.ok(Math.abs(body[0][0] - first[0] - 2 * beat) <= 0.004, "rising to full volume over two beats");
  for (const key of body) {
    assert.deepEqual(Array.from(key.slice(1, 5)), [1, 1, 1, 1], `both whole and at full volume (${key})`);
    assert.ok(Math.abs(Math.log(muffleHz(key[7]) / thinHz(key[8]))) < 0.01, `one cutoff for both (${key})`);
  }
  for (let i = 1; i < body.length; i++) {
    assert.ok(body[i][8] <= body[i - 1][8] && body[i][7] >= body[i - 1][7], "the cutoff only comes down");
  }
  assert.ok(thinHz(body.at(-1)[8]) <= 160, "down to the bass by the end");
  const late = body.find((k) => k[0] >= first[0] + (last[0] - first[0]) * 0.75);
  assert.ok(thinHz(late[8]) > 4000, "three quarters in, the next song still gives only its highs");
  assert.deepEqual([last[1], last[2], last[8]], [0, 1, 0], "on the drop the current song is gone, the next one whole");
  assert.ok(last[0] - body.at(-1)[0] <= 0.3 + 1e-9, "the current song cut within 0.3 s");
  const drop = last[0] + SWAP_AHEAD_S;
  assert.ok(barTimes(a).some((t) => Math.abs(t - drop) < 1e-6), `the drop on a bar of the current song (${drop})`);
  assert.ok(deepestHole(a, b, result.plan) <= 0.5, "no hole: both play in full");
});

test("a crossover lands where the two singers do not sing together as it hands the mids over", () => {
  // The current song is sung to its end; the next one's first line runs from 10 to 16 s. Eight bars would land on
  // its bar 8 (15.5 s), over that line; four bars land on its bar 4 (7.7 s), before it.
  const a = song({ loudTo: 190, vocals: [[150, 200]] });
  const b = song({ vocals: [[10, 16]], attributes: { albumName: "Other" } });
  assert.equal(planMix(a, song({ attributes: { albumName: "Other" } }), CROSS).bars, 8, "without the line, eight bars");
  const result = planMix(a, b, CROSS);
  assert.equal(result.style, "crossover");
  assert.equal(result.bars, 4);
  assert.ok(bAtDropOf(result.plan) < 10, `the next song whole before its singer (${bAtDropOf(result.plan)})`);
  assert.equal(planMix(a, b, CAN).bars, 8, "the drop of a kernel without the crossover is left as it was");
});

test("a song that stops on a hit loops a bar under the crossover as well", () => {
  const a = song({ vocals: [[100, 195]] });
  const b = song({ vocals: [[0, 60]], attributes: { albumName: "Other" } });
  const result = planMix(a, b, CROSS);
  assert.equal(result.kind, "loop");
  assert.equal(result.style, "crossover");
  assertKernelTakes(result.plan, a, b);
  assert.ok(result.plan.keys.every((k) => k.length === 9), "the crossover's keys");
  assert.ok(result.plan.keys.at(-1)[0] > result.plan.loop.from + 3 * result.plan.loop.length, "over the loop's turns");
});

test("without the crossover the kernel gets the drop it always took", () => {
  const a = song({ loudTo: 190 });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, CAN);
  assert.notEqual(result.style, "crossover");
  assert.ok(result.plan.keys.every((k) => k.length === 7));
});

test("a long crossover at a fast tempo stays within the kernel's 64 keys", () => {
  // The next song's music is in full from 40 s: sixteen bars of it come before its drop.
  const a = song({ bpm: 174, loudTo: 190 });
  const b = song({ bpm: 174, loudFrom: 40, attributes: { albumName: "Other" } });
  const result = planMix(a, b, { ...CROSS, style: "Long blends", maxSeconds: 40 });
  assert.equal(result.style, "crossover");
  assert.equal(result.bars, 16);
  assertKernelTakes(result.plan, a, b);
});

// ---- with a kernel that also has the gate and the roll (after Ampify 1.5.9): a style drawn for each pair

const STYLED = { ...OPTIONS, can: new Set(["cut", "filter", "echo", "loop", "crossover", "gate", "roll"]) };
const BAR = 60 / 124 * 4;
const beatTimes = (s) => s.analysis.beats.beatsInMilliseconds.map((ms) => ms / 1000);
// The kernel's low-pass of the next song (bFilter): from 20 kHz at 0 down to 400 Hz at 1.
const lowPassHz = (amount) => 20000 * (400 / 20000) ** amount;

test("in stabs, the next song comes in on its beats over the current song, without its bass, then drops in full", () => {
  const a = song({ loudTo: 190 });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, { ...STYLED, prefer: "stabs" });
  assert.equal(result.kind, "beat");
  assert.equal(result.style, "stabs");
  assertKernelTakes(result.plan, a, b);
  const { keys, gate, bStart } = result.plan;
  const drop = gate.until + SWAP_AHEAD_S;
  assert.ok(barTimes(a).some((t) => Math.abs(t - drop) < 1e-6), "the drop on a bar of the current song");
  for (const stab of gate.beats) {
    const inB = bStart + (stab - keys[0][0]);
    assert.ok(beatTimes(b).some((t) => Math.abs(t - inB) < 0.002), "each stab on a beat of the next song");
  }
  assert.equal(gate.beats.length, Math.round((drop - keys[0][0]) / (BAR / 4)), "one on every beat of the build");
  assert.ok(Math.abs(gate.on - BAR / 8) < 1e-3, "half a beat each");
  assert.equal(gate.duck, 0.3);
  assert.equal(keys[1][2], 1, "at full level: the gate chops it");
  assert.ok(Math.abs(lowPassHz(keys[0][6]) - 1000) < 1, "muffled from 1 kHz...");
  const before = keys.filter((k) => k[0] < gate.until);
  assert.ok(before.every((k, i) => i === 0 || k[6] <= before[i - 1][6]), "...opening up to the drop");
  assert.ok(before.every((k) => k[1] === 1), "the current song at full volume until the drop");
  assert.ok(before.every((k) => k[4] === 0), "the next song without its bass until the drop");
  const bassCut = keys.find((k) => k[3] === 0);
  assert.ok(barTimes(a).some((t) => Math.abs(t - bassCut[0] - SWAP_AHEAD_S) < 2e-3), "the current song's bass goes on a bar");
  assert.ok(drop - bassCut[0] - SWAP_AHEAD_S >= BAR - 1e-6, "a bar or more before the drop: a bridge without bass");
  const after = keys.filter((k) => k[0] >= gate.until);
  assert.ok(after.every((k) => k[2] === 1 && k[4] === 1), "the next song whole from the drop, bass and all");
  assert.ok(keys.at(-1)[0] - gate.until <= BAR + 2e-3, "the current song gone within a bar");
  const half = after.find((k) => k[0] >= gate.until + BAR / 2 - 1e-6);
  assert.ok(half[1] < 0.2, "mostly gone within half of it");
});

test("a drop without the stabs brings the next song up to 0.8 under the current one, bass gone a bar before", () => {
  const a = song({ loudTo: 190 });
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, { ...STYLED, prefer: "drop" });
  assert.equal(result.style, "drop");
  assertKernelTakes(result.plan, a, b);
  const { keys } = result.plan;
  assert.equal(result.plan.gate, undefined);
  const bassCut = keys.findIndex((k) => k[3] === 0);
  assert.ok(Math.abs(keys[bassCut][2] - 0.8) < 1e-9, "the next song at 0.8 where the bass goes");
  assert.ok(keys.slice(1, bassCut).every((k) => k[2] > 0 && k[2] <= 0.8 + 1e-9), "rising to it");
  assert.ok(deepestHole(a, b, result.plan) <= 0.5, "no hole");
});

test("a roll plays the current song's last bar faster and faster, then the next song lands on the bar, at any tempo", () => {
  const a = song();
  const b = song({ bpm: 96, attributes: { albumName: "Other" } });  // too far from 124 to share a tempo
  const result = planMix(a, b, { ...STYLED, prefer: "roll" });
  assert.equal(result.kind, "roll");
  assertKernelTakes(result.plan, a, b);
  const { keys, loop, bStart } = result.plan;
  const beat = BAR / 4;
  const drop = keys.at(-1)[0] + SWAP_AHEAD_S;
  assert.ok(barTimes(a).some((t) => Math.abs(t - drop) < 1e-6), "the next song lands on a bar of the current one");
  assert.ok(Math.abs(loop.from - (drop - BAR)) < 1e-3, "the bar rolled is the one before");
  assert.ok(Math.abs(loop.length - beat) < 1e-3, "its first beat twice...");
  const rolls = loop.rolls.map(([at, length]) => [at - loop.from, length]);
  assert.ok(Math.abs(rolls[0][0] - BAR / 2) < 1e-3 && Math.abs(rolls[0][1] - beat / 2) < 1e-3, "...then half beats");
  assert.ok(Math.abs(rolls[1][0] - BAR * 3 / 4) < 1e-3 && Math.abs(rolls[1][1] - beat / 4) < 1e-3, "...then quarters");
  // "A bit of the next song before the transition" (24/11, n3-1): it creeps in with the half beats, thin, without its
  // bass and at half volume at most, then drops in full.
  const before = keys.slice(0, -1);
  assert.ok(before.filter((k) => k[0] <= loop.rolls[0][0] + 1e-9).every((k) => k[2] === 0), "unheard before the half beats");
  assert.ok(before.at(-1)[2] > 0 && before.every((k) => k[2] <= 0.5), "heard a little before the drop, half at most");
  assert.ok(before.filter((k) => k[2] > 0).every((k) => k[4] === 0 && k[8] >= 0.5), "thin and without its bass then");
  const quarters = loop.rolls[1][0];
  // Thinned, its bass gone, a roll fell 12 to 18 dB before the drop (24/11, s2-10 and n-1): it darkens instead, its
  // bass kept, the next song in whole only on the drop.
  assert.ok(keys.every((k) => k[3] === 1 && k[5] === 0), "its bass to the drop, never thinned");
  const dark = keys.map((k) => k[7]);
  assert.ok(dark.every((x, i) => i === 0 || x >= dark[i - 1]), "darkening from the loop's start, never back");
  const mid = keys.find((k) => k[0] >= quarters)[7];
  assert.ok(dark[0] === 0 && mid > 0 && mid < keys.at(-2)[7] && keys.at(-2)[7] <= 0.5, "to about 1 kHz by the drop");
  // A kernel without the crossover's filters thins it out, its bass gone with the quarter beats only.
  const older = planMix(a, b, { ...STYLED, can: new Set([...STYLED.can].filter((x) => x !== "crossover")), prefer: "roll" });
  assert.equal(older.kind, "roll");
  assertKernelTakes(older.plan, a, b);
  const bassAt = (time) => older.plan.keys.findLast((k) => k[0] <= time)[3];
  assert.ok(bassAt(quarters - 0.01) === 1 && bassAt(quarters + 0.05) === 0, "its bass gone with the quarter beats");
  const entry = bStart + (drop - keys[0][0]);
  assert.ok(barTimes(b).some((t) => Math.abs(t - entry) < 1e-3), "the next song in on a bar of its own");
  assert.ok(bStart >= 0, "played unheard under the roll from before it");
  // The cut was heard as too raw (24/11): the last beat of quarters rings on, thinner, under the next song's drop.
  const { echo } = result.plan;
  assert.ok(echo, "an echo of the roll's end");
  // Apple's bars are whole milliseconds: a beat within one of BAR / 4.
  assert.ok(Math.abs(echo.to - keys.at(-1)[0]) < 1e-9 && Math.abs(echo.from - (echo.to - beat)) < 1e-3, "its last beat");
  assert.ok(Math.abs(echo.delay - beat) < 1e-3 && echo.feedback >= 0.35 && echo.feedback <= 0.5, "a beat apart, fading");
});

test("a slow song rolls the second half of its bar, on half beats", () => {
  const a = song({ bpm: 60 });  // a beat of a second: a whole bar rolled lasted four seconds (Capuche, 24/11)
  const b = song({ bpm: 96, attributes: { albumName: "Other" } });
  const result = planMix(a, b, { ...STYLED, prefer: "roll" });
  assert.equal(result.kind, "roll");
  assertKernelTakes(result.plan, a, b);
  const { keys, loop } = result.plan;
  const drop = keys.at(-1)[0] + SWAP_AHEAD_S;
  assert.ok(Math.abs(loop.length - 0.5) < 1e-6, "half a beat, again");
  assert.ok(Math.abs(loop.from - (drop - 2)) < 1e-6, "from the bar's third beat");
  const rolls = loop.rolls.map(([at, length]) => [at - loop.from, length]);
  assert.ok(Math.abs(rolls[0][0] - 1) < 1e-6 && Math.abs(rolls[0][1] - 0.25) < 1e-6, "then quarter beats");
  assert.ok(Math.abs(rolls[1][0] - 1.5) < 1e-6 && Math.abs(rolls[1][1] - 0.125) < 1e-6, "then eighths");
});

test("a next song sung over its build is not chopped into stabs, nor a sung beat rolled", () => {
  const a = song({ loudTo: 190 });
  const sung = song({ vocals: [[0, 60]], attributes: { albumName: "Other" } });
  assert.notEqual(planMix(a, sung, { ...STYLED, prefer: "stabs" }).style, "stabs");
  const singing = song({ vocals: [[150, 200]] });
  const other = song({ bpm: 96, attributes: { albumName: "Other" } });
  assert.notEqual(planMix(singing, other, { ...STYLED, prefer: "roll" }).kind, "roll");
  const fast = song({ bpm: 320 });
  const fastPlan = planMix(fast, other, { ...STYLED, prefer: "roll" });
  assert.notEqual(fastPlan.kind, "roll", "a beat shorter than the kernel's shortest loop is not rolled");
  assert.equal(planMix(song(), other, { ...STYLED, prefer: "roll" }).kind, "roll", "unsung, it rolls");
});

test("a sung or fading last bar rolls the latest bar up to eight earlier that ends a phrase, never cutting a singer", () => {
  const other = song({ bpm: 96, attributes: { albumName: "Other" } });
  const bars = barTimes(song());
  const rolledAt = (a) => {
    const result = planMix(a, other, { ...STYLED, prefer: "roll" });
    assert.equal(result.kind, "roll");
    assertKernelTakes(result.plan, a, other);
    const drop = result.plan.keys.at(-1)[0] + SWAP_AHEAD_S;
    return bars.findIndex((t) => Math.abs(t - drop) < 1e-6);
  };
  // Phrases every four bars: the last one starts on bar 100 (193.5 s); the one before on bar 96 (185.8 s).
  assert.equal(rolledAt(song({ level: (t) => (t < 186 ? -8 : -14), phraseEvery: 4 })), 96, "before the outro");
  assert.equal(rolledAt(song({ level: (t) => (t < 186 ? -8 : -14) })), 96, "without phrases, the latest bar in full");
  assert.notEqual(planMix(song({ vocals: [[150, 200]] }), other, { ...STYLED, prefer: "roll" }).kind, "roll",
    "never further back than eight bars");
  // A verse's last lines cut off, 14 and 26 s before the end, were heard as too early (24/11).
  assert.notEqual(planMix(song({ vocals: [[184, 200]], phraseEvery: 4 }), other, { ...STYLED, prefer: "roll" }).kind,
    "roll", "nor over a singer's last lines");
});

// Heard on 24/11, the same songs both ways: rolled 7 s before the song's own fade it was better than the crossfade;
// 14 and 26 s before, the crossfade was.
test("a song that fades out by itself rolls just before its fade, or keeps its crossfade", () => {
  const fading = (vocals) => song({ fadeOut: { startInMilliseconds: 190000, endInMilliseconds: 198000 }, vocals,
                                    level: (t) => (t < 190 ? -8 : t < 198 ? -8 - (t - 190) * 3 : -60) });
  const other = song({ bpm: 96, attributes: { albumName: "Other" } });
  const rolled = planMix(fading([]), other, { ...STYLED, prefer: "roll" });
  assert.equal(rolled.kind, "roll");
  assertKernelTakes(rolled.plan, fading([]), other);
  const drop = rolled.plan.keys.at(-1)[0] + SWAP_AHEAD_S;
  assert.ok(drop <= 190 && drop >= 180, `on a bar just before its fade (${drop})`);
  // Sung up to its fade: the nearest bar it could roll is more than ten seconds before it.
  assert.equal(planMix(fading([[170, 198]]), other, { ...STYLED, prefer: "roll" }).kind, "crossfade");
});

test("each pair draws its style, always the same, the ones least like a crossfade most often", () => {
  const a = song({ loudTo: 190 });
  const b = song({ attributes: { albumName: "Other" } });
  const styles = {};
  for (let i = 0; i < 60; i++) {
    const x = { ...a, id: "a" + i };
    const y = { ...b, id: "b" + i };
    const first = planMix(x, y, STYLED);
    assert.equal(planMix(x, y, STYLED).style, first.style, "the same pair, the same style");
    styles[first.style] = (styles[first.style] ?? 0) + 1;
  }
  assert.ok(Object.keys(styles).length >= 4, "every style comes up: " + JSON.stringify(styles));
  assert.ok(styles.stabs > styles.crossover && styles.stabs > styles.drop, "stabs most: " + JSON.stringify(styles));
});

test("songs whose keys clash are never held both in full, and roll more often", () => {
  const draw = (key) => {
    const styles = {};
    for (let i = 0; i < 60; i++) {
      const a = { ...song({ loudTo: 190, key: { tonic: "C", mode: "major" } }), id: "a" + i };
      const b = { ...song({ key, attributes: { albumName: "Other" } }), id: "b" + i };
      const style = planMix(a, b, STYLED).style;
      styles[style] = (styles[style] ?? 0) + 1;
    }
    return styles;
  };
  const clash = draw({ tonic: "F#", mode: "major" });
  const agree = draw({ tonic: "G", mode: "major" });
  assert.equal(clash.crossover, undefined, "no crossover: " + JSON.stringify(clash));
  assert.ok(clash.roll > (agree.roll ?? 0), "more rolls: " + JSON.stringify(clash) + " " + JSON.stringify(agree));
  assert.ok(Object.keys(clash).length >= 3, "still a style drawn for each pair: " + JSON.stringify(clash));
});

// Heard on 24/11: an echo too short (s2-4, s2-5), one cut too fast and off the next song's beat (n-4), a dead stop
// with two beats of echo alone before a late drop (n3-4), then one that rose muffled under a fading song (n5-4): "let it
// come in lightly a little before, filtered, then the drop right as the transition is full", as the rolls do; then
// (n6-1) "it comes in a bit too late, and the echo is not heard enough".
test("with the styles, an echo out lets the next song creep in thin, then drops it whole on the bar", () => {
  const a = song({ bpm: 124 });
  // A quiet intro, in full from 7 s: its bars at 138 bpm fall every 1.74 s, the drop on the one at 6.96 s.
  const b = song({ bpm: 138, level: (t) => (t < 7 ? -20 : -8), attributes: { albumName: "Other" } });
  const result = planMix(a, b, { ...STYLED, prefer: "echo" });
  assert.equal(result.kind, "echo");
  assertKernelTakes(result.plan, a, b);
  const { keys, echo, bStart } = result.plan;
  assert.equal(echo.feedback, 0.5, "each repeat half the one before, ringing on over the drop");
  assert.ok(echo.level > 0.7 && echo.level <= 2, "louder than the kernel's own echo, to be heard over the drop");
  assert.ok(Math.abs(echo.delay - 60 / 138) < 1e-3, "the repeats on the next song's beats");
  const swapAt = keys.at(-1)[0] + SWAP_AHEAD_S;
  assert.ok(barTimes(a).some((t) => Math.abs(t - swapAt) < 1e-6), `the swap on a bar of the current song (${swapAt})`);
  assert.ok(Math.abs(echo.to - keys.at(-1)[0]) < 1e-9 && Math.abs(echo.from - (swapAt - 2 * echo.delay)) < 1e-6,
            "its last two beats thrown into the echo");
  const before = keys.slice(0, -1);
  assert.ok(before.every((k) => k[2] <= 0.7), "heard before the bar, never as loud as the current song");
  // Apple's bars are in milliseconds: a bar is the song's own to a millisecond.
  assert.ok(Math.abs(keys[0][0] - (swapAt - 2 * BAR)) < 2e-3 && keys[0][2] === 0, "unheard two bars before...");
  const lastBar = keys.find((k) => Math.abs(k[0] - (swapAt - BAR)) < 2e-3);
  assert.ok(lastBar && lastBar[2] >= 0.25, "...already heard as the last bar starts...");
  assert.ok(before.slice(1).every((k, i) => k[2] > before[i][2]), "...and rising all through it");
  assert.ok(before.filter((k) => k[2] > 0).every((k) => k[4] === 0 && k[6] === 0 && k[8] >= 0.5),
            "thin, not muffled, and without its bass then");
  assert.ok(before.every((k) => k[1] === 1 && k[3] === 1 && k[5] === 0), "the current song whole, bass and all, to the bar");
  assert.ok(before.at(-1)[7] > 0 && before.at(-1)[7] <= 0.25, "darkening a little, so that its echo is heard");
  assert.equal(keys.at(-1)[1], 0, "gone on the bar");
  const entry = bStart + (swapAt - keys[0][0]);
  assert.ok(Math.abs(entry - barTimes(b)[4]) < 1e-3, `the next song in full on its drop, on the bar (${entry})`);
  // Without the crossover's filters it creeps in muffled instead.
  const plain = planMix(a, b, { ...STYLED, can: new Set([...STYLED.can].filter((x) => x !== "crossover")), prefer: "echo" });
  assert.equal(plain.kind, "echo");
  assertKernelTakes(plain.plan, a, b);
  assert.ok(plain.plan.keys.slice(0, -1).filter((k) => k[2] > 0).every((k) => k[4] === 0 && k[6] > 0), "muffled then");
});

// Heard on 24/11 (n-2): a hit, then the next song at once, "could have had a little crossfade".
test("with the styles, a song's natural end joins the next song in a short crossfade over its last ring", () => {
  const a = song({ loudTo: 195 });
  const b = song({ bpm: 138, loudFrom: 1, attributes: { albumName: "Other" } });
  const result = planMix(a, b, STYLED);
  assert.equal(result.kind, "natural");
  assertKernelTakes(result.plan, a, b);
  const { keys, bStart } = result.plan;
  assert.equal(keys.length, 2);
  assert.ok(keys[0][0] >= 194.7 && keys[0][0] < 195.3, `from under the hit's ring (${keys[0][0]})`);
  assert.ok(Math.abs(keys[1][0] - keys[0][0] - 1) < 1e-9, "the next song in full a second later");
  assert.ok(keys[0][6] > 0 && keys[1][6] === 0, "muffled at first, then open");
  assert.ok(Math.abs(bStart - 1) < 1e-9, "from its first sound");
});

// Apple may give a value it does not know as null, and null < 0.2 is true in JavaScript: unknown is not soft.
test("a song whose analysis has no number for its danceability and energy is not taken for a soft one", () => {
  const a = song({ loudTo: 195 });
  for (const name of ["danceability", "energy", "acousticness"]) a.analysis[name] = { main: null, beginning: null, ending: null };
  const b = song({ attributes: { albumName: "Other" } });
  const result = planMix(a, b, CAN);
  assert.ok(!result.soft, `not left to end softly (${result.kind})`);
});

test("a crossfade after a fade already deep where it starts lands the next song's drop there, not past it", () => {
  // Down 9 dB within two seconds, then slowly on to near silence at 195 s: deep already where a 12 s crossfade starts.
  const level = (t) => (t < 170 ? -8 : t < 172 ? -8 - (t - 170) * 4.5 : t < 195 ? -17 - (t - 172) * 8 / 23 : -60);
  const a = song({ level, fadeOut: { startInMilliseconds: 170000, endInMilliseconds: 195000 } });
  const b = song({ bpm: 96, level: (t) => (t < 7 ? -25 : -8), attributes: { albumName: "Other" } });
  const result = planMix(a, b, CAN);
  assert.equal(result.kind, "crossfade");
  assertKernelTakes(result.plan, a, b);
  assert.ok(Math.abs(result.plan.bStart - 7) < 1e-6, `its drop at the crossfade's start (${result.plan.bStart})`);
});

test("songs that cannot share a tempo roll or leave on an echo, by pair", () => {
  const a = song();
  const b = song({ bpm: 96, attributes: { albumName: "Other" } });
  const kinds = {};
  for (let i = 0; i < 100; i++) {
    const kind = planMix({ ...a, id: "a" + i }, { ...b, id: "b" + i }, STYLED).kind;
    kinds[kind] = (kinds[kind] ?? 0) + 1;
  }
  assert.deepEqual(Object.keys(kinds).sort(), ["echo", "roll"]);
  // Heard as a fade (24/11 series: 6 echoes in 12 mixes): about one pair in ten.
  assert.ok(kinds.echo <= 15, "the echo, like a fade, now and then only: " + JSON.stringify(kinds));
});
