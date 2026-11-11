#pragma once

// The full-screen player's lyrics around the lines being sung (apple.lyricsBefore, apple.lyricsAfter): how many lines
// show above and below them, and how far the block moves so that it stands centred on them. Most of the time one line
// is sung; in a duet two can be at once, so the sung lines are a span, `first` to `last`, and "before" counts from the
// first one, "after" from the last. kAllLines leaves that side as Apple shows it. Pure arithmetic, for
// src/win/lyrics_window.cpp and its tests.

#include <string_view>

namespace ampify {

inline constexpr int kAllLines = -1;
inline constexpr int kMostLines = 5;  // the settings offer 0 to 5, or all

// The lines being sung: none yet (first = last = -1, as before the first line), or first to last.
struct SungLines {
    int first = -1;
    int last = -1;
};

// Whether line `index` shows while `sung` are sung. Before the first line, only the coming ones show.
inline bool LyricLineShown(int index, SungLines sung, int before, int after) {
    if (before != kAllLines && index < sung.first - before) return false;
    if (after != kAllLines && index > sung.last + after) return false;
    return true;
}

// The sung lines as they stand in the lyrics area, before Ampify moves them: their top and bottom edges, from the
// area's top edge.
struct SungSpan {
    double top = 0;
    double bottom = 0;
};

// How far to move the lines down (positive) or up so that the sung ones stand in the middle of the lyrics area, whose
// height is `area`: Apple keeps them about a third of the way down, and the few lines shown then hung high, with
// room left under them. Nothing while a side shows all its lines (Apple's own place then) or no line is sung.
inline double LyricsCentringShift(SungSpan sung, double area, int before, int after, bool singing) {
    if (before == kAllLines || after == kAllLines || !singing || area <= 0 || sung.bottom < sung.top) return 0;
    return area / 2 - (sung.top + sung.bottom) / 2;
}

// A setting's text: "all" (or nothing) for every line, else 0 to kMostLines; anything else counts as all.
inline int LyricLinesOf(std::string_view text) {
    if (text.size() == 1 && text[0] >= '0' && text[0] - '0' <= kMostLines) return text[0] - '0';
    return kAllLines;
}

}  // namespace ampify
