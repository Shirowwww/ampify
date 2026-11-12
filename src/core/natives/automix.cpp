// Natives of the module automix (src/core/prelude/automix.cpp), through the host's AutoMixHost capability
// (src/core/automix/automix_host.hpp). Permission: audio.mix.
//
// Without the capability (the interface process, a host without Apple's player) nothing throws: the state says
// unavailable and a plan is refused with the reason. A missing permission throws PermissionDenied as usual.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../automix/automix_host.hpp"
#include "../json.hpp"

namespace ampify::natives {

namespace {

constexpr const char* kPermission = "audio.mix";

// Times keep a tenth of a millisecond; anything not finite is null.
Json Num(double value) {
    if (!std::isfinite(value)) return Json();
    char text[32];
    std::snprintf(text, sizeof(text), "%.4f", value);
    return Json(std::strtod(text, nullptr));
}

Json SongJson(const std::optional<AutoMixSong>& song) {
    if (!song) return Json();
    return Json(Json::Object{{"name", Json(song->name)},
                             {"id", song->id.empty() ? Json() : Json(song->id)},
                             {"storefront", song->storefront.empty() ? Json() : Json(song->storefront)},
                             {"time", Num(song->time)},
                             {"duration", Num(song->duration)},
                             {"atmos", song->atmos ? Json(*song->atmos) : Json()}});
}

std::string StateJson(const AutoMixState& state) {
    const automix::Status& mix = state.mix;
    Json::Object mixJson{{"phase", Json(std::string(automix::PhaseName(mix.phase)))},
                         {"next", mix.next.empty() ? Json() : Json(mix.next)},
                         {"reason", mix.reason.empty() ? Json() : Json(mix.reason)},
                         {"stepErrorDb", Num(mix.copyErrorDb)},
                         {"seeks", Json(static_cast<double>(mix.seeks))}};
    Json::Object out{{"available", Json(state.available)},
                     {"playing", Json(state.playing)},
                     {"current", SongJson(state.current)},
                     {"next", SongJson(state.next)},
                     {"mix", Json(std::move(mixJson))}};
    return Json(std::move(out)).Dump();
}

// The number of the last plan this element gave, so that it is dropped when the element stops, and only if it is
// still the plan at work (another element may have given one since).
struct AutoMixUse {
    std::uint64_t plan = 0;
};

struct AutoMixNatives : ScriptBridge {
    static AutoMixHost* Mixer(ScriptExtension* ext) { return HostOf(ext).Capability<AutoMixHost>(); }

    static JSValue Text(JSContext* ctx, const std::string& text) { return JS_NewStringLen(ctx, text.data(), text.size()); }

    AMPIFY_NATIVE(ReadState) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, kPermission, &failure)) return failure;
        AutoMixHost* mixer = Mixer(ext);
        return Text(ctx, StateJson(mixer ? mixer->State() : AutoMixState{}));
    }

    AMPIFY_NATIVE(Schedule) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, kPermission, &failure)) return failure;
        std::optional<std::string> text = String(ctx, argc, argv, 0);
        std::string error;
        std::optional<Json> json = text ? Json::Parse(*text, &error) : std::nullopt;
        std::optional<automix::MixPlan> plan = json ? automix::PlanFromJson(*json, &error) : std::nullopt;
        AutoMixHost* mixer = Mixer(ext);
        std::string reason;
        if (!plan) {
            reason = error.empty() ? "the plan is not an object" : error;
        } else if (!mixer) {
            reason = "AutoMix works in Apple Music's audio process: declare \"processes\": [\"engine\"]";
        } else {
            reason = mixer->Schedule(*plan);
            if (reason.empty()) State<AutoMixUse>(ext).plan = mixer->Plans();
        }
        return Text(ctx, reason);
    }

    AMPIFY_NATIVE(Cancel) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, kPermission, &failure)) return failure;
        std::optional<std::string> reason = String(ctx, argc, argv, 0);
        if (AutoMixHost* mixer = Mixer(ext)) mixer->Cancel(reason && !reason->empty() ? *reason : "cancelled");
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineAutoMix(api::NativeScope& scope) {
    scope.Function("automixState", AutoMixNatives::ReadState, 0);
    scope.Function("automixSchedule", AutoMixNatives::Schedule, 1);
    scope.Function("automixCancel", AutoMixNatives::Cancel, 1);
}

void StopAutoMix(ScriptExtension& extension) {
    const std::uint64_t plan = ScriptBridge::State<AutoMixUse>(&extension).plan;
    if (plan == 0) return;
    AutoMixHost* mixer = ScriptBridge::HostOf(&extension).Capability<AutoMixHost>();
    if (mixer && mixer->Plans() == plan) mixer->Cancel("the extension stopped");
}

}  // namespace ampify::natives
