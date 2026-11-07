#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tree.hpp"

namespace ampify::theme {

// The values the theme engine will give back when its theme comes off, by element and XAML property
// (ThemeEngine::Originals): the inspector records them, so the offline bench can replay another theme on Apple's own
// values.
using OriginalValues = std::map<std::pair<NodeId, std::string>, Value>;

struct InspectOptions {
    // Apple Music's window holds 5000 to 7000 elements, the popups last: the dump stops here and says so.
    size_t maxLines = 20000;
    // For inspector.json: the theme on screen (its folder name, empty for none) and the originals its engine holds.
    std::string theme;
    const OriginalValues* originals = nullptr;
};

// The properties the inspector reads on every element, in the order it writes them.
std::span<const char* const> InspectedProperties();

// The text inspector.json gives an original that was no value of the element's own (kNoOwnValue).
inline constexpr std::string_view kNoOwnValueText = "no-own-value";

// One walk of the tree, which both dumps below write: the window, the resources and, for each element up to the limit,
// the InspectedProperties it has and its box.
struct InspectedNode {
    std::vector<std::pair<const char*, Value>> properties;
    std::optional<NodeBox> box;
};
struct Inspection {
    std::optional<ViewSize> window;
    std::vector<std::pair<std::string, std::optional<Value>>> resources;
    std::vector<NodeInfo> nodes;           // the whole tree
    std::vector<InspectedNode> inspected;  // the first nodes, up to maxLines
    size_t maxLines = 0;
    bool Truncated() const { return inspected.size() < nodes.size(); }
};
Inspection Inspect(TreeBackend& tree, size_t maxLines = InspectOptions{}.maxLines);

// The dump written to the logs folder as inspector.txt: the resource keys first, then the element tree, one element per
// line, indented by depth, with the selector that finds it and the values of the common properties.
std::string InspectionText(const Inspection& inspection);
std::string InspectTree(TreeBackend& tree, const InspectOptions& options = {});

// The same tree as JSON, written next to inspector.txt as logs\inspector.json, for tools such as `ampify inspect --tree`
// and the offline bench (`ampify theme replay`, docs/development/offline-bench.md):
//   {"version":2,"truncated":false,"theme":"glass","window":{"width":1280,"height":800,"sidebarFolded":false},
//    "resources":[{"key":"…","value":"…"}],
//    "nodes":[{"index":0,"id":"…","type":"Grid","fullType":"…","name":"Bar","parent":-1,"depth":0,
//              "selector":"Grid#Bar","icon":false,"panel":true,"player":false,"playing":false,
//              "box":[0,0,1280,64],"properties":{"Background":"#FF0000FF"},"originals":{"Background":"present"}}]}
// `nodes` lists the parents before their children; `parent` is the index of the parent in `nodes`, -1 for a root.
// `id` is a text, because the backend's numbers do not all fit a JSON number. Templates and opaque values read
// "present", as in inspector.txt. The same element limit applies; `truncated` says when it was reached.
// `box` (x, y, width, height in the root's coordinates) is left out for an element not laid out; `originals`, the values
// the theme on screen will give back, only for the elements it changed ("no-own-value": none of the element's own).
// Version 1 (before 06/11/2026) had no window, theme, flags, box nor originals. Only `theme` and `originals` of
// `options` count here.
std::string InspectionJson(const Inspection& inspection, const InspectOptions& options = {});
std::string InspectTreeJson(TreeBackend& tree, const InspectOptions& options = {});

// Reads back the `nodes` of inspector.json, for tools that check selectors against a saved tree
// (`ampify theme names`). Each node gets its place among its parent's children from the order of the list.
// Nothing, with `problem` set, for a file that is not such a tree.
std::optional<std::vector<NodeInfo>> ReadInspectorTree(std::string_view json, std::string* problem = nullptr);

// A selector for the element at `index`: `Type#name` when it has a name, otherwise the chain of types from the
// nearest named ancestor (or the root) down to the element, joined with `>`.
std::string SuggestSelector(const std::vector<NodeInfo>& nodes, int index);

}  // namespace ampify::theme
