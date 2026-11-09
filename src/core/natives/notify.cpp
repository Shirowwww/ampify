// Natives of the module notify (src/core/prelude/notify.cpp). Permission: notifications.

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../text_util.hpp"
#include "../ui_vocab.hpp"

namespace ampify::natives {

namespace {

struct NotifyNatives : ScriptBridge {
    AMPIFY_NATIVE(Notify) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "notifications", &failure)) return failure;
        std::string titleText = String(ctx, argc, argv, 0).value_or("");
        std::string bodyText = String(ctx, argc, argv, 1).value_or("");
        std::string title(ClipUtf8(titleText, uivocab::kMaxToastTitle));
        std::string body(ClipUtf8(bodyText, uivocab::kMaxToastText));
        HostOf(ext).Notify(ext->id(), title, body);
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineNotify(api::NativeScope& scope) {
    scope.Function("notify", NotifyNatives::Notify, 2);
}

}  // namespace ampify::natives
