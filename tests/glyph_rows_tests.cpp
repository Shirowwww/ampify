#include <cmath>
#include <limits>
#include <vector>

#include "../src/core/theme/glyph_rows.hpp"
#include "test.hpp"

namespace th = ampify::theme;

namespace {

bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

TEST_CASE(GlyphRows_CentreMovesEachWrappedRowToTheMiddleOfTheHost) {
    // two words on the first row (0..120), one on the second (0..60), in a host 200 wide
    std::vector<th::GlyphBox> boxes{{0, 0, 50, 30}, {60, 0, 60, 30}, {0, 36, 60, 30}};

    std::vector<double> shifts = th::RowShifts(boxes, 200, th::RowAlign::Center);

    CHECK(shifts.size() == 3);
    CHECK(Near(shifts[0], 40));
    CHECK(Near(shifts[1], 40));
    CHECK(Near(shifts[2], 70));
}

TEST_CASE(GlyphRows_EndPutsEachRowAgainstTheRightEdge) {
    std::vector<th::GlyphBox> boxes{{0, 0, 120, 30}, {10, 36, 50, 30}};

    std::vector<double> shifts = th::RowShifts(boxes, 200, th::RowAlign::End);

    CHECK(Near(shifts[0], 80));
    CHECK(Near(shifts[1], 140));
}

TEST_CASE(GlyphRows_StartLeavesApplesOwnPlacement) {
    std::vector<th::GlyphBox> boxes{{20, 0, 120, 30}, {140, 36, 50, 30}};

    std::vector<double> shifts = th::RowShifts(boxes, 200, th::RowAlign::Start);

    CHECK(Near(shifts[0], 0));
    CHECK(Near(shifts[1], 0));
}

TEST_CASE(GlyphRows_AWordLiftedAFewPixelsWhileSungStaysOnItsRow) {
    std::vector<th::GlyphBox> boxes{{0, 0, 50, 30}, {60, -3, 40, 30}, {0, 36, 100, 30}};

    std::vector<double> shifts = th::RowShifts(boxes, 200, th::RowAlign::Center);

    CHECK(Near(shifts[0], 50));
    CHECK(Near(shifts[1], 50));
    CHECK(Near(shifts[2], 50));
}

TEST_CASE(GlyphRows_RowsGroupByPositionNotByOrder) {
    // Apple may list the visuals in any order
    std::vector<th::GlyphBox> boxes{{0, 36, 60, 30}, {0, 0, 50, 30}, {60, 0, 60, 30}};

    std::vector<double> shifts = th::RowShifts(boxes, 200, th::RowAlign::Center);

    CHECK(Near(shifts[0], 70));
    CHECK(Near(shifts[1], 40));
    CHECK(Near(shifts[2], 40));
}

TEST_CASE(GlyphRows_ARowWiderThanTheHostIsLeftWhereItIs) {
    std::vector<th::GlyphBox> boxes{{0, 0, 260, 30}};

    std::vector<double> shifts = th::RowShifts(boxes, 200, th::RowAlign::Center);

    CHECK(Near(shifts[0], 0));
}

TEST_CASE(GlyphRows_NoHostWidthMovesNothing) {
    std::vector<th::GlyphBox> boxes{{0, 0, 50, 30}};

    CHECK(Near(th::RowShifts(boxes, 0, th::RowAlign::Center)[0], 0));
    CHECK(Near(th::RowShifts(boxes, kNaN, th::RowAlign::Center)[0], 0));
    CHECK(th::RowShifts({}, 200, th::RowAlign::Center).empty());
}

TEST_CASE(GlyphRows_TextAlignKeywordsMapToRowAlignments) {
    CHECK(th::RowAlignOf("Center") == th::RowAlign::Center);
    CHECK(th::RowAlignOf("Right") == th::RowAlign::End);
    CHECK(th::RowAlignOf("Left") == th::RowAlign::Start);
    CHECK(th::RowAlignOf("Justify") == th::RowAlign::Start);
    CHECK(th::RowAlignKeyword(th::RowAlign::Center) == "Center");
    CHECK(th::RowAlignKeyword(th::RowAlign::End) == "Right");
    CHECK(th::RowAlignKeyword(th::RowAlign::Start) == "Left");
}

TEST_CASE(LiftedMaxSize_GrowsTheCapToTheSizeAskedForAndKeepsItsShape) {
    th::BoxSize lifted = th::LiftedMaxSize({270, 270}, 360, 360);
    CHECK(Near(lifted.width, 360));
    CHECK(Near(lifted.height, 360));

    // Only a width: the height follows in the cap's proportions
    th::BoxSize wide = th::LiftedMaxSize({270, 135}, 540, kNaN);
    CHECK(Near(wide.width, 540));
    CHECK(Near(wide.height, 270));
}

TEST_CASE(LiftedMaxSize_ASmallerSizeLeavesTheCapAlone) {
    th::BoxSize kept = th::LiftedMaxSize({270, 270}, 120, 120);
    CHECK(Near(kept.width, 270));
    CHECK(Near(kept.height, 270));
}

TEST_CASE(LiftedMaxSize_ACapOnOneSideGrowsAndTheOtherSideStaysOpen) {
    // Apple caps only the width of an editorial playlist's cover (Best Songs of 2026): kept at 270, it asked a
    // picture 270 wide and 360 high, a portrait drawn small in the square
    th::BoxSize width = th::LiftedMaxSize({270, 0}, 360, 360);
    th::BoxSize height = th::LiftedMaxSize({0, 270}, 360, 360);

    CHECK(Near(width.width, 360));
    CHECK(Near(width.height, 0));
    CHECK(Near(height.width, 0));
    CHECK(Near(height.height, 360));
}

TEST_CASE(LiftedMaxSize_NoCapStaysNoCap) {
    th::BoxSize none = th::LiftedMaxSize({0, 0}, 360, 360);
    CHECK(Near(none.width, 0));
    CHECK(Near(none.height, 0));
}

TEST_CASE(LiftedBound_GrowsAnAppleMaximumBelowTheCapToTheCap) {
    // Apple keeps an artwork's MaxHeight at 270 while its cap was lifted to 360
    double appleMax = 270;

    double bound = th::LiftedBound(appleMax, 360);

    CHECK(Near(bound, 360));
}

TEST_CASE(LiftedBound_LeavesNoMaximumOrALargerOneAlone) {
    constexpr double kNone = std::numeric_limits<double>::infinity();
    CHECK(std::isinf(th::LiftedBound(kNone, 360)));
    CHECK(Near(th::LiftedBound(400, 360), 400));
    CHECK(Near(th::LiftedBound(270, 120), 270));
}

TEST_CASE(ShiftsInPlace_HoldsWhenEveryWordSitsWhereItsRowIsAligned) {
    // the shifts written last pass, read back from the visuals in single precision
    std::vector<double> wanted{40.0, 40.0, 70.0};
    std::vector<double> current{40.0000019, 39.9999981, 70.0};

    bool inPlace = th::ShiftsInPlace(current, wanted);

    CHECK(inPlace);
}

TEST_CASE(ShiftsInPlace_FailsForWordsAppleDrewAgainAtTheLeft) {
    // A recycled line: new word visuals with no shift, in a host whose size did not change
    CHECK(!th::ShiftsInPlace({0.0, 0.0}, {35.0, 35.0}));
    CHECK(!th::ShiftsInPlace({35.0, 0.0}, {35.0, 35.0}));
}

TEST_CASE(ShiftsInPlace_FailsWhenTheNumberOfWordsChanged) {
    CHECK(!th::ShiftsInPlace({35.0, 35.0}, {35.0, 35.0, 12.0}));
    CHECK(th::ShiftsInPlace({}, {}));
}
