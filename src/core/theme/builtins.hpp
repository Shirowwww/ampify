#pragma once

// What Ampify gives every theme on top of the CSS subset:
// - stable names for Apple Music's zones, written as CSS classes (`.sidebar`, `.playbar`, `.card`...). Apple Music
//   names few of its elements and the paths to them are long; a class stands for that path, so a theme reads well and
//   one table here follows Apple Music's updates for every theme (as Spicetify's css-map.json does for Spotify);
// - ready-made layouts, imported with `@import "ampify:<name>";` (floating panels, the playback bar at the bottom),
//   tuned once so that every theme gets aligned edges and even gaps. They are plain CSS in layouts/, built into the
//   kernel by cmake/layouts.cmake.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "css.hpp"

namespace ampify::theme {

// The selectors a class stands for (several when the zone has several forms), or nothing for an unknown class.
std::optional<std::vector<std::string>> ClassSelectors(std::string_view name);
std::vector<std::string> ClassNames();

// Replaces each class of `selector` by what it stands for: one selector per combination. A class starts a part of
// the selector (`.card:hover`, `.sidebar Button`); a dot inside a name (`Microsoft.UI.Xaml.Controls.Grid`) is left
// alone. Nothing, with `problem` set, for an unknown class or a class glued to a type (`Grid.card`).
std::optional<std::vector<std::string>> ExpandClasses(std::string_view selector, std::string* problem = nullptr);

// The CSS of a ready-made layout, or nothing for an unknown name.
std::optional<std::string> BuiltinStylesheet(std::string_view name);
std::vector<std::string> BuiltinStylesheetNames();

// The classes and the layouts above, for the parser.
const StylesheetNames& ZoneNames();

// A theme's or a snippet's user.css, its classes and layouts given by the table above.
inline Stylesheet ParseThemeStylesheet(std::string_view css) {
    return ParseStylesheet(css, ZoneNames());
}

}  // namespace ampify::theme
