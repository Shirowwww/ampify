#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tree.hpp"

namespace ampify::theme {

struct InspectOptions {
    // Apple Music's window holds 5000 to 7000 elements, the popups last: the dump stops here and says so.
    size_t maxLines = 20000;
};

// The dump written to the logs folder: the resource keys first, then the element tree, one element per line,
// indented by depth, with the selector that finds it and the values of the common properties.
std::string InspectTree(TreeBackend& tree, const InspectOptions& options = {});

// The same tree as JSON, written next to inspector.txt as logs\inspector.json, for tools such as `ampify inspect --tree`:
//   {"version":1,"truncated":false,
//    "resources":[{"key":"…","value":"…"}],
//    "nodes":[{"index":0,"id":"…","type":"Grid","fullType":"…","name":"Bar","parent":-1,"depth":0,
//              "selector":"Grid#Bar","properties":{"Background":"#FF0000FF"}}]}
// `nodes` lists the parents before their children; `parent` is the index of the parent in `nodes`, -1 for a root.
// `id` is a text, because the backend's numbers do not all fit a JSON number. Templates and opaque values read
// "present", as in inspector.txt. The same element limit applies; `truncated` says when it was reached.
std::string InspectTreeJson(TreeBackend& tree, const InspectOptions& options = {});

// Reads back the `nodes` of inspector.json, for tools that check selectors against a saved tree
// (`ampify_theme_check --names`). Each node gets its place among its parent's children from the order of the list.
// Nothing, with `problem` set, for a file that is not such a tree.
std::optional<std::vector<NodeInfo>> ReadInspectorTree(std::string_view json, std::string* problem = nullptr);

// A selector for the element at `index`: `Type#name` when it has a name, otherwise the chain of types from the
// nearest named ancestor (or the root) down to the element, joined with `>`.
std::string SuggestSelector(const std::vector<NodeInfo>& nodes, int index);

}  // namespace ampify::theme
