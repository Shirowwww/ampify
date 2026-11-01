// Natives of the module storage (src/core/prelude/storage.cpp): one JSON object per element, kept by the host, within
// kStorageQuotaBytes. Permission: storage.

#include <map>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"

namespace ampify::natives {

namespace {

std::map<std::string, std::string> ParseStorage(const std::optional<std::string>& text) {
    std::map<std::string, std::string> values;
    if (!text) return values;
    std::optional<Json> root = Json::Parse(*text);
    if (!root || !root->AsObject()) return values;
    for (const auto& [key, value] : *root->AsObject()) values[key] = value.Dump();
    return values;
}

std::string DumpStorage(const std::map<std::string, std::string>& values) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, raw] : values) {
        if (!first) out += ',';
        first = false;
        out += Json(key).Dump() + ':' + raw;
    }
    return out + '}';
}

struct StorageNatives : ScriptBridge {
    static void Persist(ScriptExtension* ext) { HostOf(ext).WriteStorage(ext->id(), DumpStorage(StorageOf(ext))); }

    AMPIFY_NATIVE(StorageGet) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "storage", &failure)) return failure;
        auto& storage = StorageOf(ext);
        auto found = storage.find(String(ctx, argc, argv, 0).value_or(""));
        if (found == storage.end()) return JS_UNDEFINED;
        return JS_NewStringLen(ctx, found->second.data(), found->second.size());
    }

    AMPIFY_NATIVE(StorageSet) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "storage", &failure)) return failure;
        std::string key = String(ctx, argc, argv, 0).value_or("");
        std::string raw = String(ctx, argc, argv, 1).value_or("");
        if (key.empty() || key.size() > 128) return Throw(ctx, "TypeError", "invalid storage key");
        if (!Json::Parse(raw)) return Throw(ctx, "TypeError", "storage value is not JSON");

        std::map<std::string, std::string> next = StorageOf(ext);
        next[key] = raw;
        if (DumpStorage(next).size() > kStorageQuotaBytes) {
            return Throw(ctx, "QuotaExceeded", "storage full: " + std::to_string(kStorageQuotaBytes) + " bytes at most");
        }
        StorageOf(ext) = std::move(next);
        Persist(ext);
        return JS_UNDEFINED;
    }

    AMPIFY_NATIVE(StorageRemove) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "storage", &failure)) return failure;
        if (StorageOf(ext).erase(String(ctx, argc, argv, 0).value_or("")) > 0) Persist(ext);
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineStorage(api::NativeScope& scope) {
    scope.Function("storageGet", StorageNatives::StorageGet, 1);
    scope.Function("storageSet", StorageNatives::StorageSet, 2);
    scope.Function("storageRemove", StorageNatives::StorageRemove, 1);
}

// Loads the element's values before main.js runs.
void StartStorage(ScriptExtension& extension) {
    ScriptBridge::StorageOf(&extension) = ParseStorage(ScriptBridge::HostOf(&extension).ReadStorage(extension.id()));
}

}  // namespace ampify::natives
