// apple.lyricsFill: where the edge of a row's gradient stands as the line is sung (src/core/lyrics_fill.hpp).

#include <cmath>
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

TEST_CASE(LyricsFill_ALineIsUnderWayFromJustBeforeItUntilAMomentAfter) {
    CHECK(!ampify::LineUnderway(1000, 2000, 0));
    CHECK(ampify::LineUnderway(1000, 2000, 500));    // about to start
    CHECK(ampify::LineUnderway(1000, 2000, 1500));
    CHECK(ampify::LineUnderway(1000, 2000, 3000));   // just ended
    CHECK(!ampify::LineUnderway(1000, 2000, 4000));  // long gone: a background row goes back to Apple
}
