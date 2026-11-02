#include "selector.hpp"

#include <string>
#include <string_view>

namespace ampify::theme {

namespace {

bool CompoundMatches(const Compound& compound, const NodeInfo& node) {
    if (!compound.type.empty() && compound.type != node.type && compound.type != node.fullType) return false;
    if (!compound.name.empty() && compound.name != node.name) return false;
    if (compound.nth > 0 && node.position + 1 != compound.nth) return false;
    if (compound.last && (node.position < 0 || node.position != node.siblings - 1)) return false;
    return true;
}

// `part` is the index of the compound to match against `nodes[index]`. The compounds before it must match
// the ancestors, as their combinators say. Descendant combinators backtrack over every ancestor.
bool MatchFrom(const std::vector<Compound>& parts, size_t part, const std::vector<NodeInfo>& nodes, int index) {
    if (!CompoundMatches(parts[part], nodes[index])) return false;
    if (part == 0) return true;

    if (parts[part].combinator == Combinator::Child) {
        int parent = nodes[index].parent;
        return parent >= 0 && MatchFrom(parts, part - 1, nodes, parent);
    }

    for (int ancestor = nodes[index].parent; ancestor >= 0; ancestor = nodes[ancestor].parent) {
        if (MatchFrom(parts, part - 1, nodes, ancestor)) return true;
    }
    return false;
}

}  // namespace

Specificity SpecificityOf(const Selector& selector) {
    Specificity specificity;
    for (const Compound& compound : selector.parts) {
        if (!compound.name.empty()) specificity.names++;
        if (compound.nth > 0) specificity.classes++;
        if (compound.last) specificity.classes++;
        if (!compound.type.empty()) specificity.types++;
    }
    return specificity;
}

// The types that root Apple's player windows: a selector must name one of them to reach an element there.
bool NamesAPlayer(const Selector& selector) {
    for (const Compound& compound : selector.parts) {
        for (std::string_view root : {"FullScreenPlayerPage", "FullScreenPlayerView", "MiniPlayerPage", "MiniPlayerView"}) {
            if (compound.type == root || compound.type.ends_with("." + std::string(root))) return true;
        }
    }
    return false;
}

bool Matches(const Selector& selector, const std::vector<NodeInfo>& nodes, int index) {
    if (selector.parts.empty() || index < 0 || index >= static_cast<int>(nodes.size())) return false;
    if (nodes[index].player && !NamesAPlayer(selector)) return false;
    return MatchFrom(selector.parts, selector.parts.size() - 1, nodes, index);
}

}  // namespace ampify::theme
