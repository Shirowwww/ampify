#pragma once

// The full-screen player's lyrics around the lines being sung (apple.lyricsBefore, apple.lyricsAfter): how many lines
// show above and below them, and how far the block moves so that it stands centred on them. Most of the time one line
// is sung; in a duet two can be at once, so the sung lines are a span, `first` to `last`, and "before" counts from the
// first one, "after" from the last. kAllLines leaves that side as Apple shows it. Pure arithmetic, for
// src/win/lyrics_window.cpp and its tests.

#include <algorithm>
#include <string_view>
#include <vector>

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

// How far to move the lines down (positive) or up so that the block shown stands centred on the sung ones, which Apple
// keeps near the middle of the player: half of what the block has more above them than below. Nothing while a side
// shows all its lines (the block then reaches the edge anyway) or no line is sung.
inline double LyricsCentringShift(const std::vector<double>& heights, double spacing, SungLines sung, int before,
                                  int after) {
    if (before == kAllLines || after == kAllLines) return 0;
    const int count = static_cast<int>(heights.size());
    if (sung.first < 0 || sung.last < sung.first || sung.last >= count) return 0;
    auto span = [&](int from, int to) {  // the lines from..to inclusive, each with the gap that follows it
        double total = 0;
        for (int index = std::max(0, from); index <= std::min(to, count - 1); ++index) total += heights[index] + spacing;
        return total;
    };
    double above = span(sung.first - before, sung.first - 1);
    double below = span(sung.last + 1, sung.last + after);
    return (above - below) / 2;
}

// A setting's text: "all" (or nothing) for every line, else 0 to kMostLines; anything else counts as all.
inline int LyricLinesOf(std::string_view text) {
    if (text.size() == 1 && text[0] >= '0' && text[0] - '0' <= kMostLines) return text[0] - '0';
    return kAllLines;
}

}  // namespace ampify
