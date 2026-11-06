// Natives of the module i18n (src/core/prelude/i18n.cpp): the display language and Ampify's own texts. No permission.

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

    // native.i18nKitShown(key): the same text in the display language, for the defaults of Ampify's components.
    AMPIFY_NATIVE(KitTextShown) {
        std::string shown = i18n::Text(String(ctx, argc, argv, 0).value_or(""));
        return JS_NewStringLen(ctx, shown.data(), shown.size());
    }
};

}  // namespace

void DefineI18n(api::NativeScope& scope) {
    scope.Function("i18nKit", I18nNatives::KitText, 1);
    scope.Function("i18nKitShown", I18nNatives::KitTextShown, 1);
    scope.String("i18nLanguage", i18n::LanguageCode(ScriptBridge::HostOf(&scope.extension()).Locale()));
}

}  // namespace ampify::natives
