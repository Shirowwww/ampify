#pragma once

// Where the marks of Ampify.ui.playbar.mark go on a progress line `width` pixels long: a tick at each end of the
// stretch and the shade between them. `from` and `to` are parts of the song (0 to 1, -1 when not marked); a tick
// stays whole inside the line, even at the very start or end of the song.

#include <algorithm>

namespace ampify {

struct ProgressMark {
    bool start = false;    // the first tick shows
    bool end = false;      // the second tick and the shade between them show
    double startLeft = 0;  // left edge of the first tick
    double endLeft = 0;    // left edge of the second tick
    double shadeLeft = 0;  // the shade runs from the first tick's centre to the second's
    double shadeWidth = 0;
};

inline ProgressMark LayOutProgressMark(double width, double from, double to, double tick) {
    ProgressMark mark;
    if (!(width > tick) || !(from >= 0)) return mark;
    auto left = [width, tick](double part) { return std::clamp(part * width - tick / 2, 0.0, width - tick); };
    mark.start = true;
    mark.startLeft = left(std::min(from, 1.0));
    if (to >= 0) {
        mark.end = true;
        mark.endLeft = left(std::min(to, 1.0));
        if (mark.endLeft < mark.startLeft) std::swap(mark.startLeft, mark.endLeft);
        mark.shadeLeft = mark.startLeft + tick / 2;
        mark.shadeWidth = mark.endLeft - mark.startLeft;
    }
    return mark;
}

}  // namespace ampify
