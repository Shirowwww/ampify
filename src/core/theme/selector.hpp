#pragma once

#include <compare>
#include <vector>

#include "css.hpp"
#include "tree.hpp"

namespace ampify::theme {

// CSS specificity: a selector with more #names beats one with more pseudo-classes (:nth-child...), which beats one
// with more types.
struct Specificity {
    int names = 0;
    int classes = 0;
    int types = 0;

    auto operator<=>(const Specificity&) const = default;
};

Specificity SpecificityOf(const Selector& selector);

// True when the element at `index` of `nodes` matches the selector. Combinators look at the visual parents.
bool Matches(const Selector& selector, const std::vector<NodeInfo>& nodes, int index);

// The index in `nodes` of the element that matched the compound `part` of the selector, when the element at `index`
// matches it (`part` of the last compound gives `index`); the nearest one when several would do. -1 when the selector
// does not match.
int MatchedElement(const Selector& selector, const std::vector<NodeInfo>& nodes, int index, size_t part);

// Sets NodeInfo::playing on the list row (ListViewItemPresenter or GridViewItemPresenter) that holds each of `bars`, the
// indexes of Apple's visible "now playing" bars, and on every element between the row and the bars. Bars outside a
// list row mark nothing, so `:playing` never reaches a page.
void MarkPlayingRows(std::vector<NodeInfo>& nodes, const std::vector<int>& bars);

// A list row's presenter (ListViewItemPresenter, GridViewItemPresenter), which `.list-item` names, is selected with the
// item that holds it: `:selected` then reaches the row a theme names, not only Apple's ListViewItem around it.
void MarkSelectedPresenters(std::vector<NodeInfo>& nodes);

// Apple shows some switches as a plain Button with a dot under it when on (repeat: Button#RepeatButton and
// Ellipse#RepeatButtonDot). Sets NodeInfo::checked on the Button named like each of `dots`, the indexes of visible
// elements named "<button>Dot", without "Dot", when it holds that dot; `:checked` then reaches it as it does a toggle.
void MarkCheckedByDot(std::vector<NodeInfo>& nodes, const std::vector<int>& dots);

// True when a rule of `parts` waits on :checked, :selected, :disabled or :idle (TreeBackend::WatchStates).
bool UsesControlStates(const std::vector<Compound>& parts);

}  // namespace ampify::theme
