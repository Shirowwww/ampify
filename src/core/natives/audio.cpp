// Natives of the module audio (src/core/prelude/audio.cpp). Permission: audio.gain.

#include <algorithm>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../gain_board.hpp"

namespace ampify::natives {

namespace {

struct AudioNatives : ScriptBridge {
    AMPIFY_NATIVE(SetGain) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.gain", &failure)) return failure;
        // The ceiling belongs to the kernel's settings, never to the extension.
        double ceiling = GainCeilingDb(HostOf(ext).ReadSetting(kGainCeilingSetting));
        double db = std::clamp(Number(ctx, argc, argv, 0), -ceiling, ceiling);
        if (!HostOf(ext).SetGainDb(ext->id(), db)) return JS_NewFloat64(ctx, 0);  // the host logs it once
        GainDbOf(ext) = db;
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

}  // namespace ampify::natives
