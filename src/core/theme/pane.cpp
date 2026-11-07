#include "pane.hpp"

#include <algorithm>
#include <array>

#include "css_values.hpp"

namespace ampify::theme {

namespace {

constexpr std::string_view kPaneName = "PaneRoot";
constexpr std::string_view kSplitViewType = "SplitView";
constexpr std::string_view kSidebarSplitView = "RootSplitView";
constexpr std::string_view kColumnName = "PaneContentGrid";

}  // namespace

int PaneOwner(const std::vector<NodeInfo>& nodes, int position) {
    if (position < 0 || position >= static_cast<int>(nodes.size())) return -1;
    const NodeInfo& node = nodes[position];
    if (node.name != kPaneName || node.parent < 0) return -1;
    const int owner = nodes[node.parent].parent;
    return owner >= 0 && nodes[owner].type == kSplitViewType ? owner : -1;
}

bool SidebarPaneLayer(const std::vector<NodeInfo>& nodes, int position) {
    if (position < 0 || position >= static_cast<int>(nodes.size())) return false;
    auto sidebarPane = [&](int at) {
        const int owner = PaneOwner(nodes, at);
        return owner >= 0 && nodes[owner].name == kSidebarSplitView;
    };
    const NodeInfo& node = nodes[position];
    if (sidebarPane(position)) return true;
    if (node.parent >= 0 && node.type == "Border" && sidebarPane(node.parent)) return true;
    if (node.name != kColumnName || node.parent < 0) return false;
    const NodeInfo& border = nodes[node.parent];
    return border.type == "Border" && border.parent >= 0 && sidebarPane(border.parent);
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

std::array<CornerEllipse, 4> InnerCorners(const std::array<double, 4>& radius, const std::array<double, 4>& border) {
    const double left = border[0], top = border[1], right = border[2], bottom = border[3];
    auto inner = [](double corner, double across, double down) {
        return CornerEllipse{std::max(0.0, corner - across), std::max(0.0, corner - down)};
    };
    return {inner(radius[0], left, top), inner(radius[1], right, top), inner(radius[2], right, bottom),
            inner(radius[3], left, bottom)};
}

bool Rounded(const std::array<CornerEllipse, 4>& corners) {
    return std::any_of(corners.begin(), corners.end(), [](const CornerEllipse& corner) {
        return corner.x > 0 && corner.y > 0;
    });
}

}  // namespace ampify::theme
