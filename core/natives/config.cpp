// Natives of the module config (core/prelude/config.cpp). An element reads and writes only its own keys, under
// "extensions.<id>." or "apps.<id>.", and never its activation key.

#include <algorithm>
#include <cctype>

#include "../api/bridge.hpp"
#include "../api/module.hpp"

namespace ampify::natives {

namespace {

// Keys look like `theme.scheme`: letters, digits, '.', '-' and '_', starting with a letter or a digit.
bool ValidConfigKey(const std::string& key) {
    if (key.empty() || key.size() > 64 || key == "enabled") return false;
    if (key.find("..") != std::string::npos) return false;
    if (!std::isalnum(static_cast<unsigned char>(key[0]))) return false;
    return std::all_of(key.begin(), key.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
    });
}

struct ConfigNatives : ScriptBridge {
    AMPIFY_NATIVE(ConfigGet) {
        ScriptExtension* ext = Owner(ctx);
        std::string key = String(ctx, argc, argv, 0).value_or("");
        if (!ValidConfigKey(key)) return Throw(ctx, "TypeError", "invalid setting key: " + key);
        std::optional<std::string> value = HostOf(ext).ReadSetting(ext->ConfigPrefix() + ext->id() + "." + key);
        return value ? JS_NewStringLen(ctx, value->data(), value->size()) : JS_UNDEFINED;
    }

    AMPIFY_NATIVE(ConfigSet) {
        ScriptExtension* ext = Owner(ctx);
        std::string key = String(ctx, argc, argv, 0).value_or("");
        std::string value = String(ctx, argc, argv, 1).value_or("");
        if (!ValidConfigKey(key)) return Throw(ctx, "TypeError", "invalid setting key: " + key);
        HostOf(ext).WriteSetting(ext->ConfigPrefix() + ext->id() + "." + key, value);
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineConfig(api::NativeScope& scope) {
    scope.Function("configGet", ConfigNatives::ConfigGet, 1);
    scope.Function("configSet", ConfigNatives::ConfigSet, 2);
}

}  // namespace ampify::natives
