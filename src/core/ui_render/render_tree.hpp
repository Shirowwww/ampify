#pragma once

// The portable half of the native renderer (src/win/ui_renderer.*): it turns an interface tree of core/ui_model (the JSON
// that Ampify.ui and Ampify.React produce) into a tree of render nodes with stable ids, and compares two of them to say,
// node by node, what the Windows side must keep, update in place, create or rebuild. No Windows API here, so the
// rules are tested under Linux with a fake widget tree (tests/ui_render_tests.cpp).

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "../json.hpp"

namespace ampify::uirender {

// One primitive of the tree. `id` is unique among the nodes of one surface: the surface id for the root, then the
// path of child positions ("panel-x.0.2"). A child that has a scalar attribute `key` (or `id`) uses it instead of its
// position, so a list keeps its rows when one is inserted ("panel-x.0.kpiste-3").
struct RenderNode {
    std::string id;
    std::string type;
    std::map<std::string, Json> attributes;  // every attribute except `type` and `children`
    std::vector<RenderNode> children;

    // The attribute as text, number or boolean, with a fallback when it is missing or of another kind.
    std::string Text(std::string_view name, std::string_view fallback = {}) const;
    double Number(std::string_view name, double fallback) const;
    bool Bool(std::string_view name, bool fallback) const;
    bool Has(std::string_view name) const;
};

// The surface part of a node id: "panel-<id>", "page-<path>" or "popup-<name>", with characters outside
// [A-Za-z0-9-] replaced by '-'.
std::string SurfaceNodeId(std::string_view kind, std::string_view id);

// Builds the render tree of `tree` (already checked by ValidateTree). `rootId` is the id of the root node. Every node
// under an `inert` one gets `inert: true` as well.
RenderNode BuildRenderTree(const Json& tree, const std::string& rootId);

// The x:Name and AutomationId of the native element: "AMPIFY_<elementId>_<nodeId>". Characters that XAML refuses in a
// name are replaced by '_'. The bench and UI Automation find the elements by this name.
std::string ElementName(std::string_view elementId, std::string_view nodeId);

// True for a name that Ampify gave (prefix "AMPIFY_", or "Ampify" for the older theme section): the discovery never
// walks into those, and the anchors never choose them.
bool IsOwnName(std::string_view name);

// True for a name given by AMExclusive ("AMExclusive" prefix): Ampify never touches those elements.
bool IsForeignModName(std::string_view name);

// What the Windows side does with one node.
enum class PatchOp {
    Keep,     // same type and attributes: nothing to change on this node (its children may still change)
    Update,   // same type, some attributes changed: set them on the existing control
    Create,   // a node that did not exist: build it
    Replace,  // same id, other type: build it again and put it where the old one was
};

struct Patch {
    PatchOp op = PatchOp::Keep;
    const RenderNode* node = nullptr;  // the new node
    int from = -1;                     // index of the matching child in the old parent, -1 for Create
    std::vector<std::string> changed;  // attribute names that differ (Update), sorted
    std::vector<Patch> children;       // one per new child, in the new order (empty for Create and Replace)
    std::vector<int> removed;          // indexes of old children that no new child reuses
    bool reordered = false;            // the children list must be rewritten (insertion, removal or move)
};

// Compares the mounted tree with the new one. `old` is null on the first render: everything is created.
Patch Reconcile(const RenderNode* old, const RenderNode& next);

struct PatchStats {
    int created = 0;
    int updated = 0;
    int replaced = 0;
    int removed = 0;
    int kept = 0;

    bool Unchanged() const { return created == 0 && updated == 0 && replaced == 0 && removed == 0; }
};

// Counts the nodes of a patch, for the log line of each render.
PatchStats CountPatch(const Patch& patch);

// Number of nodes in a render tree.
std::size_t CountNodes(const RenderNode& node);

}  // namespace ampify::uirender
