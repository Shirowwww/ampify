#include "../src/core/sidebar_entry.hpp"
#include "test.hpp"

#include <limits>

namespace {

constexpr double kAuto = std::numeric_limits<double>::quiet_NaN();

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
