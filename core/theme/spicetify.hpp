#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

// Spicetify's color.ini, read as is: its keys (main, sidebar, player, text...) are translated to Apple Music's own
// elements and to the Fluent brushes that play the same part. See docs/themes/SPICETIFY.md.
namespace amk::theme {

// The Spicetify name of `key`, lowercase: a current key (main, main-elevated, text...) or the current name of a
// Spicetify 1 key (main_fg is text). Empty when `key` is not a Spicetify colour.
std::string SpicetifyKey(std::string_view key);

// A Fluent brush that plays the part of a Spicetify colour, such as TextFillColorPrimaryBrush for text.
struct SpicetifyResource {
    std::string spicetify;
    std::string resource;
};
const std::vector<SpicetifyResource>& SpicetifyResources();

// The rules that paint Apple Music's elements with the Spicetify colours `defined` holds ($main, $sidebar...), as a
// user.css text. A theme's own user.css goes after it and wins. Empty when no key has a rule.
std::string SpicetifyStylesheet(const std::set<std::string>& defined);

}  // namespace amk::theme
