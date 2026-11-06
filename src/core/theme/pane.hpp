#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "tree.hpp"

namespace ampify::theme {

// A SplitView's template lays out its pane, `Grid#PaneRoot` under the template's root grid: its width is bound to
// the open pane length, its column and alignment are set by the pane's visual states. A theme that takes those over
// lets the pane follow its content, and Apple's lyrics size their lines from the pane: WinUI's layout then never
// settles and ends the app (AG_E_LAYOUT_CYCLE). The engine keeps those to the template, for every theme.

// The index of the SplitView whose template places the element at `position` as its pane, or -1.
int PaneOwner(const std::vector<NodeInfo>& nodes, int position);

// True for a property the pane's visual states set: a theme's value there is refused.
bool TemplatePlacesPane(std::string_view property);

// `width: auto` on a pane: the open pane length (a Length text) less the left and right of `margin` (a Thickness
// text, "left,top,right,bottom"), so the pane fits its column instead of being cut by its margin. Never below 0.
// Nothing when either cannot be read.
std::optional<double> FittedPaneWidth(std::string_view openPaneLength, std::string_view margin);

}  // namespace ampify::theme
