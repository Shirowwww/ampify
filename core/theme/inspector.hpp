#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "tree.hpp"

namespace amk::theme {

struct InspectOptions {
    size_t maxLines = 5000;  // a page can hold thousands of elements; the dump stops here and says so
};

// The dump written to the journal folder: the resource keys first, then the element tree, one element per line,
// indented by depth, with the selector that finds it and the values of the common properties.
std::string InspectTree(TreeBackend& tree, const InspectOptions& options = {});

// A selector for the element at `index`: `Type#name` when it has a name, otherwise the chain of types from the
// nearest named ancestor (or the root) down to the element, joined with `>`.
std::string SuggestSelector(const std::vector<NodeInfo>& nodes, int index);

}  // namespace amk::theme
