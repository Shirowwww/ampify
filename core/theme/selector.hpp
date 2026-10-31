#pragma once

#include <compare>
#include <vector>

#include "css.hpp"
#include "tree.hpp"

namespace ampify::theme {

// CSS specificity: a selector with more #names beats one with more types, which beats one with fewer of either.
struct Specificity {
    int names = 0;
    int types = 0;

    auto operator<=>(const Specificity&) const = default;
};

Specificity SpecificityOf(const Selector& selector);

// True when the element at `index` of `nodes` matches the selector. Combinators look at the visual parents.
bool Matches(const Selector& selector, const std::vector<NodeInfo>& nodes, int index);

}  // namespace ampify::theme
