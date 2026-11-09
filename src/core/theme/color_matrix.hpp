#pragma once
// The colour filters of `filter` (saturate, grayscale, sepia, brightness, contrast, invert, hue-rotate, and Ampify's
// tint and duotone) are each a linear map of a pixel's colour, so a chain of them is one 5x4 matrix: what Direct2D's
// colour matrix effect applies in one pass (src/win/image_effects.*). Built and composed here, where it is tested.

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "color.hpp"

namespace ampify::theme {

// Direct2D's D2D1_MATRIX_5X4_F, row by row: a pixel (r, g, b, a, 1), straight alpha from 0 to 1, times this matrix
// gives the new (r, g, b, a). Rows 0 to 3 weigh the input channels, row 4 adds a constant.
using ColorMatrix = std::array<float, 20>;

ColorMatrix IdentityMatrix();
bool IsIdentity(const ColorMatrix& matrix);

// `first` then `second`, as one matrix.
ColorMatrix Then(const ColorMatrix& first, const ColorMatrix& second);

// The filters of CSS (Filter Effects Level 1), with its amounts: 1 for saturate, brightness and contrast leaves the
// colour as it is, 0 for grayscale, sepia and invert, 0 degrees for hue-rotate.
ColorMatrix SaturateMatrix(double amount);
ColorMatrix GrayscaleMatrix(double amount);
ColorMatrix SepiaMatrix(double amount);
ColorMatrix BrightnessMatrix(double amount);
ColorMatrix ContrastMatrix(double amount);
ColorMatrix InvertMatrix(double amount);
ColorMatrix HueRotateMatrix(double degrees);

// Ampify's own: `tint` multiplies the colour by `color` (white leaves it, its alpha ignored), by `amount` from 0 to 1.
// `duotone` maps the luminance from `dark` (black) to `light` (white), as a two-ink print.
ColorMatrix TintMatrix(Argb color, double amount);
ColorMatrix DuotoneMatrix(Argb dark, Argb light);

// The matrix as the 20 numbers of ValueKind::Blur, comma-separated; and back (nothing unless exactly 20 numbers).
std::string FormatMatrix(const ColorMatrix& matrix);
std::optional<ColorMatrix> ParseMatrix(std::string_view text);

}  // namespace ampify::theme
