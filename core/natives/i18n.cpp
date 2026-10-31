// Natives of the module i18n (core/prelude/i18n.cpp): the display language and Ampify's own texts. No permission.

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../i18n.hpp"

namespace ampify::natives {

namespace {

struct I18nNatives : ScriptBridge {
    // native.i18nKit(key): the English text of `key` in Ampify's own texts, or undefined when there is no such text.
    AMPIFY_NATIVE(KitText) {
        std::string key = String(ctx, argc, argv, 0).value_or("");
        std::string_view found = i18n::Lookup(i18n::KitMessages(), key);
        if (found == key) return JS_UNDEFINED;  // Lookup gives the key back when it has no text
        return JS_NewStringLen(ctx, found.data(), found.size());
    }
};

}  // namespace

void DefineI18n(api::NativeScope& scope) {
    scope.Function("i18nKit", I18nNatives::KitText, 1);
    scope.String("i18nLanguage", i18n::LanguageCode(ScriptBridge::HostOf(&scope.extension()).Locale()));
}

}  // namespace ampify::natives
