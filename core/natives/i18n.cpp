// Natives of the module i18n (core/prelude/i18n.cpp): the kit language and the kit's own texts. No permission.

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../i18n.hpp"
#include "kit_language.hpp"

namespace amk::natives {

namespace {

struct I18nNatives : ScriptBridge {
    // native.i18nKit(key): the kit text of `key` in the kit language, or undefined when the kit has no such text.
    AMK_NATIVE(KitText) {
        ScriptExtension* ext = Owner(ctx);
        std::string key = String(ctx, argc, argv, 0).value_or("");
        i18n::Language language = KitLanguage(HostOf(ext));
        std::string_view found = i18n::Lookup(i18n::KitMessages(), language, key);
        if (found == key) return JS_UNDEFINED;  // Lookup gives the key back when it has no text
        return JS_NewStringLen(ctx, found.data(), found.size());
    }
};

}  // namespace

void DefineI18n(api::NativeScope& scope) {
    scope.Function("i18nKit", I18nNatives::KitText, 1);
    scope.String("i18nLanguage",
                 std::string(i18n::Code(KitLanguage(ScriptBridge::HostOf(&scope.extension())))));
}

}  // namespace amk::natives
