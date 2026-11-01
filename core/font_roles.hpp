#pragma once

// Font roles: a name for the font of a kind of text (title, body...), as Spicetify's getFontStyle gives it. Themes set
// them in user.css (core/theme/fonts.hpp); extensions read them with Ampify.fonts (core/prelude/fonts.cpp).
//
// The theme service publishes the roles of what is applied (theme and snippets) from the interface thread when it
// changes, and the JavaScript thread reads them. Until then, and in AMPLibraryAgent.exe, which has no theme service,
// the roles are the defaults.

#include <string>
#include <string_view>
#include <vector>

namespace ampify {

// What a role resolves to. `fromTheme` is true when at least one part comes from the theme (or a snippet).
struct FontStyle {
    std::string role;    // canonical name, such as "title"
    std::string family;  // FontFamily, such as "Segoe UI Variable Display"
    std::string weight;  // FontWeight as a number, such as "600"
    std::string size;    // FontSize in pixels, such as "28"
    bool fromTheme = false;
};

// Lower case; `text` is another name for `body`.
std::string CanonicalFontRole(std::string_view role);

// The WinUI 3 type ramp of Windows 11, which a role keeps when the theme does not set it.
const std::vector<FontStyle>& DefaultFontRoles();

// The roles as JSON, by name: {"body":{"family":"…","weight":400,"size":14,"fromTheme":false},…}.
std::string FontRolesJson(const std::vector<FontStyle>& roles);

// The theme service's side and the extensions' side. Publishing an empty text goes back to the defaults.
void PublishFontRoles(std::string json);
std::string PublishedFontRoles();

}  // namespace ampify
