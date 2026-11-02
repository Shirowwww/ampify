#include "selector.hpp"

#include <string>
#include <string_view>

namespace ampify::theme {

namespace {

bool CompoundMatches(const Compound& compound, const NodeInfo& node) {
    if (!compound.type.empty() && compound.type != node.type && compound.type != node.fullType) return false;
    if (!compound.name.empty() && compound.name != node.name) return false;
    if (compound.nth > 0 && node.position + 1 != compound.nth) return false;
    if (compound.nthLast > 0 && (node.position < 0 || node.siblings - node.position != compound.nthLast)) return false;
    if (compound.last && (node.position < 0 || node.position != node.siblings - 1)) return false;
    if (compound.only && (node.position != 0 || node.siblings != 1)) return false;
    for (const Compound& negated : compound.negations) {
        if (CompoundMatches(negated, node)) return false;
    }
    return true;
}

// The sibling listed just before `index`, or -1. The snapshot lists an element after its parent and after the whole
// subtree of each earlier sibling, so the element just before it is that sibling or one of its descendants.
int PreviousSibling(const std::vector<NodeInfo>& nodes, int index) {
    const int parent = nodes[index].parent;
    if (parent < 0) return -1;
    for (int k = index - 1; k > parent; k = nodes[k].parent) {
        if (nodes[k].parent == parent) return k;
    }
    return -1;
}

// `part` is the index of the compound to match against `nodes[index]`. The compounds before it must match
// the ancestors or the earlier siblings, as their combinators say. Descendant and sibling combinators backtrack.
bool MatchFrom(const std::vector<Compound>& parts, size_t part, const std::vector<NodeInfo>& nodes, int index) {
    if (!CompoundMatches(parts[part], nodes[index])) return false;
    if (part == 0) return true;

    const Combinator combinator = parts[part].combinator;
    if (combinator == Combinator::Child) {
        int parent = nodes[index].parent;
        return parent >= 0 && MatchFrom(parts, part - 1, nodes, parent);
    }
    if (combinator == Combinator::Adjacent) {
        int previous = PreviousSibling(nodes, index);
        return previous >= 0 && MatchFrom(parts, part - 1, nodes, previous);
    }
    if (combinator == Combinator::Sibling) {
        for (int previous = PreviousSibling(nodes, index); previous >= 0; previous = PreviousSibling(nodes, previous)) {
            if (MatchFrom(parts, part - 1, nodes, previous)) return true;
        }
        return false;
    }
    for (int ancestor = nodes[index].parent; ancestor >= 0; ancestor = nodes[ancestor].parent) {
        if (MatchFrom(parts, part - 1, nodes, ancestor)) return true;
    }
    return false;
}

// As in CSS, :not(x) counts what x counts.
void AddSpecificity(const Compound& compound, Specificity& specificity) {
    if (!compound.name.empty()) specificity.names++;
    if (compound.nth > 0) specificity.classes++;
    if (compound.nthLast > 0) specificity.classes++;
    if (compound.last) specificity.classes++;
    if (compound.only) specificity.classes++;
    if (!compound.type.empty()) specificity.types++;
    for (const Compound& negated : compound.negations) AddSpecificity(negated, specificity);
}

}  // namespace

Specificity SpecificityOf(const Selector& selector) {
    Specificity specificity;
    for (const Compound& compound : selector.parts) AddSpecificity(compound, specificity);
    if (selector.pseudo != PseudoElement::None) specificity.types++;  // as CSS counts a pseudo-element
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
