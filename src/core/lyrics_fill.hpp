#pragma once

// apple.lyricsFill: Apple's word-by-word lyrics light each row of the line being sung with a gradient whose edge it
// moves in steps, and leaves the end of a word dark until the next one starts (issue #5). Ampify plans where that edge
// stands at every moment of the line, from the words' times and where each word lies on its row, so the compositor can
// move it at the screen's pace. Portable; the Windows side is src/win/lyrics_fill.cpp.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "lyrics.hpp"

namespace ampify {

// A word's left and right edges on its row, as parts of the row's width (0 to 1).
struct WordSpan {
    double from = 0;
    double to = 0;
};

// One point of the edge's path: `atMs` after the line's start, the edge stands at `edge` (0 to 1 of the row).
struct FillKey {
    double atMs = 0;
    double edge = 0;
};

// The words of `line` that one of Apple's hosts draws: the main vocals, or the background ones.
std::vector<LyricWord> WordsOfHost(const LyricLine& line, bool background);

// The edge's path over one row whose words are `words`, lying at `spans` (same size, in order). The edge waits at the
// first word's left edge, crosses each word while it is sung, holds at its right edge until the next word starts and
// jumps there. Times are from `lineStartMs`, never negative and rising; never empty when `words` is not.
std::vector<FillKey> RowFillPath(std::span<const LyricWord> words, std::span<const WordSpan> spans, int64_t lineStartMs);

// Where the edge of `path` stands `atMs` after the line's start: linear between keys, the first before them, the last
// after. 0 for an empty path.
double EdgeAt(std::span<const FillKey> path, double atMs);

// True while a line from `startMs` to `endMs` may be on show as the one sung: from a little before it starts to a
// moment after it ends.
bool LineUnderway(int64_t startMs, int64_t endMs, int64_t positionMs);

// The line of `lyrics` that a host of `wordCount` words shows while the song is at `positionMs`: the latest line
// started (or about to) whose words for that host (main or background) number `wordCount` and that has not long
// ended. nullopt when none fits: the host is then left to Apple.
std::optional<size_t> LineOfHost(const Lyrics& lyrics, size_t wordCount, bool background, int64_t positionMs);

}  // namespace ampify
