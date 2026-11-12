#pragma once

// AutoMix: how the current song (A) hands over to the next one (B). A plan says where, in A's time, B comes in, at
// which point of B, and how both volumes move until A is gone. The session (mix_session.hpp) plays it; the automix
// extension writes it from Apple's analysis of both songs. Pure C++.

#include <optional>
#include <string>
#include <vector>

#include "../json.hpp"

namespace ampify::automix {

// One point of the volume curves: when A is at `at` seconds, A plays at `a` and the next song (B) at `b`, both 0..1 of
// the user's volume.
struct GainKey {
    double at = 0;
    double a = 1;
    double b = 0;
};

// Apple's player moves on to B at the first key, B at bStart; Ampify's second player carries A's end until the last.
struct MixPlan {
    std::string next;           // the next song the plan is for, as MixDecks::NextItem names it
    double bStart = 0;          // B's time at the first key
    std::vector<GainKey> keys;  // by time; the first has a = 1 and b = 0, the last a = 0 and b = 1
};

struct Gains {
    double a = 1;
    double b = 0;
};

inline constexpr size_t kMaxKeys = 64;
inline constexpr double kMaxMixSeconds = 60;  // first key to last

// The volumes when A is at `at`: those of the first key before it and of the last key after it. Between two keys the
// squared gains move along a smoothstep, so a crossfade keeps its power and starts and stops without a corner.
Gains GainsAt(const std::vector<GainKey>& keys, double at);

// B's time when A is at `aTime`, both at normal speed.
double NextTimeAt(const MixPlan& plan, double aTime);

// Why the plan cannot be played, or empty. The durations are the songs' lengths in seconds, 0 when unknown.
std::string Check(const MixPlan& plan, double aDuration, double bDuration);

// The plan from the object Ampify.automix.schedule takes: {next, bStart, keys: [[at, a, b], ...]}.
std::optional<MixPlan> PlanFromJson(const Json& json, std::string* error);

}  // namespace ampify::automix
