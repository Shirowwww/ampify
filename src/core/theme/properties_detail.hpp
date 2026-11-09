#pragma once
// What properties.cpp, properties_paint.cpp and properties_effects.cpp share: the value converters.

#include "properties.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <vector>
#include "../text_util.hpp"
#include "color_matrix.hpp"
#include "colour_keys.hpp"
#include "css.hpp"
#include "css_values.hpp"
#include "value_codec.hpp"

namespace ampify::theme::detail {

// The images BitmapImage decodes. An animated GIF plays; SVG needs another source type, so it is refused.
constexpr std::array<std::string_view, 7> kDecorImageTypes{".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".ico"};

// What a `filter` value says so far: a blur, its edges, and the colour filters folded into one matrix.
struct FilterParts {
    std::optional<double> radius;
    std::vector<double> shares;
    ColorMatrix matrix = IdentityMatrix();
};

Conversion Fail(std::string problem);

Conversion Ok(ValueKind kind, std::string text);

// `url(path)` with optional quotes. Nothing when the value is not a url.
std::optional<std::string> UrlArgument(std::string_view raw);

// Strips a trailing `px`, the only unit the engine accepts.
std::optional<double> ParsePixels(std::string_view text);

// A number followed by `%`, as a fraction: `50%` is 0.5.
std::optional<double> ParsePercent(std::string_view text);

// An angle in degrees: `135deg`, or `0` alone, as CSS allows.
std::optional<double> ParseDegrees(std::string_view text);

// `name(arguments)` taking the whole text, the name in any case. Nothing for another form.
std::optional<std::string_view> FunctionArguments(std::string_view raw, std::string_view name);

// Four decimals are plenty for a point relative to an element, and keep the text the same from one load to the next.
std::string FormatFraction(double number);

// A plain colour: `#hex`, `$palette` name (or a Fluent key, Apple Music's colour for it) or `transparent`.
Conversion ConvertColor(std::string_view raw, const ValueContext& context);

// url(image), and an optional fit after it: cover, contain or fill.
Conversion ConvertImage(std::string_view text, const ValueContext& context);

// artwork [cover|contain|fill]: the artwork of the song playing, as Apple Music shows it in its play bar. The backend
// follows it: the image changes with the song.
Conversion ConvertArtwork(std::string_view text, const ValueContext& context);

Conversion ConvertBrush(std::string_view raw, const ValueContext& context);

// acrylic(<tint>, <opacity>): the in-app acrylic of WinUI, a tint over a blur of what lies behind the element.
// liquid-glass(<tint>, <opacity>[, <blur>[, <saturation>]]): Ampify's glass (src/win/glass_brush.*), a lighter blur
// of what lies behind, its colours made more vivid, the tint, and a sheen from the top edge.
Conversion ConvertBackdrop(std::string_view raw, const ValueContext& context);

// As in CSS, only a margin goes below zero. A negative padding, border or corner reaches a template's Border, which
// refuses it at the next layout: WinUI then stops Apple Music.
Conversion ConvertThickness(std::string_view raw, bool allowNegative);

// scale(x[, y]) rotate(<deg>) translate(x[, y]), and scaleX, scaleY, translateX, translateY: one CompositeTransform.
// XAML applies its parts in a fixed order (scale, then rotation, then translation), whatever the order written.
Conversion ConvertTransform(std::string_view raw);

// One or two percentages or keywords, x then y: `50% 50%`, `center`, `left top`. RenderTransformOrigin is relative
// to the element, so pixels are not accepted.
Conversion ConvertTransformOrigin(std::string_view raw);

// A depth in pixels: the ThemeShadow of WinUI grows with the Z of the element. `none` or 0: no shadow.
Conversion ConvertShadow(std::string_view raw);

// Reads one function of `filter` into `parts`; the problem when it does not read.
std::optional<std::string> ReadFilterItem(std::string_view item, const ValueContext& context, FilterParts& parts);

// `filter`: a list of functions, as in CSS. `blur(4px)` is the standard deviation of a Gaussian blur (XAML has none:
// src/win/element_blur.*); Ampify's `edges(30%)` or `edges(10% 50%)` after it blurs only towards the top and bottom,
// over those shares of the height, sharp in the middle (a column of lyrics around the sung line). The colour filters
// (saturate, grayscale, sepia, brightness, contrast, invert, hue-rotate, and Ampify's tint and duotone) fold into one
// colour matrix (color_matrix.hpp), applied in the order written. ValueKind::Blur says how it is written.
Conversion ConvertFilter(std::string_view raw, const ValueContext& context);

// `overflow: hidden` cuts the element to its own rectangle; `clip-path: inset(...)` to a rectangle inset from its
// edges, in pixels and in CSS order. XAML clips to rectangles only: no `round`, circle or polygon.
Conversion ConvertClip(const PropertySpec& spec, std::string_view raw);

// `transform 200ms ease, opacity .15s`: which changes animate, and for how long. The easing is Ampify's own (a quick
// ease-out); easing words and delays are accepted and ignored. Only what XAML animates cheaply is offered, and
// `width`, which glides the widths Apple writes (the smooth progress line): named on its own, never by `all`, since
// it runs on the interface thread, frame by frame.
Conversion ConvertTransition(std::string_view raw);

// `content` of ::before or ::after: an image of the theme folder, as for a background, or none. Text, counters and
// remote addresses are not offered: the decor is an Image element.
Conversion ConvertContent(std::string_view raw, const ValueContext& context);

Conversion ConvertTemplate(std::string_view raw, const ValueContext& context);

}  // namespace ampify::theme::detail
