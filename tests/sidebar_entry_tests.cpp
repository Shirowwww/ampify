#include "../src/core/sidebar_entry.hpp"
#include "test.hpp"

#include <cmath>
#include <limits>

namespace {

constexpr double kAuto = std::numeric_limits<double>::quiet_NaN();

// Where the layout puts an icon asked at `x`: on the nearest device pixel.
double Laid(double x, double scale) {
    return std::round(x * scale) / scale;
}

// The shift measured again on an entry laid out with `shift`, as MeasureIconShift does after each open.
double Remeasured(double appleAxis, double entryAxis, double shift, double scale) {
    return ampify::EntryIconShift(appleAxis, Laid(entryAxis + shift, scale) - shift, scale);
}

}  // namespace

TEST_CASE(SidebarEntry_FoldedWidthIsTheColumnLessTheMargins) {
    CHECK(ampify::FoldedEntryWidth(48, 4, 4) == 40);
}

TEST_CASE(SidebarEntry_FoldedWidthCountsEachSideMargin) {
    CHECK(ampify::FoldedEntryWidth(48, 1, 7) == 40);
    CHECK(ampify::FoldedEntryWidth(48, 0, 0) == 48);
}

TEST_CASE(SidebarEntry_FoldedWidthNeverGoesNegative) {
    CHECK(ampify::FoldedEntryWidth(48, 30, 30) == 0);
    CHECK(ampify::FoldedEntryWidth(0, 4, 4) == 0);
}

TEST_CASE(SidebarEntry_LayoutIsKeptWhenNothingDiffers) {
    CHECK(!ampify::EntryLayoutChanged({false, true, 40}, {false, true, 40}));
    CHECK(!ampify::EntryLayoutChanged({true, false, kAuto}, {true, false, kAuto}));
}

TEST_CASE(SidebarEntry_LayoutChangesWhenThePaneOpensOrFolds) {
    CHECK(ampify::EntryLayoutChanged({false, false, 40}, {true, false, kAuto}));
    CHECK(ampify::EntryLayoutChanged({true, false, kAuto}, {false, false, 40}));
}

TEST_CASE(SidebarEntry_LayoutChangesWhenTheEntryIsOnOrOff) {
    CHECK(ampify::EntryLayoutChanged({false, false, 40}, {false, true, 40}));
}

TEST_CASE(SidebarEntry_LayoutPutsBackAWidthAThemeWrote) {
    CHECK(ampify::EntryLayoutChanged({false, false, 60}, {false, false, 40}));
    CHECK(ampify::EntryLayoutChanged({true, false, 120}, {true, false, kAuto}));
}

TEST_CASE(SidebarEntry_IconShiftMovesTheIconOntoApplesAxis) {
    CHECK(ampify::EntryIconShift(25.5, 24, 2) == 1.5);
    CHECK(ampify::EntryIconShift(22, 24, 1) == -2);
}

TEST_CASE(SidebarEntry_IconShiftSnapsToDevicePixels) {
    CHECK(ampify::EntryIconShift(25.6, 24, 2) == 1.5);
    CHECK(ampify::EntryIconShift(25.8, 24, 2) == 2);
    CHECK(ampify::EntryIconShift(25.4, 24, 1) == 1);
    CHECK(std::abs(ampify::EntryIconShift(25, 24, 1.25) - 0.8) < 1e-9);
}

TEST_CASE(SidebarEntry_IconShiftIgnoresLessThanHalfADevicePixel) {
    CHECK(ampify::EntryIconShift(24.2, 24, 2) == 0);
    CHECK(ampify::EntryIconShift(23.8, 24, 2) == 0);
    CHECK(ampify::EntryIconShift(24.4, 24, 1) == 0);
}

TEST_CASE(SidebarEntry_IconShiftIgnoresAnAxisFarOffTheColumn) {
    CHECK(ampify::EntryIconShift(24 + ampify::kMaxEntryIconShift + 1, 24, 1) == 0);
    CHECK(ampify::EntryIconShift(-100, 24, 1) == 0);
}

TEST_CASE(SidebarEntry_IconShiftHoldsStillWhenMeasuredAgainOnItsOwnLayout) {
    // Apple's axis half a pixel off the entry's: a half-pixel shift was rounded by the layout, measured back as the
    // other half, and the icon went to and fro a pixel on every second open.
    for (double scale : {1.0, 1.25, 1.5, 2.0}) {
        double shift = Remeasured(24.5, 24, 0, scale);
        const double settled = shift;
        for (int open = 0; open < 4; open++) {
            shift = Remeasured(24.5, 24, shift, scale);
            CHECK(shift == settled);
        }
    }
}

TEST_CASE(SidebarEntry_IconShiftTakesAMissingScaleAsOne) {
    CHECK(ampify::EntryIconShift(25.4, 24, 0) == 1);
}

TEST_CASE(SidebarEntry_CutSharesTheMissingRoomAmongTheEntries) {
    CHECK(ampify::EntryCutEach(0, 20, 0, 5, 8) == 4);
    CHECK(ampify::EntryCutEach(0, 21, 0, 5, 8) == 5);
}

TEST_CASE(SidebarEntry_CutHoldsWhenTheListJustFits) {
    CHECK(ampify::EntryCutEach(20, 0, 0, 5, 8) == 4);
    CHECK(ampify::EntryCutEach(20, 0, 3, 5, 8) == 4);
}

TEST_CASE(SidebarEntry_CutIsGivenBackWhenTheListHasRoomToSpare) {
    CHECK(ampify::EntryCutEach(20, 0, 20, 5, 8) == 0);
    CHECK(ampify::EntryCutEach(20, 0, 12, 5, 8) == 2);
}

TEST_CASE(SidebarEntry_CutNeverPassesItsLimitOrNeedsNoEntry) {
    CHECK(ampify::EntryCutEach(0, 400, 0, 5, 8) == 8);
    CHECK(ampify::EntryCutEach(0, 20, 0, 0, 8) == 0);
}

TEST_CASE(SidebarEntry_LayoutChangesWhenTheIconShiftChanges) {
    CHECK(ampify::EntryLayoutChanged({false, false, 40, 0}, {false, false, 40, 1.5}));
    CHECK(!ampify::EntryLayoutChanged({true, false, kAuto, 1.5}, {true, false, kAuto, 1.5}));
}
