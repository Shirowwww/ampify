#include "lyrics_fill.hpp"

#include <algorithm>

namespace ampify {

namespace {

// A host shows its line a little before the line starts and keeps it a moment after it ends.
constexpr int64_t kLineAheadMs = 600;
constexpr int64_t kLineAfterMs = 1500;
constexpr double kHoldMs = 1;  // the edge holds at a word's end until 1 ms before the next word, then jumps

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

bool LineUnderway(int64_t startMs, int64_t endMs, int64_t positionMs) {
    return positionMs >= startMs - kLineAheadMs && positionMs <= endMs + kLineAfterMs;
}

std::optional<size_t> LineOfHost(const Lyrics& lyrics, size_t wordCount, bool background, int64_t positionMs) {
    if (!lyrics.synced || wordCount == 0) return std::nullopt;
    std::optional<size_t> found;
    for (size_t i = 0; i < lyrics.lines.size(); i++) {
        const LyricLine& line = lyrics.lines[i];
        if (line.timing != LyricTiming::ByWord) continue;
        if (line.startMs > positionMs + kLineAheadMs) break;
        if (!LineUnderway(line.startMs, line.endMs, positionMs)) continue;
        const auto own = std::count_if(line.words.begin(), line.words.end(),
                                       [background](const LyricWord& word) { return word.background == background; });
        if (static_cast<size_t>(own) == wordCount) found = i;
    }
    return found;
}

}  // namespace ampify
