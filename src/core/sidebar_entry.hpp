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

// Past this, the measured axis is not the column's (an item mid-animation, or another layout): the icon stays centred.
constexpr double kMaxEntryIconShift = 8;

// How far an entry's icon moves to sit on the axis of Apple's own icons, which a theme can move off the column's
// middle (Glass gives Apple's items no side margin). Half pixels, as the layout rounds them; less than half is none.
inline double EntryIconShift(double appleIconCentre, double entryIconCentre) {
    const double shift = std::round((appleIconCentre - entryIconCentre) * 2) / 2;
    if (std::abs(shift) > kMaxEntryIconShift) return 0;
    return shift;
}

// What an entry's button shows: the pane state, whether it is on, its width (NaN when open, the width is automatic),
// and how far its icon is moved onto Apple's axis.
struct EntryLayout {
    bool paneOpen = false;
    bool active = false;
    double width = 0;
    double iconShift = 0;
};

// True when the button must be laid out again. The width is the one the button shows now, so a width that a theme
// wrote on it is put back at the next pass.
inline bool EntryLayoutChanged(const EntryLayout& shown, const EntryLayout& wanted) {
    const bool sameWidth = (std::isnan(shown.width) && std::isnan(wanted.width)) || shown.width == wanted.width;
    return shown.paneOpen != wanted.paneOpen || shown.active != wanted.active || !sameWidth ||
           shown.iconShift != wanted.iconShift;
}

}  // namespace ampify
