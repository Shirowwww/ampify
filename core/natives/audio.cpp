// Natives of the module audio (core/prelude/audio.cpp). Permission: audio.gain.

#include <algorithm>
#include <cstdlib>

#include "../api/bridge.hpp"
#include "../api/module.hpp"

namespace amk::natives {

namespace {

constexpr double kDefaultGainCeilingDb = 6.0;

struct AudioNatives : ScriptBridge {
    AMK_NATIVE(SetGain) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.gain", &failure)) return failure;
        // The ceiling belongs to the kernel's settings, never to the extension.
        double ceiling = kDefaultGainCeilingDb;
        if (std::optional<std::string> raw = HostOf(ext).ReadSetting("audio.maxGainDb")) {
            ceiling = std::clamp(std::strtod(raw->c_str(), nullptr), 0.0, 12.0);
        }
        double db = std::clamp(Number(ctx, argc, argv, 0), -ceiling, ceiling);
        GainDbOf(ext) = db;
        HostOf(ext).SetGainDb(ext->id(), db);
        return JS_NewFloat64(ctx, db);
    }
};

}  // namespace

void DefineAudio(api::NativeScope& scope) {
    scope.Function("setGain", AudioNatives::SetGain, 1);
}

// Removes the element's gain when it stops.
void StopAudio(ScriptExtension& extension) {
    if (ScriptBridge::GainDbOf(&extension) != 0) ScriptBridge::HostOf(&extension).SetGainDb(extension.id(), 0);
}

}  // namespace amk::natives
