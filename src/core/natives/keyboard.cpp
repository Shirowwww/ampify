// Natives of the module keyboard (src/core/prelude/keyboard.cpp). Permission: ui.keyboard. The host calls the loader's
// DeliverKey when a shortcut is pressed, which reaches dispatch.key.

#include "keyboard.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"

namespace ampify::natives {

namespace {

struct KeyboardNatives : ScriptBridge {
    AMPIFY_NATIVE(BindKey) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.keyboard", &failure)) return failure;
        return JS_NewBool(ctx, HostOf(ext).RegisterKey(ext->id(), String(ctx, argc, argv, 0).value_or("")));
    }

    // native.unbindKey(combo): true when the host gave the shortcut back to Windows.
    AMPIFY_NATIVE(UnbindKey) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.keyboard", &failure)) return failure;
        KeyUnbindHost* host = HostOf(ext).Capability<KeyUnbindHost>();
        return JS_NewBool(ctx, host && host->UnregisterKey(ext->id(), String(ctx, argc, argv, 0).value_or("")));
    }
};

}  // namespace

void DefineKeyboard(api::NativeScope& scope) {
    scope.Function("bindKey", KeyboardNatives::BindKey, 1);
    scope.Function("unbindKey", KeyboardNatives::UnbindKey, 1);
}

void StopKeyboard(ScriptExtension& extension) { ScriptBridge::HostOf(&extension).UnregisterKeys(extension.id()); }

}  // namespace ampify::natives
