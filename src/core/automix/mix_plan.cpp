#include "mix_plan.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace ampify::automix {

namespace {

constexpr double kLeadMargin = 1.5;      // seconds of lead whatever the plan: the stretch's look-ahead and some air
constexpr double kHoldMargin = 1.5;      // seconds the deck holds beyond the lead: a seek lands where it lands
constexpr double kStretchLatency = 0.3;  // what the stretch reads ahead of what it plays, at most
constexpr double kEchoFloor = 0.01;      // an echo's repeats are over 40 dB down
constexpr double kMaxEchoRing = 8;

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
    auto of = [](const GainKey& key) { return Gains{key.a, key.b, key.aLow, key.bLow, key.aFilter, key.bFilter}; };
    if (!(at > keys.front().at)) return of(keys.front());
    if (at >= keys.back().at) return of(keys.back());
    auto next = std::upper_bound(keys.begin(), keys.end(), at, [](double t, const GainKey& k) { return t < k.at; });
    const GainKey& to = *next;
    const GainKey& from = *(next - 1);
    double span = to.at - from.at;
    double s = span > 0 ? Smoothstep((at - from.at) / span) : 1;
    auto line = [s](double x, double y) { return x + (y - x) * s; };
    return {Between(from.a, to.a, s),       Between(from.b, to.b, s),       line(from.aLow, to.aLow),
            line(from.bLow, to.bLow),       line(from.aFilter, to.aFilter), line(from.bFilter, to.bFilter)};
}

bool Stretches(const MixPlan& plan) { return plan.rate != 1; }

bool Cuts(const MixPlan& plan) { return plan.keys.size() == 2 && plan.keys[0].at == plan.keys[1].at; }

// Anything but 0 asks for one: a delay or length that is not a time is refused by Check, not ignored.
bool Echoes(const MixPlan& plan) { return plan.echo.delay != 0; }

bool Loops(const MixPlan& plan) { return plan.loop.length != 0; }

double EchoSendAt(const MixPlan& plan, double aTime) {
    return Echoes(plan) && aTime >= plan.echo.from && aTime < plan.echo.to ? 1 : 0;
}

double EchoAfter(const MixPlan& plan) {
    if (!Echoes(plan) || plan.keys.empty()) return 0;
    const Echo& echo = plan.echo;
    // Each repeat is `feedback` of the one before: that many repeats until they are under the floor, and the first.
    const double repeats = echo.feedback > 0 ? 1 + std::ceil(std::log(kEchoFloor) / std::log(echo.feedback)) : 1;
    const double ring = std::min(echo.delay * repeats, kMaxEchoRing);
    return std::max(0.0, ring - (plan.keys.back().at - echo.to) / plan.rate);
}

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
        if (!InUnit(key.aFilter) || !InUnit(key.bFilter)) return "a key's filters must be between 0 and 1";
        if (i > 0 && key.at < plan.keys[i - 1].at) return "the keys are not in time order";
    }
    const GainKey& first = plan.keys.front();
    const GainKey& last = plan.keys.back();
    if (first.a != 1 || first.b != 0) return "the first key must have A at 1 and B at 0";
    if (last.a != 0 || last.b != 1) return "the last key must leave A at 0 and B at 1";
    if (last.bLow != 1) return "the last key must leave B its lows";
    if (last.bFilter != 0) return "the last key must leave B unfiltered";
    const bool cut = Cuts(plan);
    if (last.at <= first.at && !cut) return "the mix has no length";
    if (cut && Stretches(plan)) return "a cut leaves A's speed alone";
    if (last.at - first.at > kMaxMixSeconds) return "the mix is longer than 60 seconds";
    if (!std::isfinite(plan.bStart) || plan.bStart < 0) return "B's start is not a time";
    if (!(plan.rate >= 1 - kMaxRateGap && plan.rate <= 1 + kMaxRateGap)) return "the rate must be between 0.9 and 1.1";
    if (Stretches(plan)) {
        if (!std::isfinite(plan.rateFrom) || plan.rateFrom < 0) return "rateFrom is not a time";
        if (plan.rateFrom >= first.at) return "rateFrom must come before the first key";
        if (first.at - plan.rateFrom > kMaxRampSeconds) return "rateFrom is more than 30 seconds before the first key";
    }
    // A loop needs the second player only until it has the loop: A may end long before the mix does.
    const bool loops = Loops(plan);
    if (aDuration > 0 && last.at >= aDuration && !loops) return "the mix goes past the end of the song";
    if (aDuration > 0 && !cut && !loops && aDuration - last.at < RoomAfter(plan)) {
        return "the mix ends too close to the end of the song";
    }
    if (aDuration > 0 && loops && aDuration - first.at < RoomAfter(plan)) {
        return "the mix starts too close to the end of the song";
    }
    if (bDuration > 0 && NextTimeAt(plan, last.at) >= bDuration) return "the next song would end during the mix";
    if (Echoes(plan)) {
        const Echo& echo = plan.echo;
        if (cut) return "a cut has no echo";
        if (!(echo.delay >= kMinEchoDelay && echo.delay <= kMaxEchoDelay)) {
            return "the echo's delay must be between 0.05 and 2 seconds";
        }
        if (!(echo.feedback >= 0 && echo.feedback <= kMaxEchoFeedback)) {
            return "the echo's feedback must be between 0 and 0.9";
        }
        if (!(echo.from >= first.at && echo.from < echo.to && echo.to <= last.at)) {
            return "the echo must start and end within the mix";
        }
    }
    if (loops) {
        const Loop& loop = plan.loop;
        if (cut) return "a cut has no loop";
        if (!(loop.length >= kMinLoopSeconds && loop.length <= kMaxLoopSeconds)) {
            return "the loop must be between 0.2 and 8 seconds long";
        }
        if (!(loop.from >= first.at && loop.from < last.at)) return "the loop must start within the mix";
        if (aDuration > 0 && loop.from + loop.length + kLoopRoom > aDuration) {
            return "the loop ends too close to the end of the song";
        }
    }
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
    // Optional objects of numbers, each named field required.
    auto readNumbers = [&](const char* name, std::initializer_list<std::pair<const char*, double*>> fields) {
        const Json* object = json.Find(name);
        if (!object) return true;
        if (!object->IsObject()) return false;
        for (const auto& [field, into] : fields) {
            const Json* value = object->Find(field);
            if (!value || !value->AsNumber()) return false;
            *into = *value->AsNumber();
        }
        return true;
    };
    if (!readNumbers("echo", {{"from", &plan.echo.from},
                              {"to", &plan.echo.to},
                              {"delay", &plan.echo.delay},
                              {"feedback", &plan.echo.feedback}})) {
        return fail("echo must be {from, to, delay, feedback}, all numbers");
    }
    if (!readNumbers("loop", {{"from", &plan.loop.from}, {"length", &plan.loop.length}})) {
        return fail("loop must be {from, length}, both numbers");
    }
    const Json* keys = json.Find("keys");
    if (!keys || !keys->AsArray()) return fail("keys must be an array of [at, a, b] or longer keys");
    for (const Json& key : *keys->AsArray()) {
        const Json::Array* numbers = key.AsArray();
        const size_t size = numbers ? numbers->size() : 0;
        bool valid = (size == 3 || size == 5 || size == 7) &&
                     std::all_of(numbers->begin(), numbers->end(), [](const Json& n) { return n.AsNumber() != nullptr; });
        if (!valid) {
            return fail("each key must be [at, a, b], [at, a, b, aLow, bLow] or [at, a, b, aLow, bLow, aFilter, bFilter], "
                        "all numbers");
        }
        auto number = [&](size_t i) { return *(*numbers)[i].AsNumber(); };
        GainKey point{number(0), number(1), number(2)};
        if (size >= 5) {
            point.aLow = number(3);
            point.bLow = number(4);
        }
        if (size == 7) {
            point.aFilter = number(5);
            point.bFilter = number(6);
        }
        plan.keys.push_back(point);
        if (plan.keys.size() > kMaxKeys) return fail("too many keys");
    }
    return plan;
}

}  // namespace ampify::automix
