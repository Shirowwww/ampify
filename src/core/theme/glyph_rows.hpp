#pragma once

// Layout arithmetic for text Apple Music draws itself instead of in a TextBlock: the word-by-word lyrics are glyph
// visuals laid out by Apple's own code inside a Border, so `text-align` moves those visuals row by row (the
// XAML side is src/win/hosted_text_align.cpp). And the cap an artwork puts on its own size (ArtworkMaxSize), which a
// theme's larger width or height must lift.

#include <string_view>
#include <vector>

namespace ampify::theme {

// A drawn piece of text (a word or a letter), in the host's coordinates.
struct GlyphBox {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

enum class RowAlign { Start, Center, End };

// The horizontal shift of each box so that every row of boxes sits at `align` in a host `hostWidth` wide. Boxes
// share a row when their tops are closer than half the row's height, so a word lifted while sung stays on its row.
// Start, an unknown width or a row wider than the host gives 0 (Apple's own placement).
std::vector<double> RowShifts(const std::vector<GlyphBox>& boxes, double hostWidth, RowAlign align);

// True when each visual's current shift is within half a pixel of the one RowShifts gives. Apple may draw a line's
// words again in the same host (a recycled line, same size): the new ones start at no shift and need aligning again.
bool ShiftsInPlace(const std::vector<double>& current, const std::vector<double>& wanted);

// TextAlignment's member names (properties.cpp writes `text-align` as them). Justify and anything else is Start.
RowAlign RowAlignOf(std::string_view keyword);
std::string_view RowAlignKeyword(RowAlign align);

struct BoxSize {
    double width = 0;
    double height = 0;
};

// The cap to give an artwork whose width and height (NaN when unset) a theme wrote: the original grown evenly until
// both fit, never shrunk. A side capped at 0 has no cap and keeps none, while the other side still grows: Apple caps
// only the width of an editorial playlist's cover.
BoxSize LiftedMaxSize(BoxSize original, double width, double height);

// An artwork's own MaxWidth or MaxHeight once its cap is lifted: raised to the cap when below it, else kept.
double LiftedBound(double appleMax, double cap);

}  // namespace ampify::theme
