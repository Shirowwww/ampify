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
constexpr double kUnbounded = 1e9;    // a stop past the row's first or last word is held by nothing but the row
constexpr double kLiftEase = 2;       // the rise: 1 - (1 - x)^kLiftEase, fitted on the iPhone's screen
constexpr int kLiftSteps = 16;        // LiftPath's keys: straight between them, off LiftAt by 1 / (4 * steps^2) at most

// Adds a key after the last: one at the same time (or earlier, words that overlap) moves that time's edge instead.
void Push(std::vector<FillKey>& path, double atMs, double edge) {
    atMs = std::max(atMs, 0.0);
    if (!path.empty() && atMs <= path.back().atMs) {
        path.back().edge = edge;
        return;
    }
    path.push_back({atMs, edge});
}

// Adds the stretch from (t0, v0) to (t1, v1), straight, held within low..high: a key where it meets either bound.
void PushClamped(std::vector<FillKey>& path, double t0, double v0, double t1, double v1, double low, double high) {
    auto held = [&](double v) { return std::clamp(v, low, high); };
    Push(path, t0, held(v0));
    if (t1 > t0 && v1 != v0) {
        double crossings[2];
        int found = 0;
        for (double bound : {low, high}) {
            const double share = (bound - v0) / (v1 - v0);
            if (share > 0 && share < 1) crossings[found++] = t0 + share * (t1 - t0);
        }
        if (found == 2 && crossings[1] < crossings[0]) std::swap(crossings[0], crossings[1]);
        for (int i = 0; i < found; i++) Push(path, crossings[i], held(v0 + (v1 - v0) * (crossings[i] - t0) / (t1 - t0)));
    }
    Push(path, t1, held(v1));
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

SoftFillPaths SoftRowFillPaths(std::span<const LyricWord> words, std::span<const WordSpan> spans, int64_t lineStartMs,
                               double width) {
    SoftFillPaths paths;
    const size_t count = std::min(words.size(), spans.size());
    if (count == 0) return paths;
    const double half = std::max(width, 0.0) / 2;
    auto at = [&](int64_t ms) { return static_cast<double>(ms - lineStartMs); };
    // The soft part's centre, `edge`, crosses word i from half a width before it to half a width past it; each stop
    // follows it, half a width away, held within the bounds of the word being sung.
    double sungLow = -kUnbounded;
    double sungHigh = 0;
    double unsungLow = 0;
    double unsungHigh = kUnbounded;
    auto segment = [&](double t0, double edge0, double t1, double edge1) {
        PushClamped(paths.sung, t0, edge0 - half, t1, edge1 - half, sungLow, sungHigh);
        PushClamped(paths.unsung, t0, edge0 + half, t1, edge1 + half, unsungLow, unsungHigh);
    };
    for (size_t i = 0; i < count; i++) {
        sungLow = i == 0 ? -kUnbounded : spans[i - 1].to;
        sungHigh = spans[i].to;
        unsungLow = spans[i].from;
        unsungHigh = i + 1 < count ? spans[i + 1].from : kUnbounded;
        const double begin = std::max(at(words[i].startMs), 0.0);
        const double last = std::max(paths.sung.empty() ? 0.0 : paths.sung.back().atMs,
                                     paths.unsung.empty() ? 0.0 : paths.unsung.back().atMs);
        const double from = i == 0 ? begin : std::max(begin, last + kHoldMs);
        double end = at(words[i].endMs);
        if (i + 1 < count) end = std::min(end, at(words[i + 1].startMs));
        end = std::max(end, from);
        if (i == 0) segment(0, spans[0].from - half, from, spans[0].from - half);
        segment(from, spans[i].from - half, end, spans[i].to + half);
        if (i + 1 < count) {
            const double next = at(words[i + 1].startMs);
            if (next - kHoldMs > end) segment(end, spans[i].to + half, next - kHoldMs, spans[i].to + half);
        }
    }
    return paths;
}

double LiftDurationMs(double startMs, double endMs) { return std::max(kLiftMinMs, endMs - startMs); }

double LiftAt(double startMs, double endMs, double atMs) {
    const double x = std::clamp((atMs - startMs) / LiftDurationMs(startMs, endMs), 0.0, 1.0);
    return 1 - std::pow(1 - x, kLiftEase);
}

std::vector<FillKey> LiftPath(double startMs, double endMs) {
    std::vector<FillKey> path;
    const double span = LiftDurationMs(startMs, endMs);
    for (int step = 0; step <= kLiftSteps; step++) {
        const double atMs = std::max(startMs + span * step / kLiftSteps, 0.0);  // a word started before its line
        Push(path, atMs, LiftAt(startMs, endMs, atMs));
    }
    return path;
}

std::vector<WordSpan> SpansAlong(std::span<const WordSpan> spans, double startX, double endX) {
    std::vector<WordSpan> along;
    if (std::fabs(endX - startX) < kSameOffset) return along;
    auto at = [&](double x) { return (x - startX) / (endX - startX); };
    for (const WordSpan& span : spans) {
        const double a = at(span.from);
        const double b = at(span.to);
        along.push_back({std::min(a, b), std::max(a, b)});
    }
    return along;
}

std::optional<RowFillPlan> RowFillPlan::Of(std::span<const LyricWord> words, std::span<const WordSpan> spans,
                                           int64_t lineStartMs, double softWidth) {
    for (size_t i = 1; i < spans.size(); i++) {
        if (spans[i].from < spans[i - 1].from - kSameOffset || spans[i].to < spans[i - 1].to - kSameOffset) {
            return std::nullopt;
        }
    }
    if (softWidth > 0) {
        SoftFillPaths soft = SoftRowFillPaths(words, spans, lineStartMs, softWidth);
        if (soft.sung.empty() || soft.unsung.empty()) return std::nullopt;
        return RowFillPlan(std::move(soft.sung), std::move(soft.unsung));
    }
    std::vector<FillKey> edge = RowFillPath(words, spans, lineStartMs);
    if (edge.empty()) return std::nullopt;
    return RowFillPlan(std::move(edge), {});
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
