#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "theme.hpp"

namespace ampify::theme {

// Font roles: a name for the font of a kind of text (title, body...), as Spicetify's getFontStyle gives it.
//
// A theme sets a role in a `:root` block of user.css:
//   :root {
//       --font-title: "Segoe UI Variable Display";
//       --font-title-weight: 700;
//       --font-title-size: 30px;
//   }
// Its rules use it with `font-family: var(--font-title)`, or all three parts at once with `font-role: title`.
// A role the theme does not set keeps the Windows 11 type ramp below. Ampify.fonts.style(name) will read FontRolesJson.

// What a role resolves to. `fromTheme` is true when at least one part comes from the theme (or a snippet).
struct FontStyle {
    std::string role;    // canonical name, such as "title"
    std::string family;  // FontFamily, such as "Segoe UI Variable Display"
    std::string weight;  // FontWeight as a number, such as "600"
    std::string size;    // FontSize in pixels, such as "28"
    bool fromTheme = false;
};

// Lower case, and the French names map to the English ones: titre -> title, texte -> body, sous-titre -> subtitle,
// legende or legende with an accent -> caption.
std::string CanonicalFontRole(std::string_view role);

// The role as the layers set it: for each part, the first layer that sets it wins, then the default. `layers` go from
// the strongest to the weakest (a snippet before the theme). Nothing when no layer and no default knows the role.
std::optional<FontStyle> ResolveFontRole(const std::vector<const Theme*>& layers, std::string_view role);

// The same for one theme, which may be null (no theme: the defaults).
std::optional<FontStyle> ResolveFontRole(const Theme* theme, std::string_view role);

// The default roles, then the roles only the theme sets, by name.
std::vector<std::string> FontRoleNames(const Theme* theme);

// Every role as JSON, for Ampify.fonts: {"body":{"family":"…","weight":400,"size":14,"fromTheme":false},…}.
std::string FontRolesJson(const Theme* theme);

}  // namespace ampify::theme
