#include "mix_plan.hpp"

#include <algorithm>
#include <cmath>

namespace ampify::automix {

namespace {

constexpr double kLeadMargin = 1.5;      // seconds of lead whatever the plan: the stretch's look-ahead and some air
constexpr double kHoldMargin = 1.5;      // seconds the deck holds beyond the lead: a seek lands where it lands
constexpr double kStretchLatency = 0.3;  // what the stretch reads ahead of what it plays, at most

double Smoothstep(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

// Squared gains move linearly: a crossfade from (1, 0) to (0, 1) keeps a^2 + b^2 = 1, the equal-power law.
double Between(double from, double to, double s) { return std::sqrt(from * from + (to * to - from * from) * s); }

bool InUnit(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }

// Seconds of real time the deck plays A for, from CarryFrom to the last key.
double CarriedSeconds(const MixPlan& plan) {
    if (plan.keys.empty()) return 0;
    return RampSeconds(plan) + (plan.keys.back().at - plan.keys.front().at) / plan.rate;
}

}  // namespace

Gains GainsAt(const std::vector<GainKey>& keys, double at) {
    if (keys.empty()) return {};
    auto of = [](const GainKey& key) { return Gains{key.a, key.b, key.aLow, key.bLow}; };
    if (!(at > keys.front().at)) return of(keys.front());
    if (at >= keys.back().at) return of(keys.back());
    auto next = std::upper_bound(keys.begin(), keys.end(), at, [](double t, const GainKey& k) { return t < k.at; });
    const GainKey& to = *next;
    const GainKey& from = *(next - 1);
    double span = to.at - from.at;
    double s = span > 0 ? Smoothstep((at - from.at) / span) : 1;
    return {Between(from.a, to.a, s), Between(from.b, to.b, s), from.aLow + (to.aLow - from.aLow) * s,
            from.bLow + (to.bLow - from.bLow) * s};
}

bool Stretches(const MixPlan& plan) { return plan.rate != 1; }

double CarryFrom(const MixPlan& plan) {
    double first = plan.keys.empty() ? 0 : plan.keys.front().at;
    return Stretches(plan) ? std::min(plan.rateFrom, first) : first;
}

double RampSeconds(const MixPlan& plan) {
    if (plan.keys.empty()) return 0;
    // The speed goes from 1 to rate evenly: on average (1 + rate) / 2 over the ramp.
    return 2 * (plan.keys.front().at - CarryFrom(plan)) / (1 + plan.rate);
}

double SwitchTime(const MixPlan& plan) { return CarryFrom(plan) + RampSeconds(plan); }

double NextTimeAt(const MixPlan& plan, double aTime) {
    double start = plan.keys.empty() ? 0 : plan.keys.front().at;
    return plan.bStart + (aTime - start) / plan.rate;
}

namespace {

// A faster A reads more than a second of song per second: the lead melts by that much.
double Eaten(const MixPlan& plan) { return std::max(0.0, plan.rate - 1) * CarriedSeconds(plan); }

// A slower A reads less than it receives: what waits grows by that much.
double Kept(const MixPlan& plan) { return std::max(0.0, 1 - plan.rate) * CarriedSeconds(plan); }

}  // namespace

double LeadFor(const MixPlan& plan) {
    return kLeadMargin + Eaten(plan) + (Stretches(plan) ? kStretchLatency : 0);
}

double MostLead(const MixPlan& plan) { return LeadFor(plan) + kHoldMargin; }

double HoldFor(const MixPlan& plan) { return MostLead(plan) + Kept(plan); }

double RoomAfter(const MixPlan& plan) { return HoldFor(plan) - Eaten(plan); }

std::string Check(const MixPlan& plan, double aDuration, double bDuration) {
    if (plan.next.empty()) return "the plan names no next song";
    if (plan.keys.size() < 2) return "a plan needs two keys or more";
    if (plan.keys.size() > kMaxKeys) return "too many keys";
    for (size_t i = 0; i < plan.keys.size(); i++) {
        const GainKey& key = plan.keys[i];
        if (!std::isfinite(key.at) || key.at < 0) return "a key has no valid time";
        if (!InUnit(key.a) || !InUnit(key.b)) return "a key's volumes must be between 0 and 1";
        if (!InUnit(key.aLow) || !InUnit(key.bLow)) return "a key's lows must be between 0 and 1";
        if (i > 0 && key.at < plan.keys[i - 1].at) return "the keys are not in time order";
    }
    const GainKey& first = plan.keys.front();
    const GainKey& last = plan.keys.back();
    if (first.a != 1 || first.b != 0) return "the first key must have A at 1 and B at 0";
    if (last.a != 0 || last.b != 1) return "the last key must leave A at 0 and B at 1";
    if (last.bLow != 1) return "the last key must leave B its lows";
    if (last.at <= first.at) return "the mix has no length";
    if (last.at - first.at > kMaxMixSeconds) return "the mix is longer than 60 seconds";
    if (!std::isfinite(plan.bStart) || plan.bStart < 0) return "B's start is not a time";
    if (!(plan.rate >= 1 - kMaxRateGap && plan.rate <= 1 + kMaxRateGap)) return "the rate must be between 0.9 and 1.1";
    if (Stretches(plan)) {
        if (!std::isfinite(plan.rateFrom) || plan.rateFrom < 0) return "rateFrom is not a time";
        if (plan.rateFrom >= first.at) return "rateFrom must come before the first key";
        if (first.at - plan.rateFrom > kMaxRampSeconds) return "rateFrom is more than 30 seconds before the first key";
    }
    if (aDuration > 0 && last.at >= aDuration) return "the mix goes past the end of the song";
    if (aDuration > 0 && aDuration - last.at < RoomAfter(plan)) return "the mix ends too close to the end of the song";
    if (bDuration > 0 && NextTimeAt(plan, last.at) >= bDuration) return "the next song would end during the mix";
    return {};
}

std::optional<MixPlan> PlanFromJson(const Json& json, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<MixPlan> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!json.IsObject()) return fail("the plan must be an object");
    MixPlan plan;
    const Json* next = json.Find("next");
    if (!next || !next->AsString()) return fail("next must be the next song's name from Ampify.automix.state()");
    plan.next = *next->AsString();
    const Json* bStart = json.Find("bStart");
    if (!bStart || !bStart->AsNumber()) return fail("bStart must be a number of seconds");
    plan.bStart = *bStart->AsNumber();
    if (const Json* rate = json.Find("rate")) {
        if (!rate->AsNumber()) return fail("rate must be a number");
        plan.rate = *rate->AsNumber();
    }
    if (const Json* rateFrom = json.Find("rateFrom")) {
        if (!rateFrom->AsNumber()) return fail("rateFrom must be a number of seconds");
        plan.rateFrom = *rateFrom->AsNumber();
    }
    const Json* keys = json.Find("keys");
    if (!keys || !keys->AsArray()) return fail("keys must be an array of [at, a, b] or [at, a, b, aLow, bLow]");
    for (const Json& key : *keys->AsArray()) {
        const Json::Array* numbers = key.AsArray();
        bool valid = numbers && (numbers->size() == 3 || numbers->size() == 5) &&
                     std::all_of(numbers->begin(), numbers->end(), [](const Json& n) { return n.AsNumber() != nullptr; });
        if (!valid) return fail("each key must be [at, a, b] or [at, a, b, aLow, bLow], all numbers");
        auto number = [&](size_t i) { return *(*numbers)[i].AsNumber(); };
        GainKey point{number(0), number(1), number(2)};
        if (numbers->size() == 5) {
            point.aLow = number(3);
            point.bLow = number(4);
        }
        plan.keys.push_back(point);
        if (plan.keys.size() > kMaxKeys) return fail("too many keys");
    }
    return plan;
}

}  // namespace ampify::automix
