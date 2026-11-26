#pragma once

// apple.lyricsFill: Apple's word-by-word lyrics light each row of the line being sung with a gradient whose edge it
// moves in steps, and leaves the end of a word dark until the next one starts (issue #5). Ampify plans where that edge
// stands at every moment of the line, from the words' times and where each word lies on its row, so the compositor can
// move it at the screen's pace. Portable; the Windows side is src/win/lyrics_fill.cpp.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
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
// `width` (a part of the row) between the gradient's two middle stops, each with a path of its own. The iPhone app
// fades each word through a gradient of its own (issue #5, measured on its screen): a word starts wholly unsung and
// ends wholly sung, and the soft part never reaches the next word while it waits to be sung. One gradient over the row
// draws the same thing when its stops stay within the word being sung: the sung stop between the previous word's end
// and this word's end, the unsung stop between this word's start and the next word's start. In a space wider than
// `width` the soft part keeps its width; in a narrower one it spans the space.
struct SoftFillPaths {
    std::vector<FillKey> sung;    // where the sung colour ends
    std::vector<FillKey> unsung;  // where the unsung colour starts
};
SoftFillPaths SoftRowFillPaths(std::span<const LyricWord> words, std::span<const WordSpan> spans, int64_t lineStartMs,
                               double width);

// The iPhone app lifts each word a little as it is sung: from the word's start, over a second or the word's length when
// longer, quickly at first and slowing down (measured on its screen, issue #5). Apple Music on Windows lifts it too,
// but only as its own clock, which moves once a second, reaches the word: late, in a few hundred ms, and again at the
// next tick. How far a word starting `startMs` and ending `endMs` has risen `atMs` (all from the line's start), from 0
// to 1, over LiftDurationMs.
inline constexpr double kLiftMinMs = 1000;
double LiftAt(double startMs, double endMs, double atMs);
// The length of that rise: kLiftMinMs, or the word's own length when longer.
double LiftDurationMs(double startMs, double endMs);
// The same rise as keys the compositor moves through, straight between them (within a thousandth of LiftAt): the curve
// the screen draws comes from here.
std::vector<FillKey> LiftPath(double startMs, double endMs);

// Where words lying at `spans` across a row (parts of its width, left to right) stand along a gradient that runs from
// `startX` to `endX` of the row: a gradient drawn right to left measures from the right, each span's ends swapped.
// Empty when the gradient does not cross the row.
std::vector<WordSpan> SpansAlong(std::span<const WordSpan> spans, double startX, double endX);

// What one row's gradient follows: one edge for both middle stops (Apple's crisp edge, RowFillPath), or with a soft
// edge a path for each (SoftRowFillPaths). Only Of makes one, and its paths are never empty.
class RowFillPlan {
public:
    // The plan for a row of `words` lying at `spans` along its gradient (SpansAlong), with a soft edge `softWidth` wide
    // (0: crisp). nullopt for a row with no word, or whose words do not follow one another along the gradient: the
    // edge would run back, and the stops cross.
    static std::optional<RowFillPlan> Of(std::span<const LyricWord> words, std::span<const WordSpan> spans,
                                         int64_t lineStartMs, double softWidth);

    bool Soft() const { return !m_unsung.empty(); }
    // Where the sung colour ends, and where the unsung one starts: the same path for the crisp edge.
    const std::vector<FillKey>& Sung() const { return m_sung; }
    const std::vector<FillKey>& Unsung() const { return Soft() ? m_unsung : m_sung; }
    // When the last of its paths stops moving, from the line's start.
    double LastMs() const { return std::max(m_sung.back().atMs, Unsung().back().atMs); }

private:
    RowFillPlan(std::vector<FillKey> sung, std::vector<FillKey> unsung)
        : m_sung(std::move(sung)), m_unsung(std::move(unsung)) {}

    std::vector<FillKey> m_sung;
    std::vector<FillKey> m_unsung;  // empty for the crisp edge
};

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
