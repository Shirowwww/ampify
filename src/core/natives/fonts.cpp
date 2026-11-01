// Natives of the module fonts (src/core/prelude/fonts.cpp). No permission: the roles are the fonts and sizes the
// interface already shows.

#include <optional>
#include <string>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../font_roles.hpp"
#include "../json.hpp"

namespace ampify::natives {

namespace {

struct FontNatives : ScriptBridge {
    // native.fontRoles(): every role as JSON text (FontRolesJson).
    AMPIFY_NATIVE(Roles) {
        std::string json = PublishedFontRoles();
        return JS_NewStringLen(ctx, json.data(), json.size());
    }

    // native.fontStyle(name): one role as JSON text, with its canonical name under "role", or null when it is unknown.
    AMPIFY_NATIVE(Style) {
        std::optional<std::string> name = String(ctx, argc, argv, 0);
        if (!name) return JS_NULL;
        std::string role = CanonicalFontRole(*name);
        std::optional<Json> roles = Json::Parse(PublishedFontRoles());
        const Json* found = roles ? roles->Find(role) : nullptr;
        if (!found || !found->IsObject()) return JS_NULL;
        Json style = *found;
        style.Set("role", Json(role));
        std::string json = style.Dump();
        return JS_NewStringLen(ctx, json.data(), json.size());
    }
};

}  // namespace

void DefineFonts(api::NativeScope& scope) {
    scope.Function("fontRoles", FontNatives::Roles, 0);
    scope.Function("fontStyle", FontNatives::Style, 1);
}

}  // namespace ampify::natives
