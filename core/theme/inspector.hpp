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

// The same tree as JSON, written next to inspector.txt as journal\inspector.json, for tools such as `amk inspect --tree`:
//   {"version":1,"truncated":false,
//    "resources":[{"key":"…","value":"…"}],
//    "nodes":[{"index":0,"id":"…","type":"Grid","fullType":"…","name":"Bar","parent":-1,"depth":0,
//              "selector":"Grid#Bar","properties":{"Background":"#FF0000FF"}}]}
// `nodes` lists the parents before their children; `parent` is the index of the parent in `nodes`, -1 for a root.
// `id` is a text, because the backend's numbers do not all fit a JSON number. Templates and opaque values read
// « présent », as in inspector.txt. The same element limit applies; `truncated` says when it was reached.
std::string InspectTreeJson(TreeBackend& tree, const InspectOptions& options = {});

// A selector for the element at `index`: `Type#name` when it has a name, otherwise the chain of types from the
// nearest named ancestor (or the root) down to the element, joined with `>`.
std::string SuggestSelector(const std::vector<NodeInfo>& nodes, int index);

}  // namespace amk::theme
