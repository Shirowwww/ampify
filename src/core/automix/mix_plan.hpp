#pragma once

// AutoMix: how the current song (A) hands over to the next one (B). A plan says where, in A's time, B comes in, at
// which point of B, how both volumes and both songs' lows move until A is gone, and how fast A plays meanwhile. The
// session (mix_session.hpp) plays it; the automix extension writes it from Apple's analysis of both songs. Pure C++.

#include <optional>
#include <string>
#include <vector>

#include "../json.hpp"

namespace ampify::automix {

// One point of the curves: when A is at `at` seconds, A plays at `a` and the next song (B) at `b`, both 0..1 of the
// user's volume. `aLow` and `bLow` are what is left of each song's lows (under 150 Hz or so), 1 for all of them: two
// bass lines at once are mud, so a mix swaps them.
struct GainKey {
    double at = 0;
    double a = 1;
    double b = 0;
    double aLow = 1;
    double bLow = 1;
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
};

struct Gains {
    double a = 1;
    double b = 0;
    double aLow = 1;
    double bLow = 1;
};

inline constexpr size_t kMaxKeys = 64;
inline constexpr double kMaxMixSeconds = 60;   // first key to last
inline constexpr double kMaxRateGap = 0.1;     // rate between 0.9 and 1.1
inline constexpr double kMaxRampSeconds = 30;  // rateFrom to the first key

// The volumes when A is at `at`: those of the first key before it and of the last key after it. Between two keys the
// squared gains move along a smoothstep, so a crossfade keeps its power and starts and stops without a corner; the
// lows move along the same smoothstep.
Gains GainsAt(const std::vector<GainKey>& keys, double at);

bool Stretches(const MixPlan& plan);  // A changes speed

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
// Seconds of A there must be after the last key: the second player, that far ahead of what is heard by then at
// most, stops feeding the deck once it is through the song.
double RoomAfter(const MixPlan& plan);

// Why the plan cannot be played, or empty. The durations are the songs' lengths in seconds, 0 when unknown.
std::string Check(const MixPlan& plan, double aDuration, double bDuration);

// The plan from the object Ampify.automix.schedule takes: {next, bStart, keys: [[at, a, b] or [at, a, b, aLow, bLow],
// ...], rate, rateFrom}, the last two optional.
std::optional<MixPlan> PlanFromJson(const Json& json, std::string* error);

}  // namespace ampify::automix
