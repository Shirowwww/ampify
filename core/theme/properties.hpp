#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "color.hpp"
#include "files.hpp"
#include "tree.hpp"

namespace amk::theme {

// The CSS properties a theme may set, and the XAML property each one maps to.
enum class PropertyKind {
    Brush,       // a colour, `#hex`, `$palette` name, `transparent`, `url(image) [cover|contain|fill]`, or
                 // `linear-gradient(<angle>deg | to <side>, <colour> [<p>%], <colour> [<p>%], ...)`
    Backdrop,    // `acrylic(<colour>, <opacity>)`: an AcrylicBrush, written to Background like a brush
    Length,      // a number of pixels (`px` optional), or `auto`
    Thickness,   // one to four lengths, in CSS order: top right bottom left (corners for corner-radius)
    Visibility,  // visible, or collapsed / hidden / none
    Number,      // a plain number, or a font weight keyword: normal, bold
    Text,        // a font family: a name, optionally in quotes, or `url(fonts/file.ttf#Family Name)`
    Template,    // url(file) of a XAML ControlTemplate
    FlowDirection,  // ltr / left-to-right, or rtl / right-to-left (XAML LeftToRight, RightToLeft)
    Order,       // a whole number from 0: the position of the element among the items of its parent (kOrderProperty)
    Alignment,   // left / center / right / stretch, or top / center / bottom / stretch for vertical-alignment
    Integer,     // a whole number: a Grid row or column (from 0), a span (from 1), a z-index (may be negative)
    Transform,   // scale(x[, y]) rotate(<deg>) translate(x[, y]) and their X / Y forms, or none
    TransformOrigin,  // one or two percentages or keywords (left, center, right, top, bottom)
    Shadow,      // a depth in pixels from 0 to 100 (ThemeShadow and Translation Z), or none
};

struct PropertySpec {
    std::string_view css;   // "background"
    std::string_view xaml;  // "Background"
    PropertyKind kind;
};

std::span<const PropertySpec> KnownProperties();

// Case-insensitive lookup. Nothing for a property the engine does not know.
const PropertySpec* FindProperty(std::string_view css);

// What a conversion needs from the theme that holds the rule.
struct ValueContext {
    const Palette& palette;
    FileSource& files;
    std::string folder;  // absolute theme folder, with '/' separators, prefixed to image paths
    bool allowImages = true;  // false when the assets part is off: url(...) images are refused
};

// The outcome of converting one CSS value: a typed value, or a French explanation for the journal.
struct Conversion {
    std::optional<Value> value;
    std::string problem;
};

Conversion ConvertValue(const PropertySpec& spec, std::string_view raw, const ValueContext& context);

// Shortest text that reads back to the same number, without trailing zeros: 18, 0.5, -4.
std::string FormatNumber(double number);

}  // namespace amk::theme
