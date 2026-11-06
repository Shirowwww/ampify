#include "glyph_rows.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ampify::theme {

namespace {

constexpr double kMinimumRowGap = 1.0;  // boxes with no height still split into rows a pixel apart

struct Row {
    double top = 0;
    double height = 0;
    double left = 0;
    double right = 0;
};

double RowShift(const Row& row, double hostWidth, RowAlign align) {
    double spare = hostWidth - (row.right - row.left);
    if (align == RowAlign::Start || spare <= 0) return 0;
    double target = align == RowAlign::Center ? spare / 2 : spare;
    return target - row.left;
}

}  // namespace

std::vector<double> RowShifts(const std::vector<GlyphBox>& boxes, double hostWidth, RowAlign align) {
    std::vector<double> shifts(boxes.size(), 0.0);
    if (!(hostWidth > 0) || align == RowAlign::Start) return shifts;

    std::vector<size_t> order(boxes.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return boxes[a].y < boxes[b].y; });

    std::vector<Row> rows;
    std::vector<size_t> rowOf(boxes.size(), 0);
    for (size_t index : order) {
        const GlyphBox& box = boxes[index];
        bool joins = !rows.empty() &&
                     box.y - rows.back().top < std::max(kMinimumRowGap, std::max(rows.back().height, box.height) / 2);
        if (!joins) rows.push_back({box.y, box.height, box.x, box.x + box.width});
        Row& row = rows.back();
        row.height = std::max(row.height, box.height);
        row.left = std::min(row.left, box.x);
        row.right = std::max(row.right, box.x + box.width);
        rowOf[index] = rows.size() - 1;
    }
    for (size_t i = 0; i < boxes.size(); ++i) shifts[i] = RowShift(rows[rowOf[i]], hostWidth, align);
    return shifts;
}

RowAlign RowAlignOf(std::string_view keyword) {
    if (keyword == "Center") return RowAlign::Center;
    if (keyword == "Right") return RowAlign::End;
    return RowAlign::Start;
}

std::string_view RowAlignKeyword(RowAlign align) {
    if (align == RowAlign::Center) return "Center";
    if (align == RowAlign::End) return "Right";
    return "Left";
}

BoxSize LiftedMaxSize(BoxSize original, double width, double height) {
    if (!(original.width > 0) || !(original.height > 0)) return original;
    double scale = 1.0;
    if (std::isfinite(width)) scale = std::max(scale, width / original.width);
    if (std::isfinite(height)) scale = std::max(scale, height / original.height);
    return {original.width * scale, original.height * scale};
}

double LiftedBound(double appleMax, double cap) {
    return std::isfinite(appleMax) && appleMax < cap ? cap : appleMax;
}

}  // namespace ampify::theme
