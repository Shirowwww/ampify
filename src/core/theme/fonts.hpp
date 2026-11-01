#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../font_roles.hpp"
#include "theme.hpp"

namespace ampify::theme {

// The font roles a theme sets (src/core/font_roles.hpp has the names, the defaults and what extensions read).
//
// A theme sets a role in a `:root` block of user.css:
//   :root {
//       --font-title: "Segoe UI Variable Display";
//       --font-title-weight: 700;
//       --font-title-size: 30px;
//   }
// Its rules use it with `font-family: var(--font-title)`, or all three parts at once with `font-role: title`.
// A role the theme does not set keeps the Windows 11 type ramp (DefaultFontRoles).

using ampify::FontStyle;

// The role as the layers set it: for each part, the first layer that sets it wins, then the default. `layers` go from
// the strongest to the weakest (a snippet before the theme). Nothing when no layer and no default knows the role.
std::optional<FontStyle> ResolveFontRole(const std::vector<const Theme*>& layers, std::string_view role);

// The same for one theme, which may be null (no theme: the defaults).
std::optional<FontStyle> ResolveFontRole(const Theme* theme, std::string_view role);

// The default roles, then the roles only the theme sets, by name.
std::vector<std::string> FontRoleNames(const Theme* theme);

// Every role of the theme as JSON (ampify::FontRolesJson), which the theme service publishes for Ampify.fonts.
std::string FontRolesJson(const Theme* theme);

}  // namespace ampify::theme
