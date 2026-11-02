// Natives of the module core (src/core/prelude/core.cpp): the journal, the shared permission check, and the constants
// behind Ampify.version, kitApi, process and locale.

#include "../api/bridge.hpp"
#include "../api/module.hpp"

namespace ampify::natives {

namespace {

struct CoreNatives : ScriptBridge {
    using ScriptBridge::Require;

    AMPIFY_NATIVE(Log) {
        ScriptExtension* ext = Owner(ctx);
        std::string message = String(ctx, argc, argv, 0).value_or("");
        JournalOf(ext).Write("[" + ext->id() + "] " + message);
        return JS_UNDEFINED;
    }

    // native.fault(message): an error the prelude caught in a handler. Logged, and counted towards stopping the element.
    AMPIFY_NATIVE(Fault) {
        ScriptExtension* ext = Owner(ctx);
        std::string message = String(ctx, argc, argv, 0).value_or("");
        JournalOf(ext).Write("[" + ext->id() + "] " + message);
        CountFailure(ext, message);
        return JS_UNDEFINED;
    }

    // native.require(permission): throws PermissionDenied when the manifest does not declare it.
    AMPIFY_NATIVE(Require) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, String(ctx, argc, argv, 0).value_or(""), &failure)) return failure;
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineCore(api::NativeScope& scope) {
    scope.Function("log", CoreNatives::Log, 1);
    scope.Function("fault", CoreNatives::Fault, 1);
    scope.Function("require", CoreNatives::Require, 1);
    scope.String("version", kKernelVersion);
    scope.String("kitApi", kKitApi);
    scope.String("process", ScriptBridge::HostOf(&scope.extension()).Process());
    scope.String("locale", ScriptBridge::HostOf(&scope.extension()).Locale());
}

}  // namespace ampify::natives
