#include "style.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "../text_util.hpp"

namespace ampify::uirender {

namespace {

constexpr std::size_t kMaxTransitionParts = 4;

std::vector<std::string_view> Split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t at = text.find(separator, start);
        parts.push_back(TrimSpace(text.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start)));
        if (at == std::string_view::npos) return parts;
        start = at + 1;
    }
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// A plain decimal number ("12", "0.6", ".5"), without sign, exponent or trailing text.
std::optional<double> Decimal(std::string_view text) {
    if (text.empty() || text.size() > 16) return std::nullopt;
    bool digit = false;
    bool dot = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            digit = true;
        } else if (c == '.' && !dot) {
            dot = true;
        } else {
            return std::nullopt;
        }
    }
    if (!digit) return std::nullopt;
    return std::stod(std::string(text));
}

std::optional<double> Duration(std::string_view text) {
    if (text.size() > 2 && text.substr(text.size() - 2) == "ms") return Decimal(text.substr(0, text.size() - 2));
    if (text.size() > 1 && text.back() == 's') {
        std::optional<double> seconds = Decimal(text.substr(0, text.size() - 1));
        if (seconds) return *seconds * 1000;
    }
    return std::nullopt;
}

}  // namespace

std::optional<Rgba> ParseColor(std::string_view text) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#') return std::nullopt;
    std::uint8_t bytes[4] = {255, 0, 0, 0};
    int first = text.size() == 9 ? 0 : 1;
    for (int index = first; index < 4; index++) {
        std::size_t at = 1 + static_cast<std::size_t>(index - first) * 2;
        int high = HexDigit(text[at]);
        int low = HexDigit(text[at + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        bytes[index] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return Rgba{bytes[0], bytes[1], bytes[2], bytes[3]};
}

std::optional<Background> ParseBackground(std::string_view text) {
    text = TrimSpace(text);
    Background background;
    if (text == "acrylic") {
        background.acrylic = true;
        background.themed = true;
        return background;
    }
    constexpr std::string_view kOpen = "acrylic(";
    if (text.substr(0, kOpen.size()) == kOpen && text.size() > kOpen.size() && text.back() == ')') {
        std::vector<std::string_view> parts = Split(text.substr(kOpen.size(), text.size() - kOpen.size() - 1), ',');
        if (parts.size() > 2) return std::nullopt;
        std::optional<Rgba> tint = ParseColor(parts[0]);
        if (!tint) return std::nullopt;
        background.acrylic = true;
        background.color = *tint;
        if (parts.size() == 2) {
            std::optional<double> opacity = Decimal(parts[1]);
            if (!opacity || *opacity > 1) return std::nullopt;
            background.tintOpacity = *opacity;
        }
        return background;
    }
    std::optional<Rgba> color = ParseColor(text);
    if (!color) return std::nullopt;
    background.color = *color;
    return background;
}

std::optional<Edges> ParsePadding(const Json& value) {
    auto inRange = [](double side) { return std::isfinite(side) && side >= 0 && side <= kMaxPadding; };
    if (const double* number = value.AsNumber()) {
        if (!inRange(*number)) return std::nullopt;
        return Edges{*number, *number, *number, *number};
    }
    const std::string* text = value.AsString();
    if (!text) return std::nullopt;
    std::vector<std::string_view> parts = Split(*text, ',');
    if (parts.size() != 4) return std::nullopt;
    double sides[4] = {};
    for (std::size_t index = 0; index < 4; index++) {
        std::optional<double> side = Decimal(parts[index]);
        if (!side || !inRange(*side)) return std::nullopt;
        sides[index] = *side;
    }
    return Edges{sides[0], sides[1], sides[2], sides[3]};
}

std::optional<Transitions> ParseTransition(std::string_view text) {
    text = TrimSpace(text);
    Transitions transitions;
    if (text == "none") return transitions;
    std::vector<std::string_view> parts = Split(text, ',');
    if (parts.size() > kMaxTransitionParts) return std::nullopt;
    for (std::string_view part : parts) {
        std::size_t space = part.find(' ');
        if (space == std::string_view::npos) return std::nullopt;
        std::string_view name = part.substr(0, space);
        std::optional<double> ms = Duration(TrimSpace(part.substr(space + 1)));
        if (!ms || *ms > kMaxTransitionMs) return std::nullopt;
        if (name == "opacity" || name == "all") transitions.opacityMs = *ms;
        if (name == "scale" || name == "all") transitions.scaleMs = *ms;
        if (name == "translation" || name == "all") transitions.translationMs = *ms;
        if (name != "opacity" && name != "scale" && name != "translation" && name != "all") return std::nullopt;
    }
    return transitions;
}

int GridColumns(const RenderNode& node) {
    double columns = node.Number("columns", 2);
    if (!std::isfinite(columns)) return 2;
    if (columns < 1) return 1;
    if (columns > kMaxGridColumns) return kMaxGridColumns;
    return static_cast<int>(columns);
}

GridCell GridCellOf(std::size_t index, int columns) {
    std::size_t count = static_cast<std::size_t>(columns < 1 ? 1 : columns);
    return GridCell{static_cast<int>(index / count), static_cast<int>(index % count)};
}

std::string ButtonVariantOf(const RenderNode& node) {
    if (node.type == "tab") return node.Bool("selected", false) ? "primary" : "tertiary";
    if (node.type == "menu") return "secondary";
    return node.Text("variant", "secondary");
}

Layout LayoutOf(const RenderNode& node) {
    if (node.type == "layer") return Layout::Layer;
    if (node.type == "grid") return Layout::Columns;
    if (node.type == "card" || node.type == "tabs") return Layout::Panel;
    if (node.type != "stack" && node.type != "list" && node.type != "row") return Layout::None;
    for (const RenderNode& child : node.children) {
        if (node.type != "list" && child.Bool("grow", false)) return Layout::Grid;
    }
    if (node.type == "row" && node.children.size() == 2) return Layout::Pair;
    return Layout::Panel;
}

}  // namespace ampify::uirender
