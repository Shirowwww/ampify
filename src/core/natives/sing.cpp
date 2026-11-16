// Natives of the module audioSing (src/core/prelude/audio_sing.cpp), through the host's SingHost capability
// (src/core/audio/sing_host.hpp). Permission: audio.sing.
//
// Without the capability nothing throws: set answers false and the state says nothing is known. A missing permission
// throws PermissionDenied as usual.

#include <cmath>
#include <string>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../audio/sing_host.hpp"
#include "../json.hpp"

namespace ampify::natives {

namespace {

constexpr const char* kPermission = "audio.sing";

const char* ModeName(audio::SingMode mode) {
    switch (mode) {
        case audio::SingMode::Light: return "light";
        case audio::SingMode::Studio: return "studio";
        default: return "off";
    }
}

const char* PhaseName(audio::SingPhase phase) {
    switch (phase) {
        case audio::SingPhase::Downloading: return "downloading";
        case audio::SingPhase::Loading: return "loading";
        case audio::SingPhase::Buffering: return "buffering";
        case audio::SingPhase::On: return "on";
        case audio::SingPhase::Failed: return "failed";
        default: return "off";
    }
}

const char* ReasonName(audio::SingReason reason) {
    switch (reason) {
        case audio::SingReason::NoGraphicsCard: return "no-graphics-card";
        case audio::SingReason::DownloadFailed: return "download-failed";
        case audio::SingReason::ModelFailed: return "model-failed";
        case audio::SingReason::DolbyAtmos: return "dolby-atmos";
        case audio::SingReason::AutoMixMixing: return "automix-mixing";
        default: return "none";
    }
}

std::optional<audio::SingMode> ParseMode(const std::string& name) {
    if (name == "off") return audio::SingMode::Off;
    if (name == "light") return audio::SingMode::Light;
    if (name == "studio") return audio::SingMode::Studio;
    return std::nullopt;
}

// Three decimals are plenty for a progress, a voice or a delay, and keep 0.2 as 0.2.
Json Rounded(double value) { return Json(std::round(value * 1000.0) / 1000.0); }

// `available`: whether Studio can run here, null until the engine knows.
Json AvailableJson(audio::SingStudio studio) {
    if (studio == audio::SingStudio::Yes) return Json(true);
    if (studio == audio::SingStudio::No) return Json(false);
    return Json();
}

std::string StateJson(const std::optional<audio::SingRequest>& request, const std::optional<audio::SingStatus>& status) {
    audio::SingRequest asked = request.value_or(audio::SingRequest{});
    audio::SingStatus seen = status.value_or(audio::SingStatus{});
    Json::Object members;
    members.emplace_back("available", AvailableJson(seen.studio));
    members.emplace_back("mode", Json(ModeName(asked.mode)));
    members.emplace_back("voice", Rounded(asked.voice));
    members.emplace_back("phase", Json(PhaseName(seen.phase)));
    members.emplace_back("progress", Rounded(seen.progress));
    members.emplace_back("delay", Rounded(seen.delaySeconds));
    members.emplace_back("reason", Json(ReasonName(seen.reason)));
    return Json(std::move(members)).Dump();
}

struct SingNatives : ScriptBridge {
    static SingHost* Singer(ScriptExtension* ext) { return HostOf(ext).Capability<SingHost>(); }

    AMPIFY_NATIVE(Set) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, kPermission, &failure)) return failure;
        std::optional<audio::SingMode> mode = ParseMode(String(ctx, argc, argv, 0).value_or(""));
        double voice = Number(ctx, argc, argv, 1);
        if (!mode) return Throw(ctx, "RangeError", "sing.set expects a mode: \"off\", \"light\" or \"studio\"");
        if (!std::isfinite(voice) || voice < 0 || voice > 1) {
            return Throw(ctx, "RangeError", "sing.set expects a voice from 0 to 1");
        }
        SingHost* singer = Singer(ext);
        return JS_NewBool(ctx, singer && singer->Set(*mode, voice));
    }

    AMPIFY_NATIVE(ReadState) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, kPermission, &failure)) return failure;
        SingHost* singer = Singer(ext);
        std::string json = singer ? StateJson(singer->Request(), singer->Status()) : StateJson(std::nullopt, std::nullopt);
        return JS_NewStringLen(ctx, json.data(), json.size());
    }
};

}  // namespace

void DefineAudioSing(api::NativeScope& scope) {
    scope.Function("singSet", SingNatives::Set, 2);
    scope.Function("singState", SingNatives::ReadState, 0);
}

}  // namespace ampify::natives
