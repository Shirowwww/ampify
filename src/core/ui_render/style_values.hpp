#pragma once

// The values of the style attributes, parsed once for both sides: src/core/ui_model.cpp refuses a bad value with a
// clear message, and src/win/ui_renderer.cpp turns a good one into XAML values. No render tree here, so the model
// does not depend on the renderer. Portable and tested under Linux.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "../json.hpp"
#include "../ui_vocab.hpp"

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
    Rgba color;           // the solid color, the acrylic tint, or the gradient's top
    double tintOpacity = 0.6;
    bool gradient = false;  // "linear-gradient(#top, #bottom)": from `color` at the top to `end` at the bottom
    Rgba end;
};
std::optional<Background> ParseBackground(std::string_view text);

// `padding`: one number for the four sides, or "top,right,bottom,left" as in CSS. Each side is 0 to kMaxPadding.
struct Edges {
    double top = 0;
    double right = 0;
    double bottom = 0;
    double left = 0;
};
using uivocab::kMaxPadding;
std::optional<Edges> ParsePadding(const Json& value);

// `transition`: "opacity 400ms", "all 300ms", "opacity 400ms, scale 0.3s" or "none". Each part animates one property
// on the compositor (XAML's implicit transitions); "all" is opacity, scale and translation. 0 to 10 s each.
struct Transitions {
    std::optional<double> opacityMs;
    std::optional<double> scaleMs;
    std::optional<double> translationMs;
};
using uivocab::kMaxTransitionMs;
std::optional<Transitions> ParseTransition(std::string_view text);

}  // namespace ampify::uirender
