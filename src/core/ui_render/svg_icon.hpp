#pragma once

// The icons of Ampify (Ampify.icons) are small SVG texts with one or more <path d="...">. The native renderer draws
// them as XAML Path shapes filled with the theme's text brush, so they follow the light and dark modes; an
// SvgImageSource would draw them black. This parser keeps only what is safe to put in a XAML attribute.

#include <string>
#include <string_view>
#include <vector>

namespace ampify::uirender {

struct SvgIcon {
    double width = 24;   // from viewBox, 24 x 24 when it is missing
    double height = 24;
    std::vector<std::string> paths;  // the d attributes, each made only of path commands, digits and separators

    bool Usable() const { return !paths.empty(); }
};

// Reads the viewBox and the d attributes of `svg`. A path whose data holds any other character is dropped, so the
// result cannot carry markup into the XAML text it is copied into.
SvgIcon ParseSvgIcon(std::string_view svg);

// True for a Segoe Fluent Icons glyph given as text (one or two UTF-16 code units, not an SVG): drawn with FontIcon.
bool IsGlyphIcon(std::string_view icon);

}  // namespace ampify::uirender
