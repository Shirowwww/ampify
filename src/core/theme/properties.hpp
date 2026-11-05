#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "animation.hpp"
#include "color.hpp"
#include "files.hpp"
#include "tree.hpp"

namespace ampify::theme {

// The CSS properties a theme may set, and the XAML property each one maps to.
enum class PropertyKind {
    Brush,       // a colour, `#hex`, `$palette` name, `transparent`, `url(image) [cover|contain|fill]`, or
                 // `linear-gradient(<angle>deg | to <side>, <colour> [<p>%], <colour> [<p>%], ...)`
    Backdrop,    // `acrylic(<colour>, <opacity>)`: an AcrylicBrush, written to Background like a brush
    Length,      // a number of pixels (`px` optional), or `auto`
    Thickness,   // one to four lengths, in CSS order: top right bottom left (corners for corner-radius)
    Visibility,  // visible, or collapsed / hidden / none; for `display`, none, or block / flex / grid... (visible)
    Number,      // a plain number, or a font weight keyword: normal, bold
    Keyword,     // one word of a fixed list, written as the XAML enumeration member: text-align, font-style,
                 // text-overflow, white-space
    LetterSpacing,  // em (to XAML's thousandths of an em), px (kept as "2px" until the font size is known), or normal
    LineHeight,     // px, or a factor of the font size (1.5, 150%, 1.5em, kept as "1.5em"), or normal (XAML's 0)
    Text,        // a font family: a name, optionally in quotes, or `url(fonts/file.ttf#Family Name)`
    Template,    // url(file) of a XAML ControlTemplate
    FlowDirection,  // ltr / left-to-right, or rtl / right-to-left (XAML LeftToRight, RightToLeft)
    ColorScheme,    // light, dark or normal (XAML ElementTheme Light, Dark, Default): the mode of the element's brushes
    Order,       // a whole number from 0: the position of the element among the items of its parent (kOrderProperty)
    Alignment,   // left / center / right / stretch, or top / center / bottom / stretch for vertical-alignment
    Integer,     // a whole number: a Grid row or column (from 0), a span (from 1), a z-index (may be negative)
    Transform,   // scale(x[, y]) rotate(<deg>) translate(x[, y]) and their X / Y forms, or none
    TransformOrigin,  // one or two percentages or keywords (left, center, right, top, bottom)
    Shadow,      // a depth in pixels from 0 to 100 (ThemeShadow and Translation Z), or none
    Transition,  // `<property> <duration> [easing], ...` for transform, opacity, background or all; or none
    Animation,   // `<name> <duration> [easing] [delay] [count] [direction] [fill], ...` running @keyframes blocks of
                 // the theme (animation.hpp); or none
    Clip,        // `overflow: hidden | clip | visible`, or `clip-path: inset(<lengths>) | none`: a rectangle the element
                 // is cut to (ValueKind::Clip)
    Content,     // the image of ::before or ::after: `url(image) [cover|contain|fill]` of the theme folder (an Image
                 // value), or `none` (an Image value with no text)
};

struct PropertySpec {
    std::string_view css;   // "background"
    std::string_view xaml;  // "Background"
    PropertyKind kind;
};

std::span<const PropertySpec> KnownProperties();

// Case-insensitive lookup. Nothing for a property the engine does not know.
const PropertySpec* FindProperty(std::string_view css);

// For a CSS property the engine does not know, what a theme writes instead ("use backdrop: acrylic(tint, opacity)"),
// or why nothing can stand in for it. Empty when there is no advice.
std::string_view PropertyHint(std::string_view css);

// True for the XAML properties a decorative image of ::before or ::after takes: its content, size, place, opacity,
// transform and animation. The others (colours, fonts, padding...) mean nothing on an image.
bool AppliesToDecor(std::string_view xaml);

// What a conversion needs from the theme that holds the rule.
struct ValueContext {
    const Palette& palette;
    FileSource& files;
    std::string folder;  // absolute theme folder, with '/' separators, prefixed to image paths
    bool allowImages = true;  // false when the assets part is off: url(...) images are refused
    const KeyframeSets* keyframes = nullptr;  // the theme's @keyframes, which `animation` names; none when null
};

// The outcome of converting one CSS value: a typed value, or an English explanation for the log.
struct Conversion {
    std::optional<Value> value;
    std::string problem;
};

Conversion ConvertValue(const PropertySpec& spec, std::string_view raw, const ValueContext& context);

// Shortest text that reads back to the same number, without trailing zeros: 18, 0.5, -4.
std::string FormatNumber(double number);

// A value as a person reads it (ampify theme check, the inspector). A CornerRadius is kept in the order of a thickness,
// bottom-left, top-left, top-right, bottom-right (ConvertThickness), and shown in XAML's own order: top-left,
// top-right, bottom-right, bottom-left. Any other value shows its text.
std::string ShownText(std::string_view property, const Value& value);

// A font size relative to the element's own: "120%" or "1.2em" in user.css, kept as "120%" in the Length value. The
// engine scales each element's size with it, and the backend only ever sees pixels.
bool IsRelativeSize(const Value& value);

// `relative` applied to `size`, a Length in pixels: "16.8" for "120%" of "14". Nothing when `size` is not a positive
// number or `relative` is not a relative size.
std::optional<Value> ScaleFontSize(const Value& relative, const Value& size);

// A value measured against the element's font size, which the engine finishes when it writes: a letter-spacing in
// pixels (a Number "2px") or a line-height factor (a Length "1.5em").
bool NeedsFontSize(const Value& value);

// `value` measured against `fontSize`, a Length in pixels: "2px" of 16 is a CharacterSpacing of 125 (thousandths of an
// em), "1.5em" of 14 a LineHeight of 21. Nothing when `fontSize` is not a positive number or `value` needs none.
std::optional<Value> ApplyFontSize(const Value& value, const Value& fontSize);

// True for the same value, numbers compared as WinUI keeps them: in single precision, so a size written as 16.8 reads
// back as 16.799999237060547, and must not count as a new value on every pass.
bool SameValue(const Value& a, const Value& b);

}  // namespace ampify::theme
