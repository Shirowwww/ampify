#include "mix_plan.hpp"

#include <algorithm>
#include <cmath>

namespace ampify::automix {

namespace {

double Smoothstep(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

// Squared gains move linearly: a crossfade from (1, 0) to (0, 1) keeps a^2 + b^2 = 1, the equal-power law.
double Between(double from, double to, double s) { return std::sqrt(from * from + (to * to - from * from) * s); }

bool InUnit(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }

}  // namespace

Gains GainsAt(const std::vector<GainKey>& keys, double at) {
    if (keys.empty()) return {};
    if (!(at > keys.front().at)) return {keys.front().a, keys.front().b};
    if (at >= keys.back().at) return {keys.back().a, keys.back().b};
    auto next = std::upper_bound(keys.begin(), keys.end(), at, [](double t, const GainKey& k) { return t < k.at; });
    const GainKey& to = *next;
    const GainKey& from = *(next - 1);
    double span = to.at - from.at;
    double s = span > 0 ? Smoothstep((at - from.at) / span) : 1;
    return {Between(from.a, to.a, s), Between(from.b, to.b, s)};
}

double NextTimeAt(const MixPlan& plan, double aTime) {
    double start = plan.keys.empty() ? 0 : plan.keys.front().at;
    return plan.bStart + (aTime - start);
}

std::string Check(const MixPlan& plan, double aDuration, double bDuration) {
    if (plan.next.empty()) return "the plan names no next song";
    if (plan.keys.size() < 2) return "a plan needs two keys or more";
    if (plan.keys.size() > kMaxKeys) return "too many keys";
    for (size_t i = 0; i < plan.keys.size(); i++) {
        const GainKey& key = plan.keys[i];
        if (!std::isfinite(key.at) || key.at < 0) return "a key has no valid time";
        if (!InUnit(key.a) || !InUnit(key.b)) return "a key's volumes must be between 0 and 1";
        if (i > 0 && key.at < plan.keys[i - 1].at) return "the keys are not in time order";
    }
    const GainKey& first = plan.keys.front();
    const GainKey& last = plan.keys.back();
    if (first.a != 1 || first.b != 0) return "the first key must have A at 1 and B at 0";
    if (last.a != 0 || last.b != 1) return "the last key must leave A at 0 and B at 1";
    if (last.at <= first.at) return "the mix has no length";
    if (last.at - first.at > kMaxMixSeconds) return "the mix is longer than 60 seconds";
    if (!std::isfinite(plan.bStart) || plan.bStart < 0) return "B's start is not a time";
    if (aDuration > 0 && last.at >= aDuration) return "the mix goes past the end of the song";
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
    const Json* keys = json.Find("keys");
    if (!keys || !keys->AsArray()) return fail("keys must be an array of [at, a, b]");
    for (const Json& key : *keys->AsArray()) {
        const Json::Array* triple = key.AsArray();
        if (!triple || triple->size() != 3 || !(*triple)[0].AsNumber() || !(*triple)[1].AsNumber() ||
            !(*triple)[2].AsNumber()) {
            return fail("each key must be [at, a, b], three numbers");
        }
        plan.keys.push_back({*(*triple)[0].AsNumber(), *(*triple)[1].AsNumber(), *(*triple)[2].AsNumber()});
        if (plan.keys.size() > kMaxKeys) return fail("too many keys");
    }
    return plan;
}

}  // namespace ampify::automix
