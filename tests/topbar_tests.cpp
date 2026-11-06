#include "test.hpp"

#include "../src/core/topbar.hpp"

using namespace ampify;

TEST_CASE(Topbar_ShownOnlyWhenAskedWideEnoughAndNotAtTheTop) {
    CHECK(!TopbarShown(false, false, 1600));  // off by default
    CHECK(TopbarShown(true, false, 1600));
    CHECK(!TopbarShown(true, true, 1600));    // Apple's transport bar sits at the top: no strip over it
    CHECK(!TopbarShown(true, false, kTopbarMinWidth - 1));
    CHECK(TopbarShown(true, false, kTopbarMinWidth));
}

TEST_CASE(Topbar_TransportCountsAtTheTopOnlyWhenVisibleAndInTheStrip) {
    CHECK(TransportAtTop(true, 0));                       // Apple's own layout, no theme
    CHECK(TransportAtTop(true, kTopbarHeight - 1));       // a theme that insets the bar a little
    CHECK(!TransportAtTop(true, kTopbarHeight));          // starts under the strip
    CHECK(!TransportAtTop(true, 900));                    // a bottom bar
    CHECK(!TransportAtTop(false, 0));                     // collapsed by a theme
}

TEST_CASE(Topbar_SearchFieldIsCentredWithinBounds) {
    CHECK(TopbarSearchWidth(760) == 388);            // room between Back and the caption buttons
    CHECK(TopbarSearchWidth(2000) == kTopbarSearchMaxWidth);
    CHECK(TopbarSearchWidth(300) == kTopbarSearchMinWidth);
    // The field clears the caption buttons on its right, and the same margin is kept on its left.
    for (double width : {760.0, 900.0, 1600.0}) {
        double left = (width - TopbarSearchWidth(width)) / 2;
        CHECK(left >= kTopbarCaptionWidth);
    }
}

TEST_CASE(Topbar_BackSitsLeftOfTheCentredFieldAndBothClearTheCaptionButtons) {
    for (double width : {kTopbarMinWidth, 900.0, 1324.0, 2560.0}) {
        TopbarLayout layout = LayOutTopbar(width, kTopbarCaptionHeight);
        CHECK(layout.search.x + layout.search.width / 2 == width / 2);
        CHECK(layout.back.x + layout.back.width + kTopbarGap == layout.search.x);
        CHECK(layout.back.x >= 0);
        CHECK(layout.search.x + layout.search.width <= width - kTopbarCaptionWidth);
        CHECK(layout.back.height == kTopbarControlHeight && layout.search.height == kTopbarControlHeight);
    }
}

TEST_CASE(Topbar_ControlsCentreOnTheCaptionButtonsNotOnTheStrip) {
    for (double caption : {kTopbarCaptionHeight, 40.0, 48.0}) {
        TopbarLayout layout = LayOutTopbar(1140, caption);
        CHECK(layout.back.y + layout.back.height / 2 == caption / 2);
        CHECK(layout.search.y + layout.search.height / 2 == caption / 2);
    }
}

TEST_CASE(Topbar_ControlsNeverRiseAboveTheWindowUnderAShortCaption) {
    TopbarLayout layout = LayOutTopbar(1140, 24);
    CHECK(layout.back.y == 0 && layout.search.y == 0);
}

TEST_CASE(Topbar_CaptionHeightIsReadInDevicePixelsOrFallsBackToStandard) {
    CHECK(TopbarCaptionHeight(40, 1.25) == 32);                   // Windows' standard caption at 125%
    CHECK(TopbarCaptionHeight(60, 1.25) == 48);                   // a tall caption
    CHECK(TopbarCaptionHeight(0, 1.25) == kTopbarCaptionHeight);  // not extended into the title bar yet
    CHECK(TopbarCaptionHeight(40, 0) == kTopbarCaptionHeight);    // no scale known
}

TEST_CASE(Topbar_PassthroughRoundsOutwardInDevicePixels) {
    TopbarRect rect{10, 0, 100, 48};
    DeviceRect at100 = ToDeviceRect(rect, 1.0);
    CHECK(at100.x == 10 && at100.y == 0 && at100.width == 100 && at100.height == 48);

    DeviceRect at150 = ToDeviceRect(rect, 1.5);
    CHECK(at150.x == 15 && at150.width == 150 && at150.height == 72);

    DeviceRect at125 = ToDeviceRect(rect, 1.25);
    CHECK(at125.x == 12 && at125.width == 126 && at125.height == 60);  // 12.5 .. 137.5 rounds to 12 .. 138
}

TEST_CASE(Topbar_MinimumWidthIsWhatTheStripNeeds) {
    CHECK(kTopbarMinWidth == 612);                       // Back and both caption sides, plus the narrowest field
    CHECK(TopbarSearchWidth(kTopbarMinWidth) == kTopbarSearchMinWidth);
}

TEST_CASE(Topbar_PassthroughRoundsOutwardBelowOneAndAcrossNegativeOrigins) {
    DeviceRect half = ToDeviceRect(TopbarRect{-3, 0, 10, 10}, 0.5);
    CHECK(half.x == -2 && half.width == 6);              // -1.5 .. 3.5 widens to -2 .. 4
    DeviceRect shrunk = ToDeviceRect(TopbarRect{1, 0, 3, 3}, 0.5);
    CHECK(shrunk.x == 0 && shrunk.width == 2);           // 0.5 .. 2 widens to 0 .. 2
    DeviceRect negative = ToDeviceRect(TopbarRect{-10, 0, 20, 48}, 1.0);
    CHECK(negative.x == -10 && negative.width == 20);
}

TEST_CASE(Topbar_OwnRegionsAreSwappedWithoutTouchingApplesOwn) {
    std::vector<DeviceRect> apple{{0, 0, 300, 40}, {1000, 0, 200, 64}};
    std::vector<DeviceRect> before{{500, 4, 40, 32}};
    std::vector<DeviceRect> now{{510, 4, 40, 32}, {558, 4, 400, 32}};
    std::vector<DeviceRect> current = apple;
    current.push_back(before[0]);

    std::vector<DeviceRect> merged = MergeRegions(current, before, now);

    CHECK(merged.size() == 4);
    CHECK(merged[0] == apple[0] && merged[1] == apple[1]);
    CHECK(merged[2] == now[0] && merged[3] == now[1]);
    CHECK(MergeRegions(merged, now, {}) == apple);       // turned off: only Apple's are left
}
