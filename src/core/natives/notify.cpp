// Natives of the module notify (src/core/prelude/notify.cpp). Permission: notifications.

#include "../api/bridge.hpp"
#include "../api/module.hpp"

namespace ampify::natives {

namespace {

constexpr size_t kMaxNotifyTitle = 120;
constexpr size_t kMaxNotifyText = 500;

struct NotifyNatives : ScriptBridge {
    AMPIFY_NATIVE(Notify) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "notifications", &failure)) return failure;
        std::string title = String(ctx, argc, argv, 0).value_or("").substr(0, kMaxNotifyTitle);
        std::string body = String(ctx, argc, argv, 1).value_or("").substr(0, kMaxNotifyText);
        HostOf(ext).Notify(ext->id(), title, body);
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineNotify(api::NativeScope& scope) {
    scope.Function("notify", NotifyNatives::Notify, 2);
}

}  // namespace ampify::natives
