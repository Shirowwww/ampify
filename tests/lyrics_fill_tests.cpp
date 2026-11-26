// apple.lyricsFill: where the edge of a row's gradient stands as the line is sung (src/core/lyrics_fill.hpp).

#include <cmath>
#include <utility>
#include <vector>

#include "../src/core/lyrics_fill.hpp"
#include "test.hpp"

namespace {

using ampify::EdgeAt;
using ampify::FillKey;
using ampify::LineOfHost;
using ampify::LyricWord;
using ampify::RowFillPath;
using ampify::WordSpan;

bool Near(double a, double b) { return std::fabs(a - b) < 0.005; }  // a jump between words takes 1 ms

LyricWord Word(int64_t start, int64_t end, bool background = false) {
    LyricWord word;
    word.startMs = start;
    word.endMs = end;
    word.text = "w";
    word.background = background;
    return word;
}

ampify::LyricLine Line(int64_t start, int64_t end, std::vector<LyricWord> words) {
    ampify::LyricLine line;
    line.startMs = start;
    line.endMs = end;
    line.words = std::move(words);
    line.timing = ampify::LyricTiming::ByWord;
    return line;
}

}  // namespace

TEST_CASE(LyricsFill_TheEdgeCrossesEachWordWhileItIsSung) {
    std::vector<LyricWord> words{Word(1000, 1400), Word(1400, 2000)};
    std::vector<WordSpan> spans{{0.0, 0.4}, {0.5, 1.0}};
    std::vector<FillKey> path = RowFillPath(words, spans, 1000);
    CHECK(!path.empty());
    CHECK(Near(EdgeAt(path, 0), 0.0));
    CHECK(Near(EdgeAt(path, 200), 0.2));    // halfway through the first word
    CHECK(Near(EdgeAt(path, 400), 0.4));    // the first word sung whole
    CHECK(Near(EdgeAt(path, 401), 0.5));    // the next one starts right away: the edge jumps to it
    CHECK(Near(EdgeAt(path, 700), 0.75));   // halfway through the second
    CHECK(Near(EdgeAt(path, 1000), 1.0));   // done
    CHECK(Near(EdgeAt(path, 5000), 1.0));   // and stays lit
}

TEST_CASE(LyricsFill_TheEdgeHoldsAtAWordsEndUntilTheNextWordStarts) {
    // Apple left the end of a word dark until the next one: Ampify lights it whole, then waits.
    std::vector<LyricWord> words{Word(0, 300), Word(1000, 1200)};
    std::vector<WordSpan> spans{{0.0, 0.3}, {0.6, 1.0}};
    std::vector<FillKey> path = RowFillPath(words, spans, 0);
    CHECK(Near(EdgeAt(path, 300), 0.3));
    CHECK(Near(EdgeAt(path, 900), 0.3));
    CHECK(Near(EdgeAt(path, 1000), 0.6));
    CHECK(Near(EdgeAt(path, 1100), 0.8));
}

TEST_CASE(LyricsFill_ARowWaitsForItsFirstWordAndTimesNeverGoBack) {
    // The second row of a line starts later; a word that overlaps the next one ends where the next begins.
    std::vector<LyricWord> words{Word(2000, 2600), Word(2400, 3000)};
    std::vector<WordSpan> spans{{0.1, 0.5}, {0.6, 0.9}};
    std::vector<FillKey> path = RowFillPath(words, spans, 1000);
    CHECK(Near(EdgeAt(path, 500), 0.1));
    for (size_t i = 1; i < path.size(); i++) CHECK(path[i].atMs > path[i - 1].atMs);
    CHECK(Near(EdgeAt(path, 1401), 0.6));  // the second word started: its own crossing wins
    CHECK(Near(EdgeAt(path, 2000), 0.9));
    CHECK(RowFillPath({}, {}, 0).empty());
    CHECK(Near(EdgeAt({}, 10), 0.0));
}

// apple.lyricsFill = soft: the colour turns from sung to unsung between two stops, each with a path of its own.
TEST_CASE(LyricsFill_ASoftEdgeFadesEachWordWithoutReachingTheNextOne) {
    // Issue #5: the soft part spilled onto the next word while it waited to be sung.
    std::vector<LyricWord> words{Word(0, 300), Word(1000, 1200)};
    std::vector<WordSpan> spans{{0.0, 0.3}, {0.35, 0.7}};  // a space narrower than the soft part
    ampify::SoftFillPaths soft = ampify::SoftRowFillPaths(words, spans, 0, 0.1);
    CHECK(!soft.sung.empty() && !soft.unsung.empty());
    CHECK(EdgeAt(soft.unsung, 0) <= 0.0 + 1e-9);   // the first word wholly unsung as it starts
    CHECK(Near(EdgeAt(soft.sung, 150), 0.10));       // halfway: the soft part, its whole width, inside the word
    CHECK(Near(EdgeAt(soft.unsung, 150), 0.20));
    CHECK(Near(EdgeAt(soft.sung, 300), 0.3));        // the first word sung whole at its end
    CHECK(EdgeAt(soft.unsung, 300) <= 0.35 + 1e-9);  // and the next one still wholly unsung
    CHECK(Near(EdgeAt(soft.sung, 900), 0.3));        // while it waits
    CHECK(EdgeAt(soft.unsung, 900) <= 0.35 + 1e-9);
    CHECK(Near(EdgeAt(soft.unsung, 1100), 0.575));   // then it fades in turn
    CHECK(Near(EdgeAt(soft.sung, 1200), 0.7));
    CHECK(ampify::SoftRowFillPaths({}, {}, 0, 0.1).sung.empty());
}

TEST_CASE(LyricsFill_ASoftEdgeKeepsItsWidthInAWideSpace) {
    std::vector<LyricWord> words{Word(0, 300), Word(1000, 1200)};
    std::vector<WordSpan> spans{{0.0, 0.3}, {0.6, 1.0}};
    ampify::SoftFillPaths soft = ampify::SoftRowFillPaths(words, spans, 0, 0.1);
    CHECK(Near(EdgeAt(soft.sung, 500), 0.3));  // past the sung word, as wide as ever, short of the next one
    CHECK(Near(EdgeAt(soft.unsung, 500), 0.4));
    CHECK(Near(EdgeAt(soft.sung, 1000), 0.5));  // the next word starts: the soft part stands just before it
    CHECK(Near(EdgeAt(soft.unsung, 1000), 0.6));
}

TEST_CASE(LyricsFill_ASoftEdgesStopsNeverGoBackNorCross) {
    // Words back to back, overlapping, and apart; a row that starts late.
    std::vector<LyricWord> words{Word(1500, 1900), Word(1900, 2100), Word(2000, 2600), Word(3000, 3100)};
    std::vector<WordSpan> spans{{0.02, 0.2}, {0.23, 0.3}, {0.32, 0.7}, {0.9, 0.98}};
    ampify::SoftFillPaths soft = ampify::SoftRowFillPaths(words, spans, 1000, 0.12);
    for (const auto* path : {&soft.sung, &soft.unsung}) {
        for (size_t i = 1; i < path->size(); i++) {
            CHECK((*path)[i].atMs > (*path)[i - 1].atMs);
            CHECK((*path)[i].edge >= (*path)[i - 1].edge - 1e-9);
        }
    }
    for (double at = 0; at <= 2500; at += 5) CHECK(EdgeAt(soft.sung, at) <= EdgeAt(soft.unsung, at) + 1e-9);
    CHECK(EdgeAt(soft.unsung, 400) <= 0.02 + 1e-9);  // nothing lit before the row's first word
    CHECK(EdgeAt(soft.sung, 2200) >= 0.98 - 1e-9);   // all of it sung at the end
}

// The iPhone app lifts a word from its start, over a second or its own length when longer, slowing down.
TEST_CASE(LyricsFill_AWordRisesFromItsStartOverASecondOrItsLength) {
    CHECK(Near(ampify::LiftAt(1000, 1250, 900), 0.0));
    CHECK(Near(ampify::LiftAt(1000, 1250, 1500), 0.75));  // a short word: half its second, three quarters up
    CHECK(Near(ampify::LiftAt(1000, 1250, 2000), 1.0));
    CHECK(Near(ampify::LiftAt(0, 3000, 1500), 0.75));     // a long word rises over its own length
    CHECK(Near(ampify::LiftAt(0, 3000, 9000), 1.0));
    CHECK(Near(ampify::LiftDurationMs(0, 250), 1000));
}

// The compositor draws LiftPath, straight between its keys: the same curve as LiftAt.
TEST_CASE(LyricsFill_TheRiseDrawnFollowsTheRiseTested) {
    for (const auto& [start, end] : {std::pair{400.0, 650.0}, std::pair{0.0, 3000.0}, std::pair{-300.0, 200.0}}) {
        const std::vector<FillKey> path = ampify::LiftPath(start, end);
        CHECK(!path.empty() && path.front().atMs >= 0);
        for (size_t i = 1; i < path.size(); i++) CHECK(path[i].atMs > path[i - 1].atMs);
        for (double at = 0; at <= end + 2000; at += 7) {
            CHECK(std::fabs(EdgeAt(path, at) - ampify::LiftAt(start, end, at)) < 0.002);
        }
    }
}

// A row's plan always has its paths: none for a row with no word, one shared by both stops for the crisp edge.
TEST_CASE(LyricsFill_ARowPlanIsWholeOrNone) {
    std::vector<LyricWord> words{Word(0, 300), Word(1000, 1200)};
    std::vector<WordSpan> spans{{0.0, 0.3}, {0.35, 0.7}};
    CHECK(!ampify::RowFillPlan::Of({}, {}, 0, 0).has_value());
    CHECK(!ampify::RowFillPlan::Of({}, {}, 0, 0.1).has_value());
    const auto crisp = ampify::RowFillPlan::Of(words, spans, 0, 0);
    CHECK(crisp && !crisp->Soft() && !crisp->Sung().empty() && &crisp->Unsung() == &crisp->Sung());
    const auto soft = ampify::RowFillPlan::Of(words, spans, 0, 0.1);
    CHECK(soft && soft->Soft() && !soft->Sung().empty() && !soft->Unsung().empty());
    CHECK(Near(soft->LastMs(), 1200));
}

// Right-to-left lyrics: words laid from the right are in order along a gradient drawn from the right, and a row whose
// words do not follow one another along its gradient is left to Apple (the stops would cross).
TEST_CASE(LyricsFill_RightToLeftWordsAreReadAlongTheGradient) {
    std::vector<LyricWord> words{Word(0, 300), Word(1000, 1200)};
    std::vector<WordSpan> fromRight{{0.7, 1.0}, {0.3, 0.65}};
    const std::vector<WordSpan> along = ampify::SpansAlong(fromRight, 1, 0);
    CHECK(along.size() == 2 && Near(along[0].from, 0.0) && Near(along[0].to, 0.3));
    CHECK(Near(along[1].from, 0.35) && Near(along[1].to, 0.7));
    CHECK(ampify::RowFillPlan::Of(words, along, 0, 0.1).has_value());
    CHECK(!ampify::RowFillPlan::Of(words, ampify::SpansAlong(fromRight, 0, 1), 0, 0.1).has_value());
    CHECK(!ampify::RowFillPlan::Of(words, ampify::SpansAlong(fromRight, 0, 1), 0, 0).has_value());
    CHECK(ampify::SpansAlong(fromRight, 0.5, 0.5).empty());
}

TEST_CASE(LyricsFill_ApplesGradientIsKnownWhetherItsEdgeIsCrispOrSpread) {
    using Stops = std::vector<ampify::FillStop>;
    constexpr uint32_t kSung = 0xD8FFFFFF;
    constexpr uint32_t kUnsung = 0x3FFFFFFF;
    const Stops crisp{{0, kSung}, {0.4, kSung}, {0.4, kUnsung}, {1, kUnsung}};
    CHECK(ampify::ShapedAsAppleFill(crisp));
    // Paused, Apple spreads its edge over the word, behind Ampify's gradient: still Apple's, Ampify takes it again.
    const Stops spread{{0, kSung}, {0.891, kSung}, {0.975, kUnsung}, {1, kUnsung}};
    CHECK(ampify::ShapedAsAppleFill(spread));
    const Stops fadedEnds{{0, 0}, {0.1, kSung}, {0.9, kSung}, {1, 0}};  // not a sung and an unsung colour
    CHECK(!ampify::ShapedAsAppleFill(fadedEnds));
    const Stops backwards{{0, kSung}, {0.6, kSung}, {0.4, kUnsung}, {1, kUnsung}};
    CHECK(!ampify::ShapedAsAppleFill(backwards));
    const Stops three{{0, kSung}, {0.5, kUnsung}, {1, kUnsung}};
    CHECK(!ampify::ShapedAsAppleFill(three));
    const Stops shifted{{0.1, kSung}, {0.4, kSung}, {0.4, kUnsung}, {1, kUnsung}};
    CHECK(!ampify::ShapedAsAppleFill(shifted));
}

TEST_CASE(LyricsFill_BackgroundVocalsAreTheirOwnHost) {
    ampify::Lyrics lyrics = ampify::ParseTtml(
        "<tt><body><div><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">Main</span> <span begin=\"2\" end=\"3\">line</span>"
        "<span ttm:role=\"x-bg\"><span begin=\"2.2\" end=\"2.8\">(ooh)</span></span></p></div></body></tt>");
    CHECK(lyrics.lines.size() == 1);
    if (lyrics.lines.size() != 1) return;
    const ampify::LyricLine& line = lyrics.lines[0];
    CHECK(line.words.size() == 3);
    CHECK(ampify::WordsOfHost(line, false).size() == 2);
    CHECK(ampify::WordsOfHost(line, true).size() == 1);
    CHECK(LineOfHost(lyrics, 2, false, 1500) == std::optional<size_t>{0});
    CHECK(LineOfHost(lyrics, 1, true, 2500) == std::optional<size_t>{0});
}

TEST_CASE(LyricsFill_AHostIsMatchedToTheLatestLineOfItsSizeThatIsStillUnderWay) {
    ampify::Lyrics lyrics;
    lyrics.synced = true;
    lyrics.lines.push_back(Line(1000, 3000, {Word(1000, 2000), Word(2000, 3000)}));
    lyrics.lines.push_back(Line(3000, 5000, {Word(3000, 4000), Word(4000, 4500), Word(4500, 5000)}));
    lyrics.lines.push_back(Line(5000, 7000, {Word(5000, 6000), Word(6000, 7000)}));
    CHECK(LineOfHost(lyrics, 2, false, 2000) == std::optional<size_t>{0});
    CHECK(LineOfHost(lyrics, 3, false, 3500) == std::optional<size_t>{1});
    CHECK(LineOfHost(lyrics, 2, false, 4800) == std::optional<size_t>{2});  // the next line is about to start
    CHECK(LineOfHost(lyrics, 2, false, 3200) == std::optional<size_t>{0});  // the first has just ended
    CHECK(!LineOfHost(lyrics, 4, false, 3500));                             // no line of four words: Apple's own
    CHECK(!LineOfHost(lyrics, 2, false, 20000));                            // long after the last line
    lyrics.synced = false;
    CHECK(!LineOfHost(lyrics, 2, false, 2000));
}

// "One more time" sung three times: the times alone take the next line as soon as it is about to start, and a row
// Apple rebuilds then would fill again from its start. The start Apple gave the row's line settles which it is.
TEST_CASE(LyricsFill_AHostIsMatchedByTheStartAppleGaveItsLine) {
    ampify::Lyrics lyrics;
    lyrics.synced = true;
    lyrics.lines.push_back(Line(1000, 3000, {Word(1000, 2000), Word(2000, 3000)}));
    lyrics.lines.push_back(Line(3000, 5000, {Word(3000, 4000), Word(4000, 5000)}));
    lyrics.lines.push_back(Line(5000, 7000, {Word(5000, 6000), Word(6000, 7000)}));
    CHECK(LineOfHost(lyrics, 2, false, 2800) == std::optional<size_t>{1});  // by the times: the next one
    CHECK(LineOfHost(lyrics, 2, false, 2800, 1000) == std::optional<size_t>{0});
    CHECK(LineOfHost(lyrics, 2, false, 4900, 5000) == std::optional<size_t>{2});
    CHECK(!LineOfHost(lyrics, 3, false, 2800, 1000));  // the line Apple names has another size: Apple's own
    CHECK(LineOfHost(lyrics, 2, false, 2800, 99000) == std::optional<size_t>{1});  // no line starts there: by times
}

TEST_CASE(LyricsFill_PronunciationRowsAreTheShorterOnes) {
    using Rows = std::vector<bool>;
    const std::vector<double> sung = {38, 38};
    CHECK(ampify::PronunciationRows(sung) == (Rows{false, false}));  // no pronunciation shown
    const std::vector<double> romaji = {38, 21, 38, 21};             // each sung row with its romaji under it
    CHECK(ampify::PronunciationRows(romaji) == (Rows{false, true, false, true}));
    const std::vector<double> close = {38, 35};  // a row of other letters, a little shorter: still sung
    CHECK(ampify::PronunciationRows(close) == (Rows{false, false}));
    CHECK(ampify::PronunciationRows(std::vector<double>{}).empty());
    const std::vector<double> flat = {0, 0};  // not laid out yet
    CHECK(ampify::PronunciationRows(flat) == (Rows{false, false}));
}

TEST_CASE(LyricsFill_ALineIsUnderWayFromJustBeforeItUntilAMomentAfter) {
    CHECK(!ampify::LineUnderway(1000, 2000, 0));
    CHECK(ampify::LineUnderway(1000, 2000, 500));    // about to start
    CHECK(ampify::LineUnderway(1000, 2000, 1500));
    CHECK(ampify::LineUnderway(1000, 2000, 3000));   // just ended
    CHECK(!ampify::LineUnderway(1000, 2000, 4000));  // long gone: a background row goes back to Apple
}
