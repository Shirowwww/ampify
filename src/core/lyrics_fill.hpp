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

// One stop of a row's gradient: where it stands (0 to 1) and its colour, as 0xAARRGGBB.
struct FillStop {
    double offset = 0;
    uint32_t argb = 0;
};

// Whether a row's gradient is drawn as Apple's word-by-word fill: four stops from 0 to 1, the sung colour up to the
// edge and the unsung one after it. Apple keeps its edge crisp while it sings (the middle stops together), and spreads
// it over the word in other states, paused for one, with its two colours still in pairs: that is Apple's fill too.
bool ShapedAsAppleFill(std::span<const FillStop> stops);

// The words of `line` that one of Apple's hosts draws: the main vocals, or the background ones.
std::vector<LyricWord> WordsOfHost(const LyricLine& line, bool background);

// The edge's path over one row whose words are `words`, lying at `spans` (same size, in order). The edge waits at the
// first word's left edge, crosses each word while it is sung, holds at its right edge until the next word starts and
// jumps there. Times are from `lineStartMs`, never negative and rising; never empty when `words` is not.
std::vector<FillKey> RowFillPath(std::span<const LyricWord> words, std::span<const WordSpan> spans, int64_t lineStartMs);

// With a soft edge (apple.lyricsFill = soft, as the iPhone app draws it), the colour turns from sung to unsung over
// `width` (a part of the row) centred on the edge. The spans the edge then crosses: each word widened by half that
// width on both sides, so it starts wholly unsung and ends wholly sung, and none starting before the previous one ends,
// so the edge never steps back across a space narrower than the soft part.
std::vector<WordSpan> SoftSpans(std::span<const WordSpan> spans, double width);

// Where the edge of `path` stands `atMs` after the line's start: linear between keys, the first before them, the last
// after. 0 for an empty path.
double EdgeAt(std::span<const FillKey> path, double atMs);

// With Show Pronunciation, Apple draws a line's romaji as rows of their own in the line's host, in a smaller type,
// each under the row it spells. Which rows of these heights (top to bottom) are pronunciation: the ones clearly
// shorter than the tallest.
std::vector<bool> PronunciationRows(std::span<const double> heights);

// True while a line from `startMs` to `endMs` may be on show as the one sung: from a little before it starts to a
// moment after it ends.
bool LineUnderway(int64_t startMs, int64_t endMs, int64_t positionMs);

// The line of `lyrics` that a host of `wordCount` words shows while the song is at `positionMs`. With `appleStartMs`,
// the start Apple gave the host's line, the line that starts there (lines sung twice in a row stay apart); without it,
// or when no line starts there, the latest line started (or about to) whose words for that host (main or background)
// number `wordCount` and that has not long ended. nullopt when none fits: the host is then left to Apple.
std::optional<size_t> LineOfHost(const Lyrics& lyrics, size_t wordCount, bool background, int64_t positionMs,
                                 std::optional<int64_t> appleStartMs = std::nullopt);

}  // namespace ampify
