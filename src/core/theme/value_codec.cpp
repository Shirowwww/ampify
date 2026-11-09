#include "value_codec.hpp"

#include <array>
#include <charconv>
#include <vector>

#include "properties.hpp"

namespace ampify::theme {

namespace {

constexpr std::string_view kHandlePrefix = "handle:";

struct FitName {
    ImageFit fit;
    std::string_view name;
};
constexpr std::array<FitName, 3> kFitNames{{
    {ImageFit::Fill, "Fill"},
    {ImageFit::Uniform, "Uniform"},
    {ImageFit::UniformToFill, "UniformToFill"},
}};

std::vector<std::string_view> Split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    for (size_t at = text.find(separator); at != std::string_view::npos; at = text.find(separator)) {
        parts.push_back(text.substr(0, at));
        text.remove_prefix(at + 1);
    }
    parts.push_back(text);
    return parts;
}

std::optional<double> Number(std::string_view text) {
    double value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}

}  // namespace

std::string EncodeImage(const ImageRef& image) {
    for (const FitName& entry : kFitNames) {
        if (entry.fit == image.fit) return image.path + "|" + std::string(entry.name);
    }
    return image.path;
}

ImageRef DecodeImage(std::string_view text) {
    size_t bar = text.find('|');
    ImageRef image{std::string(text.substr(0, bar)), ImageFit::Default};
    if (bar == std::string_view::npos) return image;
    std::string_view fit = text.substr(bar + 1);
    for (const FitName& entry : kFitNames) {
        if (entry.name == fit) image.fit = entry.fit;
    }
    return image;
}

std::string EncodeCorners(const Corners& corners) {
    return FormatNumber(corners.bottomLeft) + "," + FormatNumber(corners.topLeft) + "," +
           FormatNumber(corners.topRight) + "," + FormatNumber(corners.bottomRight);
}

std::optional<Corners> DecodeCorners(std::string_view text) {
    std::vector<std::string_view> parts = Split(text, ',');
    if (parts.size() != 4) return std::nullopt;
    std::array<double, 4> values{};
    for (size_t i = 0; i < parts.size(); i++) {
        std::optional<double> value = Number(parts[i]);
        if (!value) return std::nullopt;
        values[i] = *value;
    }
    return Corners{values[1], values[2], values[3], values[0]};
}

std::optional<std::string> CornersInCssOrder(std::string_view text) {
    std::vector<std::string_view> parts = Split(text, ',');
    if (parts.size() != 4) return std::nullopt;
    return std::string(parts[1]) + "," + std::string(parts[2]) + "," + std::string(parts[3]) + "," +
           std::string(parts[0]);
}

std::string EncodeHandle(std::uint64_t handle) {
    return std::string(kHandlePrefix) + std::to_string(handle);
}

std::optional<std::uint64_t> DecodeHandle(std::string_view text) {
    if (!text.starts_with(kHandlePrefix)) return std::nullopt;
    text.remove_prefix(kHandlePrefix.size());
    text = text.substr(0, text.find(';'));
    std::uint64_t handle = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), handle);
    if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return handle;
}

std::string_view HandleColour(std::string_view text) {
    size_t colour = text.find(';');
    return colour == std::string_view::npos ? std::string_view() : text.substr(colour + 1);
}

std::optional<bool> DecodeVisible(std::string_view text) {
    if (text == kVisible) return true;
    if (text == kCollapsed) return false;
    return std::nullopt;
}

}  // namespace ampify::theme
