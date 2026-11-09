#include "color_matrix.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

#include "css_values.hpp"
#include "properties.hpp"

namespace ampify::theme {

namespace {

constexpr int kRows = 5;
constexpr int kColumns = 4;
constexpr double kPi = 3.14159265358979323846;
// Rec. 709 luminance weights, as CSS's grayscale, saturate and hue-rotate use them.
constexpr double kRed = 0.2126;
constexpr double kGreen = 0.7152;
constexpr double kBlue = 0.0722;

// A matrix written as CSS's specification writes it: out[c] = sum(in[k] * m[c][k]) + m[c][3], for c and k over
// r, g, b (alpha kept), turned into Direct2D's rows.
ColorMatrix FromRgb(const std::array<std::array<double, 4>, 3>& m) {
    ColorMatrix out = IdentityMatrix();
    for (int c = 0; c < 3; c++) {
        for (int k = 0; k < 3; k++) out[k * kColumns + c] = static_cast<float>(m[c][k]);
        out[4 * kColumns + c] = static_cast<float>(m[c][3]);
    }
    return out;
}

float Channel(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

}  // namespace

ColorMatrix IdentityMatrix() {
    ColorMatrix m{};
    for (int i = 0; i < kColumns; i++) m[i * kColumns + i] = 1;
    return m;
}

bool IsIdentity(const ColorMatrix& matrix) {
    const ColorMatrix identity = IdentityMatrix();
    for (size_t i = 0; i < matrix.size(); i++) {
        if (std::abs(matrix[i] - identity[i]) > 1e-4f) return false;
    }
    return true;
}

ColorMatrix Then(const ColorMatrix& first, const ColorMatrix& second) {
    // Rows of (r, g, b, a, 1): the constant row of the first matrix goes through the second's weights too.
    ColorMatrix out{};
    for (int row = 0; row < kRows; row++) {
        for (int column = 0; column < kColumns; column++) {
            double sum = row == kRows - 1 ? second[(kRows - 1) * kColumns + column] : 0.0;
            for (int k = 0; k < kColumns; k++) sum += first[row * kColumns + k] * second[k * kColumns + column];
            out[row * kColumns + column] = static_cast<float>(sum);
        }
    }
    return out;
}

ColorMatrix SaturateMatrix(double s) {
    return FromRgb({{{kRed + (1 - kRed) * s, kGreen - kGreen * s, kBlue - kBlue * s, 0},
                     {kRed - kRed * s, kGreen + (1 - kGreen) * s, kBlue - kBlue * s, 0},
                     {kRed - kRed * s, kGreen - kGreen * s, kBlue + (1 - kBlue) * s, 0}}});
}

ColorMatrix GrayscaleMatrix(double amount) {
    return SaturateMatrix(1 - std::clamp(amount, 0.0, 1.0));
}

ColorMatrix SepiaMatrix(double amount) {
    const double a = 1 - std::clamp(amount, 0.0, 1.0);
    return FromRgb({{{0.393 + 0.607 * a, 0.769 - 0.769 * a, 0.189 - 0.189 * a, 0},
                     {0.349 - 0.349 * a, 0.686 + 0.314 * a, 0.168 - 0.168 * a, 0},
                     {0.272 - 0.272 * a, 0.534 - 0.534 * a, 0.131 + 0.869 * a, 0}}});
}

ColorMatrix BrightnessMatrix(double amount) {
    return FromRgb({{{amount, 0, 0, 0}, {0, amount, 0, 0}, {0, 0, amount, 0}}});
}

ColorMatrix ContrastMatrix(double amount) {
    const double offset = 0.5 - 0.5 * amount;
    return FromRgb({{{amount, 0, 0, offset}, {0, amount, 0, offset}, {0, 0, amount, offset}}});
}

ColorMatrix InvertMatrix(double amount) {
    const double a = std::clamp(amount, 0.0, 1.0);
    const double scale = 1 - 2 * a;
    return FromRgb({{{scale, 0, 0, a}, {0, scale, 0, a}, {0, 0, scale, a}}});
}

ColorMatrix HueRotateMatrix(double degrees) {
    const double c = std::cos(degrees * kPi / 180);
    const double s = std::sin(degrees * kPi / 180);
    return FromRgb({{{0.213 + c * 0.787 - s * 0.213, 0.715 - c * 0.715 - s * 0.715, 0.072 - c * 0.072 + s * 0.928, 0},
                     {0.213 - c * 0.213 + s * 0.143, 0.715 + c * 0.285 + s * 0.140, 0.072 - c * 0.072 - s * 0.283, 0},
                     {0.213 - c * 0.213 - s * 0.787, 0.715 - c * 0.715 + s * 0.715, 0.072 + c * 0.928 + s * 0.072, 0}}});
}

ColorMatrix TintMatrix(Argb color, double amount) {
    const double a = std::clamp(amount, 0.0, 1.0);
    auto weight = [&](std::uint8_t channel) { return 1 - a + a * Channel(channel); };
    return FromRgb({{{weight(color.r), 0, 0, 0}, {0, weight(color.g), 0, 0}, {0, 0, weight(color.b), 0}}});
}

ColorMatrix DuotoneMatrix(Argb dark, Argb light) {
    // out = dark + (light - dark) * luminance, one channel at a time.
    std::array<std::array<double, 4>, 3> m{};
    const std::array<float, 3> from{Channel(dark.r), Channel(dark.g), Channel(dark.b)};
    const std::array<float, 3> to{Channel(light.r), Channel(light.g), Channel(light.b)};
    for (int c = 0; c < 3; c++) {
        const double span = to[c] - from[c];
        m[c] = {kRed * span, kGreen * span, kBlue * span, from[c]};
    }
    return FromRgb(m);
}

std::string FormatMatrix(const ColorMatrix& matrix) {
    std::string text;
    for (float number : matrix) {
        if (!text.empty()) text += ',';
        double rounded = std::round(static_cast<double>(number) * 10000) / 10000;
        text += FormatNumber(rounded == 0 ? 0 : rounded);  // no "-0"
    }
    return text;
}

std::optional<ColorMatrix> ParseMatrix(std::string_view text) {
    std::vector<std::string_view> pieces = SplitArguments(text);
    if (pieces.size() != std::tuple_size_v<ColorMatrix>) return std::nullopt;
    ColorMatrix matrix{};
    for (size_t i = 0; i < pieces.size(); i++) {
        std::optional<double> number = ParseNumber(pieces[i]);
        if (!number) return std::nullopt;
        matrix[i] = static_cast<float>(*number);
    }
    return matrix;
}

}  // namespace ampify::theme
