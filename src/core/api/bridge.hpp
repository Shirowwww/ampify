#pragma once

// What the natives of every module share: the extension that owns the runtime, argument conversion, errors, the
// permission check, and access to the kernel services. A natives file derives a struct from ScriptBridge and writes
// its natives as AMPIFY_NATIVE static members, so these helpers read unqualified. Natives run on the JavaScript thread.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <typeinfo>

#include "../script_extension.hpp"

namespace ampify {

struct ScriptBridge {
    static ScriptExtension* Owner(JSContext* ctx) {
        return static_cast<ScriptExtension*>(js::Runtime::FromContext(ctx)->UserData());
    }

    static JSValue Throw(JSContext* ctx, const char* name, const std::string& message) {
        return JS_Throw(ctx, js::NewNamedError(ctx, name, message));
    }

    static std::optional<std::string> String(JSContext* ctx, int argc, JSValueConst* argv, int index) {
        if (index >= argc) return std::nullopt;
        const char* text = JS_ToCString(ctx, argv[index]);
        if (!text) return std::nullopt;
        std::string out(text);
        JS_FreeCString(ctx, text);
        return out;
    }

    static double Number(JSContext* ctx, int argc, JSValueConst* argv, int index) {
        double value = 0;
        if (index < argc) JS_ToFloat64(ctx, &value, argv[index]);
        return value;
    }

    // True when `permission` is declared. Otherwise `failure` gets an Error named PermissionDenied, which says
    // which permission is missing, and the native returns it.
    static bool Require(JSContext* ctx, ScriptExtension* ext, const std::string& permission, JSValue* failure) {
        if (ext->manifest().HasPermission(permission)) return true;
        *failure = Throw(ctx, "PermissionDenied",
                         "missing permission: " + permission + " (declare it in manifest.json)");
        return false;
    }

    // Kernel services.
    static Host& HostOf(ScriptExtension* ext) { return ext->m_host; }
    static Journal& JournalOf(ScriptExtension* ext) { return ext->m_journal; }
    static uint32_t SubmitHttp(ScriptExtension* ext, std::function<HttpResponse()> request) {
        return ext->SubmitHttp(std::move(request));
    }
    static std::chrono::steady_clock::time_point Now(ScriptExtension* ext) { return ext->m_clock(); }

    // State of the built-in modules, kept by ScriptExtension.
    static std::map<std::string, std::string>& StorageOf(ScriptExtension* ext) { return ext->m_storage; }
    static std::multimap<std::chrono::steady_clock::time_point, uint32_t>& TimersOf(ScriptExtension* ext) {
        return ext->m_timers;
    }
    static double& GainDbOf(ScriptExtension* ext) { return ext->m_gainDb; }

    // State that a module keeps per element, created empty on first use and dropped when the runtime is freed.
    // Key it by a type of the module's own, so that two modules never share a slot. Plain C++ data only: it outlives
    // the runtime, so it must not hold JavaScript values.
    template <class T>
    static T& State(ScriptExtension* ext) {
        std::shared_ptr<void>& slot = ext->m_moduleState[std::type_index(typeid(T))];
        if (!slot) slot = std::make_shared<T>();
        return *static_cast<T*>(slot.get());
    }
};

}  // namespace ampify

// A native: called from the prelude as native.<name>(...), on the JavaScript thread.
#define AMPIFY_NATIVE(name)                                                                       \
    static JSValue name([[maybe_unused]] JSContext* ctx, [[maybe_unused]] JSValueConst self, \
                        [[maybe_unused]] int argc, [[maybe_unused]] JSValueConst* argv)
