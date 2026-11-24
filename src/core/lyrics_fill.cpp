#include "lyrics_fill.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ampify {

namespace {

// A host shows its line a little before the line starts and keeps it a moment after it ends.
constexpr int64_t kLineAheadMs = 600;
constexpr int64_t kLineAfterMs = 1500;
constexpr int64_t kSameStartMs = 5;  // Apple's line start (100 ns) and the TTML's (ms) read from the same text
constexpr double kHoldMs = 1;  // the edge holds at a word's end until 1 ms before the next word, then jumps
constexpr double kPronunciationShare = 0.8;  // romaji rows are about 0.6 of a sung row's height
constexpr double kSameOffset = 1e-4;  // Apple's crisp edge: its two middle stops at the same place

// Adds a key after the last: one at the same time (or earlier, words that overlap) moves that time's edge instead.
void Push(std::vector<FillKey>& path, double atMs, double edge) {
    atMs = std::max(atMs, 0.0);
    if (!path.empty() && atMs <= path.back().atMs) {
        path.back().edge = edge;
        return;
    }
    path.push_back({atMs, edge});
}

}  // namespace

bool ShapedAsAppleFill(std::span<const FillStop> stops) {
    if (stops.size() != 4 || stops[0].offset != 0 || stops[3].offset != 1) return false;
    if (std::fabs(stops[1].offset - stops[2].offset) < kSameOffset) return true;
    const bool inOrder = stops[1].offset >= 0 && stops[1].offset <= stops[2].offset && stops[2].offset <= 1;
    return inOrder && stops[0].argb == stops[1].argb && stops[2].argb == stops[3].argb;
}

std::vector<LyricWord> WordsOfHost(const LyricLine& line, bool background) {
    std::vector<LyricWord> words;
    for (const LyricWord& word : line.words) {
        if (word.background == background) words.push_back(word);
    }
    return words;
}

std::vector<FillKey> RowFillPath(std::span<const LyricWord> words, std::span<const WordSpan> spans, int64_t lineStartMs) {
    std::vector<FillKey> path;
    const size_t count = std::min(words.size(), spans.size());
    if (count == 0) return path;
    Push(path, 0, spans[0].from);
    for (size_t i = 0; i < count; i++) {
        const double begin = static_cast<double>(words[i].startMs - lineStartMs);
        double end = static_cast<double>(words[i].endMs - lineStartMs);
        if (i + 1 < count) end = std::min(end, static_cast<double>(words[i + 1].startMs - lineStartMs));
        // A word that starts as the last ends jumps there 1 ms later: the arrival at the last word's end stays.
        const double from = i == 0 ? begin : std::max(begin, path.back().atMs + kHoldMs);
        end = std::max(end, from);
        Push(path, from, spans[i].from);
        Push(path, end, spans[i].to);
        if (i + 1 < count) {
            const double next = static_cast<double>(words[i + 1].startMs - lineStartMs);
            if (next - kHoldMs > end) Push(path, next - kHoldMs, spans[i].to);
        }
    }
    return path;
}

std::vector<WordSpan> SoftSpans(std::span<const WordSpan> spans, double width) {
    const double half = std::max(width, 0.0) / 2;
    std::vector<WordSpan> soft;
    soft.reserve(spans.size());
    for (const WordSpan& span : spans) {
        const double from = soft.empty() ? span.from - half : std::max(span.from - half, soft.back().to);
        soft.push_back({from, std::max(span.to + half, from)});
    }
    return soft;
}

double EdgeAt(std::span<const FillKey> path, double atMs) {
    if (path.empty()) return 0;
    if (atMs <= path.front().atMs) return path.front().edge;
    for (size_t i = 1; i < path.size(); i++) {
        if (atMs < path[i].atMs) {
            const FillKey& a = path[i - 1];
            const FillKey& b = path[i];
            return a.edge + (b.edge - a.edge) * (atMs - a.atMs) / (b.atMs - a.atMs);
        }
    }
    return path.back().edge;
}

std::vector<bool> PronunciationRows(std::span<const double> heights) {
    std::vector<bool> spoken(heights.size(), false);
    const double tallest = heights.empty() ? 0 : *std::max_element(heights.begin(), heights.end());
    if (!(tallest > 0)) return spoken;
    for (size_t i = 0; i < heights.size(); i++) spoken[i] = heights[i] < tallest * kPronunciationShare;
    return spoken;
}

bool LineUnderway(int64_t startMs, int64_t endMs, int64_t positionMs) {
    return positionMs >= startMs - kLineAheadMs && positionMs <= endMs + kLineAfterMs;
}

std::optional<size_t> LineOfHost(const Lyrics& lyrics, size_t wordCount, bool background, int64_t positionMs,
                                 std::optional<int64_t> appleStartMs) {
    if (!lyrics.synced || wordCount == 0) return std::nullopt;
    auto fits = [&](const LyricLine& line) {
        const auto own = std::count_if(line.words.begin(), line.words.end(),
                                       [background](const LyricWord& word) { return word.background == background; });
        return line.timing == LyricTiming::ByWord && static_cast<size_t>(own) == wordCount;
    };
    if (appleStartMs) {
        for (size_t i = 0; i < lyrics.lines.size(); i++) {
            const LyricLine& line = lyrics.lines[i];
            if (std::llabs(line.startMs - *appleStartMs) > kSameStartMs) continue;
            if (fits(line)) return i;
            return std::nullopt;  // Apple's line, another size: not what Ampify read
        }
    }
    std::optional<size_t> found;
    for (size_t i = 0; i < lyrics.lines.size(); i++) {
        const LyricLine& line = lyrics.lines[i];
        if (line.startMs > positionMs + kLineAheadMs) break;
        if (LineUnderway(line.startMs, line.endMs, positionMs) && fits(line)) found = i;
    }
    return found;
}

}  // namespace ampify
