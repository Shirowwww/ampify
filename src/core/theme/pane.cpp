#include "pane.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace ampify::theme {

namespace {

constexpr std::string_view kPaneName = "PaneRoot";
constexpr std::string_view kSplitViewType = "SplitView";

std::optional<double> ParseNumber(std::string_view text) {
    double number = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return number;
}

}  // namespace

int PaneOwner(const std::vector<NodeInfo>& nodes, int position) {
    if (position < 0 || position >= static_cast<int>(nodes.size())) return -1;
    const NodeInfo& node = nodes[position];
    if (node.name != kPaneName || node.parent < 0) return -1;
    const int owner = nodes[node.parent].parent;
    return owner >= 0 && nodes[owner].type == kSplitViewType ? owner : -1;
}

bool TemplatePlacesPane(std::string_view property) {
    return property == "HorizontalAlignment" || property == "Grid.Column" || property == "Grid.ColumnSpan";
}

std::optional<double> FittedPaneWidth(std::string_view openPaneLength, std::string_view margin) {
    const std::optional<double> length = ParseNumber(openPaneLength);
    if (!length) return std::nullopt;
    std::array<double, 4> sides{};
    size_t start = 0;
    for (size_t side = 0; side < sides.size(); side++) {
        const size_t comma = margin.find(',', start);
        const bool last = side + 1 == sides.size();
        if (last != (comma == std::string_view::npos)) return std::nullopt;
        std::optional<double> number = ParseNumber(margin.substr(start, last ? std::string_view::npos : comma - start));
        if (!number) return std::nullopt;
        sides[side] = *number;
        start = comma + 1;
    }
    return std::max(0.0, *length - sides[0] - sides[2]);
}

}  // namespace ampify::theme
