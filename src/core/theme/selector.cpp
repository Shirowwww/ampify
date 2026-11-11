#include "selector.hpp"

#include <string>
#include <string_view>

namespace ampify::theme {

namespace {

bool HasInside(const std::vector<Compound>& parts, const std::vector<NodeInfo>& nodes, int index);

bool CompoundMatches(const Compound& compound, const std::vector<NodeInfo>& nodes, int index) {
    const NodeInfo& node = nodes[index];
    if (!compound.type.empty() && compound.type != node.type && compound.type != node.fullType) return false;
    if (!compound.name.empty() && compound.name != node.name) return false;
    if (compound.nth > 0 && node.position + 1 != compound.nth) return false;
    if (compound.nthLast > 0 && (node.position < 0 || node.siblings - node.position != compound.nthLast)) return false;
    if (compound.last && (node.position < 0 || node.position != node.siblings - 1)) return false;
    if (compound.only && (node.position != 0 || node.siblings != 1)) return false;
    if (compound.playing && !node.playing) return false;
    if (compound.checked && !node.checked) return false;
    if (compound.selected && !node.selected) return false;
    if (compound.disabled && !node.disabled) return false;
    if (compound.idle && !node.idle) return false;
    for (const Compound& negated : compound.negations) {
        if (CompoundMatches(negated, nodes, index)) return false;
    }
    for (const std::vector<Compound>& relative : compound.has) {
        if (!HasInside(relative, nodes, index)) return false;
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

// Where a match is going and what it records: the element that matched the compound `wanted`, written to `found`.
struct Match {
    const std::vector<Compound>& parts;
    const std::vector<NodeInfo>& nodes;
    size_t wanted = static_cast<size_t>(-1);
    int found = -1;
    // For :has(), the element the parts are relative to: the match stays inside it, its first part in it (or, for
    // `:has(> X)`, a child of it).
    int scope = -1;
};

bool MatchFrom(Match& match, size_t part, int index);

// The compounds before `part` against the ancestors or the earlier siblings of `nodes[index]`, as the combinator of
// `part` says. Descendant and sibling combinators backtrack.
bool MatchBefore(Match& match, size_t part, int index) {
    const std::vector<NodeInfo>& nodes = match.nodes;
    const Combinator combinator = match.parts[part].combinator;
    if (combinator == Combinator::Child) {
        int parent = nodes[index].parent;
        return parent > match.scope && MatchFrom(match, part - 1, parent);
    }
    if (combinator == Combinator::Adjacent) {
        int previous = PreviousSibling(nodes, index);
        return previous >= 0 && MatchFrom(match, part - 1, previous);
    }
    if (combinator == Combinator::Sibling) {
        for (int previous = PreviousSibling(nodes, index); previous >= 0; previous = PreviousSibling(nodes, previous)) {
            if (MatchFrom(match, part - 1, previous)) return true;
        }
        return false;
    }
    for (int ancestor = nodes[index].parent; ancestor > match.scope; ancestor = nodes[ancestor].parent) {
        if (MatchFrom(match, part - 1, ancestor)) return true;
    }
    return false;
}

// `part` is the index of the compound to match against `nodes[index]`. Every caller stops at the first success, so the
// element recorded for `wanted` belongs to the match that is kept: the nearest one.
bool MatchFrom(Match& match, size_t part, int index) {
    if (!CompoundMatches(match.parts[part], match.nodes, index)) return false;
    if (part == 0 && match.scope >= 0 && match.parts[0].combinator == Combinator::Child &&
        match.nodes[index].parent != match.scope) {
        return false;
    }
    if (part > 0 && !MatchBefore(match, part, index)) return false;
    if (part == match.wanted) match.found = index;
    return true;
}

// The elements inside `index` are those listed after it whose parent is it or one of them: the snapshot lists each
// element's subtree right after it.
bool HasInside(const std::vector<Compound>& parts, const std::vector<NodeInfo>& nodes, int index) {
    if (parts.empty()) return false;
    const int count = static_cast<int>(nodes.size());
    for (int inside = index + 1; inside < count && nodes[inside].parent >= index; inside++) {
        Match match{parts, nodes};
        match.scope = index;
        if (MatchFrom(match, parts.size() - 1, inside)) return true;
    }
    return false;
}

// As in CSS, :not(x) counts what x counts; the :except(x) of a built-in name counts nothing.
void AddSpecificity(const Compound& compound, Specificity& specificity) {
    if (!compound.name.empty()) specificity.names++;
    if (compound.nth > 0) specificity.classes++;
    if (compound.nthLast > 0) specificity.classes++;
    if (compound.last) specificity.classes++;
    if (compound.only) specificity.classes++;
    if (compound.playing) specificity.classes++;
    if (compound.checked) specificity.classes++;
    if (compound.selected) specificity.classes++;
    if (compound.disabled) specificity.classes++;
    if (compound.idle) specificity.classes++;
    if (!compound.type.empty()) specificity.types++;
    for (const Compound& negated : compound.negations) {
        if (negated.counted) AddSpecificity(negated, specificity);
    }
    for (const std::vector<Compound>& relative : compound.has) {  // as in CSS, :has(x) counts what x counts
        for (const Compound& part : relative) AddSpecificity(part, specificity);
    }
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
    if (selector.parts.empty()) return false;
    return MatchedElement(selector, nodes, index, selector.parts.size() - 1) >= 0;
}

int MatchedElement(const Selector& selector, const std::vector<NodeInfo>& nodes, int index, size_t part) {
    if (selector.parts.empty() || part >= selector.parts.size()) return -1;
    if (index < 0 || index >= static_cast<int>(nodes.size())) return -1;
    if (nodes[index].player && !NamesAPlayer(selector)) return -1;
    Match match{selector.parts, nodes, part};
    return MatchFrom(match, selector.parts.size() - 1, index) ? match.found : -1;
}

void MarkPlayingRows(std::vector<NodeInfo>& nodes, const std::vector<int>& bars) {
    const int count = static_cast<int>(nodes.size());
    for (int bar : bars) {
        if (bar < 0 || bar >= count) continue;
        int row = nodes[bar].parent;
        while (row >= 0 && nodes[row].type != "ListViewItemPresenter" && nodes[row].type != "GridViewItemPresenter") {
            row = nodes[row].parent;
        }
        if (row < 0) continue;
        for (int k = nodes[bar].parent; k != row; k = nodes[k].parent) nodes[k].playing = true;
        nodes[row].playing = true;
    }
}

void MarkSelectedPresenters(std::vector<NodeInfo>& nodes) {
    for (NodeInfo& node : nodes) {
        if (node.selected || node.parent < 0 || !nodes[node.parent].selected) continue;
        if (node.type == "ListViewItemPresenter" || node.type == "GridViewItemPresenter") node.selected = true;
    }
}

void MarkCheckedByDot(std::vector<NodeInfo>& nodes, const std::vector<int>& dots) {
    constexpr std::string_view kDot = "Dot";
    const int count = static_cast<int>(nodes.size());
    for (int dot : dots) {
        if (dot < 0 || dot >= count || !nodes[dot].name.ends_with(kDot) || nodes[dot].name.size() == kDot.size()) {
            continue;
        }
        const std::string_view name = nodes[dot].name;
        const std::string_view button = name.substr(0, name.size() - kDot.size());
        for (int k = nodes[dot].parent; k >= 0; k = nodes[k].parent) {
            if (nodes[k].name != button) continue;
            if (nodes[k].type == "Button") nodes[k].checked = true;
            break;
        }
    }
}

bool UsesControlStates(const std::vector<Compound>& parts) {
    for (const Compound& part : parts) {
        if (part.checked || part.selected || part.disabled || part.idle) return true;
        if (UsesControlStates(part.negations)) return true;
        for (const std::vector<Compound>& relative : part.has) {
            if (UsesControlStates(relative)) return true;
        }
    }
    return false;
}

}  // namespace ampify::theme
