#include "render_tree.hpp"

#include <algorithm>
#include <cmath>

namespace ampify::uirender {

namespace {

bool Safe(char c, bool allowDash) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (allowDash && c == '-');
}

std::string Sanitized(std::string_view text, char replacement, bool allowDash, bool allowDot) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        bool keep = Safe(c, allowDash) || (allowDot && c == '.');
        out += keep ? c : replacement;
    }
    return out;
}

// The key of a child: its `key` or `id` attribute, when it is a text or a whole number.
std::string KeyOf(const Json& node) {
    for (const char* name : {"key", "id"}) {
        const Json* value = node.Find(name);
        if (!value) continue;
        if (const std::string* text = value->AsString(); text && !text->empty()) return "k" + Sanitized(*text, '-', true, false);
        if (const double* number = value->AsNumber(); number && std::floor(*number) == *number) {
            return "k" + std::to_string(static_cast<long long>(*number));
        }
    }
    return {};
}

// `inertAbove`: an ancestor is inert, so this node is too, whatever its own attribute says (as in HTML). The
// renderer then turns hit-testing and keyboard focus off on every control below, not only on the inert box.
void Build(const Json& tree, const std::string& id, RenderNode& out, bool inertAbove) {
    out.id = id;
    if (const Json* type = tree.Find("type")) {
        if (const std::string* text = type->AsString()) out.type = *text;
    }
    const Json::Object* members = tree.AsObject();
    if (!members) return;
    for (const auto& [key, value] : *members) {
        if (key == "type" || key == "children") continue;
        out.attributes[key] = value;
    }
    if (inertAbove) out.attributes["inert"] = Json(true);
    const bool inert = out.Bool("inert", false);
    const Json* children = tree.Find("children");
    const Json::Array* items = children ? children->AsArray() : nullptr;
    if (!items) return;
    out.children.reserve(items->size());
    std::vector<std::string> used;
    for (std::size_t index = 0; index < items->size(); index++) {
        std::string segment = KeyOf((*items)[index]);
        // A repeated key falls back to the position, as Ampify.React does.
        if (segment.empty() || std::find(used.begin(), used.end(), segment) != used.end()) segment = std::to_string(index);
        used.push_back(segment);
        RenderNode child;
        Build((*items)[index], id + "." + segment, child, inert);
        out.children.push_back(std::move(child));
    }
}

std::vector<std::string> ChangedAttributes(const RenderNode& a, const RenderNode& b) {
    std::vector<std::string> changed;
    for (const auto& [key, value] : b.attributes) {
        auto found = a.attributes.find(key);
        if (found == a.attributes.end() || found->second.Dump() != value.Dump()) changed.push_back(key);
    }
    for (const auto& [key, value] : a.attributes) {
        if (!b.attributes.count(key)) changed.push_back(key);
    }
    std::sort(changed.begin(), changed.end());
    return changed;
}

void Count(const Patch& patch, PatchStats& stats) {
    switch (patch.op) {
        case PatchOp::Keep: stats.kept++; break;
        case PatchOp::Update: stats.updated++; break;
        case PatchOp::Create: stats.created += static_cast<int>(CountNodes(*patch.node)); return;
        case PatchOp::Replace: stats.replaced += static_cast<int>(CountNodes(*patch.node)); return;
    }
    stats.removed += static_cast<int>(patch.removed.size());
    for (const Patch& child : patch.children) Count(child, stats);
}

}  // namespace

std::string RenderNode::Text(std::string_view name, std::string_view fallback) const {
    auto found = attributes.find(std::string(name));
    if (found == attributes.end()) return std::string(fallback);
    if (const std::string* text = found->second.AsString()) return *text;
    if (const double* number = found->second.AsNumber()) {
        if (std::floor(*number) == *number && std::fabs(*number) < 1e15) return std::to_string(static_cast<long long>(*number));
        return found->second.Dump();
    }
    if (const bool* flag = found->second.AsBool()) return *flag ? "true" : "false";
    return std::string(fallback);
}

double RenderNode::Number(std::string_view name, double fallback) const {
    auto found = attributes.find(std::string(name));
    if (found == attributes.end()) return fallback;
    if (const double* number = found->second.AsNumber()) return *number;
    if (const std::string* text = found->second.AsString()) {
        try {
            return std::stod(*text);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

bool RenderNode::Bool(std::string_view name, bool fallback) const {
    auto found = attributes.find(std::string(name));
    if (found == attributes.end()) return fallback;
    if (const bool* flag = found->second.AsBool()) return *flag;
    if (const double* number = found->second.AsNumber()) return *number != 0;
    if (const std::string* text = found->second.AsString()) return *text == "true" || *text == "1";
    return fallback;
}

bool RenderNode::Has(std::string_view name) const {
    return attributes.count(std::string(name)) != 0;
}

std::string SurfaceNodeId(std::string_view kind, std::string_view id) {
    std::string trimmed(id);
    while (!trimmed.empty() && trimmed.front() == '/') trimmed.erase(trimmed.begin());
    return Sanitized(kind, '-', true, false) + "-" + Sanitized(trimmed.empty() ? "main" : trimmed, '-', true, false);
}

RenderNode BuildRenderTree(const Json& tree, const std::string& rootId) {
    RenderNode root;
    Build(tree, rootId, root, false);
    return root;
}

std::string ElementName(std::string_view elementId, std::string_view nodeId) {
    return "AMPIFY_" + Sanitized(elementId, '_', true, false) + "_" + Sanitized(nodeId, '_', true, true);
}

bool IsOwnName(std::string_view name) {
    return name.starts_with("AMPIFY_") || name.starts_with("Ampify");
}

bool IsForeignModName(std::string_view name) {
    return name.substr(0, 11) == "AMExclusive";
}

Patch Reconcile(const RenderNode* old, const RenderNode& next) {
    Patch patch;
    patch.node = &next;
    if (!old) {
        patch.op = PatchOp::Create;
        return patch;
    }
    if (old->type != next.type) {
        patch.op = PatchOp::Replace;
        return patch;
    }
    patch.changed = ChangedAttributes(*old, next);
    patch.op = patch.changed.empty() ? PatchOp::Keep : PatchOp::Update;

    std::vector<bool> reused(old->children.size(), false);
    int expected = 0;  // the old index that keeps the order unchanged
    for (const RenderNode& child : next.children) {
        int match = -1;
        for (std::size_t index = 0; index < old->children.size(); index++) {
            if (!reused[index] && old->children[index].id == child.id) {
                match = static_cast<int>(index);
                break;
            }
        }
        Patch childPatch = Reconcile(match >= 0 ? &old->children[match] : nullptr, child);
        childPatch.from = match;
        if (match >= 0) reused[match] = true;
        // Created, rebuilt or moved children change the list of the native parent.
        if (match != expected || childPatch.op == PatchOp::Replace) patch.reordered = true;
        expected = (match >= 0 ? match : expected) + 1;
        patch.children.push_back(std::move(childPatch));
    }
    for (std::size_t index = 0; index < reused.size(); index++) {
        if (!reused[index]) patch.removed.push_back(static_cast<int>(index));
    }
    if (!patch.removed.empty()) patch.reordered = true;
    return patch;
}

PatchStats CountPatch(const Patch& patch) {
    PatchStats stats;
    Count(patch, stats);
    return stats;
}

std::size_t CountNodes(const RenderNode& node) {
    std::size_t count = 1;
    for (const RenderNode& child : node.children) count += CountNodes(child);
    return count;
}

}  // namespace ampify::uirender
