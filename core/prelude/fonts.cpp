// Module fonts: Ampify.fonts, the font roles of the applied theme (the counterpart of Spicetify's getFontStyle).
// Natives: core/natives/fonts.cpp. The roles and their defaults: core/font_roles.hpp.

namespace ampify::prelude {

extern const char kFonts[];
const char kFonts[] = R"AMPIFYJS(
  Ampify.fonts = {
    roles() {
      return JSON.parse(native.fontRoles());
    },
    style(name) {
      if (typeof name !== "string" || name === "") {
        throw new TypeError("fonts.style expects the name of a role, such as \"title\"");
      }
      const json = native.fontStyle(name);
      return json === null ? null : JSON.parse(json);
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
