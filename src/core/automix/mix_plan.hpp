#pragma once

// AutoMix: how the current song (A) hands over to the next one (B). A plan says where, in A's time, B comes in, at
// which point of B, how both volumes, both songs' lows and their filters move until A is gone, how fast A plays
// meanwhile, and whether A leaves on an echo or loops a bar. The session (mix_session.hpp) plays it; the automix
// extension writes it from Apple's analysis of both songs. Pure C++.

#include <optional>
#include <string>
#include <vector>

#include "../json.hpp"

namespace ampify::automix {

// One point of the curves: when A is at `at` seconds, A plays at `a` and the next song (B) at `b`, both 0..1 of the
// user's volume. `aLow` and `bLow` are what is left of each song's lows (under 150 Hz or so), 1 for all of them: two
// bass lines at once are mud, so a mix swaps them. `aFilter` thins A out from below (a high-pass rising from 20 Hz at
// 0 to 2 kHz at 1) and `bFilter` muffles B from above (a low-pass coming down from 20 kHz at 0 to 400 Hz at 1).
// `aMuffle` and `bThin` are the other way round, steeper, and reach further: A through a low-pass coming down from
// 20 kHz at 0 to 30 Hz at 1, B through a high-pass rising from 2 Hz at 0 to 16 kHz at 1. At one cutoff the two add
// up to the power of either song alone: a crossover that hands A's sound to B's from the top down, both at full volume.
struct GainKey {
    double at = 0;
    double a = 1;
    double b = 0;
    double aLow = 1;
    double bLow = 1;
    double aFilter = 0;
    double bFilter = 0;
    double aMuffle = 0;
    double bThin = 0;
};

// An echo thrown on A, as a DJ does at the end of a phrase: from `from` to `to` (A's time) what the deck plays of A
// also goes into a delay line, whose repeats, each a little thinner than the last, ring on over B once A is gone.
// `delay` is the time between repeats (a beat, or half of one), `feedback` what each repeat is of the one before.
inline constexpr double kEchoLevel = 0.7;  // an echo's first repeat against what was thrown, unless the plan says

struct Echo {
    double from = 0;
    double to = 0;
    double delay = 0;  // 0: no echo
    double feedback = 0;
    double level = kEchoLevel;
};

// The loop rolled shorter, as a DJ rolls a bar into a drop: from `at` (A's time, which runs on through the loop) the
// deck plays the loop's first `length` seconds over and over. Each roll starts on a turn of the one before (of the
// loop, for the first) and is shorter than it.
struct LoopRoll {
    double at = 0;
    double length = 0;
};

// A stretch of A played over and over from `from` (A's time) on, `length` seconds of it (a bar), until the mix is
// over: A goes on past its end, or past a break, for as long as B needs. A's time runs on through the loop, as if
// the song went on: the keys after `from` are in that time.
struct Loop {
    double from = 0;
    double length = 0;  // 0: no loop
    std::vector<LoopRoll> rolls;  // by time
};

// B in stabs on its beats, as a DJ teases the next song in under the current one: from each of `beats` (A's time:
// B's beats laid on A's) B is heard for `on` seconds, and not between them, until `until`, from where it plays on as
// the keys say. Under each stab A dips by `duck` (0..1 of its volume), so that the stab is heard.
struct Gate {
    std::vector<double> beats;  // empty: no gate
    double on = 0;
    double duck = 0;
    double until = 0;
};

// Apple's player moves on to B when A reaches the first key, B at bStart; Ampify's deck carries A's end until the last.
struct MixPlan {
    std::string next;           // the next song the plan is for, as MixDecks::NextItem names it
    double bStart = 0;          // B's time at the first key
    std::vector<GainKey> keys;  // by time; the first has a = 1 and b = 0, the last a = 0 and b = 1
    // A's speed through the mix, its pitch unchanged: B's tempo over A's, so that their beats stay together. It moves
    // from 1 at rateFrom (A's time) to `rate` at the first key. 1 leaves A as recorded and ignores rateFrom.
    double rate = 1;
    double rateFrom = 0;
    Echo echo;
    Loop loop;
    Gate gate;
};

struct Gains {
    double a = 1;
    double b = 0;
    double aLow = 1;
    double bLow = 1;
    double aFilter = 0;
    double bFilter = 0;
    double aMuffle = 0;
    double bThin = 0;
};

inline constexpr size_t kMaxKeys = 64;
inline constexpr double kMaxMixSeconds = 60;   // first key to last
inline constexpr double kMaxRateGap = 0.1;     // rate between 0.9 and 1.1
inline constexpr double kMaxRampSeconds = 30;  // rateFrom to the first key
inline constexpr double kMinEchoDelay = 0.05;  // seconds between an echo's repeats
inline constexpr double kMaxEchoDelay = 2;
inline constexpr double kMaxEchoFeedback = 0.9;
inline constexpr double kMaxEchoLevel = 2;
inline constexpr double kMinLoopSeconds = 0.2;
inline constexpr double kMaxLoopSeconds = 8;
inline constexpr double kLoopRoom = 1;  // seconds of A after the loop: the second player is paused before its end
inline constexpr size_t kMaxLoopRolls = 8;
inline constexpr double kMinRollSeconds = 0.03;  // an eighth of a beat at 140 bpm is 54 ms
inline constexpr size_t kMaxGateBeats = 128;
inline constexpr double kMinStabSeconds = 0.02;
inline constexpr double kMaxStabSeconds = 2;
// A stab opens over this long before its beat, so that the beat's attack is whole, and closes over this long after.
inline constexpr double kStabRise = 0.003;
inline constexpr double kStabFall = 0.008;
// The second player plays A ahead of what is heard: through A, it would stop, and its stream with the deck's sound in
// it (about a second past the song's end). It is paused this long before A's end and the deck plays what it holds:
// a mix that needs the deck may end this close to A's end.
inline constexpr double kGhostEndMargin = 0.3;

// The volumes when A is at `at`: those of the first key before it and of the last key after it. Between two keys the
// squared gains move along a smoothstep, so a crossfade keeps its power and starts and stops without a corner; the
// lows and the filters move along the same smoothstep, the crossover in a straight line (its sweeps have a key a beat
// and would stop on each).
Gains GainsAt(const std::vector<GainKey>& keys, double at);

bool Stretches(const MixPlan& plan);  // A changes speed

// A cut: two keys at one time. Apple's player alone leaves A there for B at bStart, end to start, with no overlap and
// no second player, so A may play on up to its very end (a ballad left to finish, a song that ends on a hit).
bool Cuts(const MixPlan& plan);

bool Echoes(const MixPlan& plan);
bool Loops(const MixPlan& plan);
bool Gates(const MixPlan& plan);

// 1 while B is in stabs, from the first stab's rise to the gate's end, at A's time `aTime`; else 0.
double GateAt(const MixPlan& plan, double aTime);
// How open a stab leaves B at A's time `aTime`, 0..1: 1 from each beat for `on` seconds, with short edges.
double StabAt(const Gate& gate, double aTime);

// 1 while A goes into the echo, at A's time `aTime`, else 0.
double EchoSendAt(const MixPlan& plan, double aTime);
// Seconds the echo's repeats still ring after the last key, as they pass for the listener: until they are 40 dB down,
// 8 s at most.
double EchoAfter(const MixPlan& plan);

// A's time from which the deck must carry A: where its speed starts to move, or the first key.
double CarryFrom(const MixPlan& plan);

// Seconds from CarryFrom to the first key as they pass for the listener (A's speed moves evenly in real time), which
// is also A's time in Apple's player, still at normal speed, when the deck's copy reaches the first key.
double RampSeconds(const MixPlan& plan);
double SwitchTime(const MixPlan& plan);  // A's time at which Apple's player must leave A

// B's time when the deck's A is at `aTime`, from the first key on.
double NextTimeAt(const MixPlan& plan, double aTime);

// How far ahead of the deck's reading the second player must run, in seconds of A: what a faster A eats of that lead
// and what the stretch looks ahead, with a margin.
double LeadFor(const MixPlan& plan);
// The most it may run ahead when the deck starts: a slower A falls further behind it, and the deck holds no more.
double MostLead(const MixPlan& plan);
// How much of the second player's sound the deck must be able to hold, in seconds.
double HoldFor(const MixPlan& plan);

// Why the plan cannot be played, or empty. The durations are the songs' lengths in seconds, 0 when unknown.
std::string Check(const MixPlan& plan, double aDuration, double bDuration);

// The plan from the object Ampify.automix.schedule takes: {next, bStart, keys: [[at, a, b], [at, a, b, aLow, bLow],
// [at, a, b, aLow, bLow, aFilter, bFilter] or [at, a, b, aLow, bLow, aFilter, bFilter, aMuffle, bThin], ...], rate,
// rateFrom, echo: {from, to, delay, feedback}, loop: {from, length, rolls: [[at, length], ...]}, gate: {beats: [...],
// on, duck, until}}, the last five optional (and a loop's rolls).
std::optional<MixPlan> PlanFromJson(const Json& json, std::string* error);

}  // namespace ampify::automix
