#pragma once

// The style attributes of the interface primitives, parsed once for both sides: core/ui_model.cpp refuses a bad value
// with a clear message, and win/ui_renderer.cpp turns a good one into XAML values. Portable and tested under Linux.

#include <cstdint>
#include <optional>
#include <string_view>

#include "../json.hpp"
#include "render_tree.hpp"

namespace ampify::uirender {

struct Rgba {
    std::uint8_t a = 255;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

// "#RRGGBB" or "#AARRGGBB" (the alpha first, as XAML writes it).
std::optional<Rgba> ParseColor(std::string_view text);

// `background`: a color, "acrylic" (the theme's in-app acrylic), or "acrylic(#AARRGGBB, 0.6)" (tint and tint
// opacity; the opacity may be left out).
struct Background {
    bool acrylic = false;
    bool themed = false;  // plain "acrylic": the tint comes from the theme
    Rgba color;           // the solid color, or the acrylic tint
    double tintOpacity = 0.6;
};
std::optional<Background> ParseBackground(std::string_view text);

// `padding`: one number for the four sides, or "top,right,bottom,left" as in CSS. Each side is 0 to kMaxPadding.
struct Edges {
    double top = 0;
    double right = 0;
    double bottom = 0;
    double left = 0;
};
constexpr double kMaxPadding = 1000;
std::optional<Edges> ParsePadding(const Json& value);

// `transition`: "opacity 400ms", "all 300ms", "opacity 400ms, scale 0.3s" or "none". Each part animates one property
// on the compositor (XAML's implicit transitions); "all" is opacity, scale and translation. 0 to 10 s each.
struct Transitions {
    std::optional<double> opacityMs;
    std::optional<double> scaleMs;
    std::optional<double> translationMs;
};
constexpr double kMaxTransitionMs = 10000;
std::optional<Transitions> ParseTransition(std::string_view text);

// How a container lays out its children on the Windows side. A change of layout rebuilds the native container.
enum class Layout {
    None,   // not a container
    Panel,  // StackPanel: stack, list, card's body, a row without a growing child and not of two children
    Pair,   // a row of exactly two children and none that grows: Grid (Star, Auto), the second on the right
    Grid,   // a stack or row with a growing child: Grid, one Auto or Star cell per child
    Layer,  // layer: Grid of one cell, the children stacked on top of each other
};
Layout LayoutOf(const RenderNode& node);

}  // namespace ampify::uirender
