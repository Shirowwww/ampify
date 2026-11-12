#include "../src/core/lyrics_window.hpp"
#include "test.hpp"

using ampify::kAllLines;
using ampify::LyricLineShown;
using ampify::LyricLinesOf;
using ampify::LyricsCentringShift;
using ampify::SungSpan;
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

TEST_CASE(LyricsWindow_TheSungLineMovesToTheMiddleOfTheLyricsArea) {
    // Apple keeps the sung line about a third of the way down an area 900 high: it moves down to the middle.
    CHECK_NEAR(LyricsCentringShift(SungSpan{300, 360}, 900, 2, 2, true), 450 - 330, 1e-9);
    // Already in the middle: nothing moves.
    CHECK_NEAR(LyricsCentringShift(SungSpan{420, 480}, 900, 1, 1, true), 0, 1e-9);
    // Below it (the first lines of a song): up.
    CHECK_NEAR(LyricsCentringShift(SungSpan{600, 660}, 900, 0, 3, true), 450 - 630, 1e-9);
    // A duet: the middle of both sung lines.
    CHECK_NEAR(LyricsCentringShift(SungSpan{300, 420}, 900, 2, 2, true), 90, 1e-9);
}

TEST_CASE(LyricsWindow_NothingMovesWhileASideShowsAllOrNothingIsSung) {
    CHECK_NEAR(LyricsCentringShift(SungSpan{300, 360}, 900, kAllLines, 1, true), 0, 1e-9);
    CHECK_NEAR(LyricsCentringShift(SungSpan{300, 360}, 900, 1, 1, false), 0, 1e-9);
    CHECK_NEAR(LyricsCentringShift(SungSpan{300, 360}, 0, 1, 1, true), 0, 1e-9);
}

TEST_CASE(LyricsWindow_AScrollBeforeItsLineLightsIsNotFollowed) {
    // Apple scrolls, then lights the line: the scroll comes seconds after the line before it.
    CHECK(!ampify::ScrollFollowsSungLine(2400));
    CHECK(!ampify::ScrollFollowsSungLine(ampify::kScrollAfterLineMs));
    // A scroll right after its line is the one that brings it in place.
    CHECK(ampify::ScrollFollowsSungLine(0));
    CHECK(ampify::ScrollFollowsSungLine(40));
}

TEST_CASE(LyricsWindow_ASettingReadsAllOrZeroToFive) {
    CHECK(LyricLinesOf("all") == kAllLines);
    CHECK(LyricLinesOf("") == kAllLines);
    CHECK(LyricLinesOf("0") == 0);
    CHECK(LyricLinesOf("5") == 5);
    CHECK(LyricLinesOf("6") == kAllLines);
    CHECK(LyricLinesOf("12") == kAllLines);
}
