#pragma once

// The layout rules of the interface primitives on the Windows side. The values of the style attributes (colors,
// padding, transitions) are in style_values.hpp. Portable and tested under Linux.

#include <cstddef>
#include <string>

#include "render_tree.hpp"
#include "style_values.hpp"

namespace ampify::uirender {

// How a container lays out its children on the Windows side. A change of layout rebuilds the native container.
enum class Layout {
    None,     // not a container (a menu's items are its flyout, not children)
    Panel,    // StackPanel: stack, list, tabs, card's body, a row without a growing child and not of two children
    Pair,     // a row of exactly two children and none that grows: Grid (Star, Auto), the second on the right
    Grid,     // a stack or row with a growing child: Grid, one Auto or Star cell per child
    Layer,    // layer: Grid of one cell, the children stacked on top of each other
    Columns,  // grid: Grid of `columns` equal Star columns, the children filled row by row
};
Layout LayoutOf(const RenderNode& node);

// `columns` of a grid: 2 when missing, 1 to 12 (kMaxGridColumns) otherwise.
using uivocab::kMaxGridColumns;
int GridColumns(const RenderNode& node);

// The row and the column of the child at `index` in a grid of `columns` columns.
struct GridCell {
    int row = 0;
    int column = 0;
};
GridCell GridCellOf(std::size_t index, int columns);

// The variant of a button-like node: a tab is primary while it is on, tertiary while it is not; a menu's is secondary.
std::string ButtonVariantOf(const RenderNode& node);

}  // namespace ampify::uirender
