#pragma once
// The sidebar's own entries (Marketplace, extensions) in the folded column: a centred square as wide as the column
// less the entry's side margins, with no label. Open, the full row with its label.

#include <algorithm>
#include <cmath>

namespace ampify {

// The folded width of an entry: the column less its left and right margins, never negative.
inline double FoldedEntryWidth(double columnWidth, double marginLeft, double marginRight) {
    return std::max(0.0, columnWidth - marginLeft - marginRight);
}

// What an entry's button shows: the pane state, whether it is on, and its width (NaN when open, the width is automatic).
struct EntryLayout {
    bool paneOpen = false;
    bool active = false;
    double width = 0;
};

// True when the button must be laid out again. The width is the one the button shows now, so a width that a theme
// wrote on it is put back at the next pass.
inline bool EntryLayoutChanged(const EntryLayout& shown, const EntryLayout& wanted) {
    const bool sameWidth = (std::isnan(shown.width) && std::isnan(wanted.width)) || shown.width == wanted.width;
    return shown.paneOpen != wanted.paneOpen || shown.active != wanted.active || !sameWidth;
}

}  // namespace ampify
