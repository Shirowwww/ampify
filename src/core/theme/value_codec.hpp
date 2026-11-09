#pragma once
// The text forms of the theme::Value kinds that the engine writes and a backend reads back (tree.hpp): each has one
// encoder and one decoder here, so the engine, the inspector and the Windows backend cannot drift apart. Portable.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ampify::theme {

// How an image fills its element (`url(...) cover|contain|fill`); Default leaves it to the element that shows it.
enum class ImageFit { Default, Fill, Uniform, UniformToFill };

// ValueKind::Image: a path (or kArtworkImage) and its fit, written "path|Fit".
struct ImageRef {
    std::string path;
    ImageFit fit = ImageFit::Default;
    bool operator==(const ImageRef&) const = default;
};
std::string EncodeImage(const ImageRef& image);
ImageRef DecodeImage(std::string_view text);

// The corners of ValueKind::Thickness for CornerRadius. The text keeps CSS's box order read as left, top, right,
// bottom (ConvertThickness), which for `corner-radius: a b c d` is bottom-left, top-left, top-right, bottom-right.
struct Corners {
    double topLeft = 0;
    double topRight = 0;
    double bottomRight = 0;
    double bottomLeft = 0;
    bool operator==(const Corners&) const = default;
};
std::string EncodeCorners(const Corners& corners);
std::optional<Corners> DecodeCorners(std::string_view text);
// The same text in CSS's order (top-left, top-right, bottom-right, bottom-left), as a theme writes it; nullopt when it
// does not hold four values.
std::optional<std::string> CornersInCssOrder(std::string_view text);

// ValueKind::Opaque: "handle:N", optionally followed by ";#AARRGGBB", the colour of a plain brush kept as an object.
std::string EncodeHandle(std::uint64_t handle);
std::optional<std::uint64_t> DecodeHandle(std::string_view text);
// The colour after the handle, empty when there is none.
std::string_view HandleColour(std::string_view text);

// ValueKind::Visibility: exactly one of these two.
inline constexpr std::string_view kVisible = "Visible";
inline constexpr std::string_view kCollapsed = "Collapsed";
// True for kVisible, false for kCollapsed, nullopt for any other text.
std::optional<bool> DecodeVisible(std::string_view text);

}  // namespace ampify::theme
