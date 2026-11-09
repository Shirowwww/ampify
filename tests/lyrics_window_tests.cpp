#include <vector>

#include "../src/core/lyrics_window.hpp"
#include "test.hpp"

using ampify::kAllLines;
using ampify::LyricLineShown;
using ampify::LyricLinesOf;
using ampify::LyricsCentringShift;
using ampify::SungLines;

namespace {

SungLines One(int line) { return {line, line}; }

}  // namespace

TEST_CASE(LyricsWindow_AllLinesShowByDefault) {
    for (int index = 0; index < 20; ++index) CHECK(LyricLineShown(index, One(7), kAllLines, kAllLines));
}

TEST_CASE(LyricsWindow_OneAboveAndTwoBelowTheSungLine) {
    CHECK(!LyricLineShown(5, One(7), 1, 2));
    CHECK(LyricLineShown(6, One(7), 1, 2));
    CHECK(LyricLineShown(7, One(7), 1, 2));
    CHECK(LyricLineShown(9, One(7), 1, 2));
    CHECK(!LyricLineShown(10, One(7), 1, 2));
}

// A duet sings two lines at once: "before" counts from the first of them, "after" from the last.
TEST_CASE(LyricsWindow_TwoLinesSungAtOnceBothShowWithTheirSurroundings) {
    const SungLines duet{7, 8};
    CHECK(!LyricLineShown(5, duet, 1, 1));
    CHECK(LyricLineShown(6, duet, 1, 1));
    CHECK(LyricLineShown(7, duet, 1, 1));
    CHECK(LyricLineShown(8, duet, 1, 1));
    CHECK(LyricLineShown(9, duet, 1, 1));
    CHECK(!LyricLineShown(10, duet, 1, 1));
    CHECK(LyricLineShown(8, duet, 0, 0));  // with no line around, both sung lines still show
}

TEST_CASE(LyricsWindow_BeforeTheFirstLineOnlyTheComingOnesShow) {
    CHECK(LyricLineShown(0, SungLines{}, 0, 2));
    CHECK(LyricLineShown(1, SungLines{}, 0, 2));
    CHECK(!LyricLineShown(2, SungLines{}, 0, 2));
}

TEST_CASE(LyricsWindow_TheBlockMovesSoTheSungLineStandsInItsMiddle) {
    const std::vector<double> heights{40, 40, 80, 40, 40, 40};
    // Three above (40 + 80 + 40 with their gaps of 10), one below (40 + 10): the block moves down by half the rest.
    CHECK_NEAR(LyricsCentringShift(heights, 10, One(4), 3, 1), (190.0 - 50.0) / 2, 1e-9);
    // As many lines on both sides of the same height: nothing moves.
    CHECK_NEAR(LyricsCentringShift(heights, 10, One(4), 1, 1), 0, 1e-9);
    // Near the start, a side holds fewer lines than asked.
    CHECK_NEAR(LyricsCentringShift(heights, 10, One(0), 2, 1), -25, 1e-9);
    // A duet: the lines around are counted from either end of the two sung ones.
    CHECK_NEAR(LyricsCentringShift(heights, 10, SungLines{3, 4}, 1, 1), (90.0 - 50.0) / 2, 1e-9);
}

TEST_CASE(LyricsWindow_NothingMovesWhileASideShowsAllOrNothingIsSung) {
    const std::vector<double> heights{40, 40, 40};
    CHECK_NEAR(LyricsCentringShift(heights, 10, One(1), kAllLines, 1), 0, 1e-9);
    CHECK_NEAR(LyricsCentringShift(heights, 10, SungLines{}, 1, 1), 0, 1e-9);
}

TEST_CASE(LyricsWindow_ASettingReadsAllOrZeroToFive) {
    CHECK(LyricLinesOf("all") == kAllLines);
    CHECK(LyricLinesOf("") == kAllLines);
    CHECK(LyricLinesOf("0") == 0);
    CHECK(LyricLinesOf("5") == 5);
    CHECK(LyricLinesOf("6") == kAllLines);
    CHECK(LyricLinesOf("12") == kAllLines);
}
