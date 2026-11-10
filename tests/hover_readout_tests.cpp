#include "../src/core/hover_readout.hpp"
#include "test.hpp"

using ampify::ClockText;
using ampify::HoverReadout;
using ampify::LayOutHoverReadout;

namespace {

constexpr double kWidth = 400;
constexpr double kMarker = 2;
constexpr double kLabel = 40;

}  // namespace

TEST_CASE(HoverReadout_TheMarkerAndTheLabelAreCentredOnThePointer) {
    HoverReadout readout = LayOutHoverReadout(kWidth, 100, kMarker, kLabel);
    CHECK(readout.shown);
    CHECK_NEAR(readout.part, 0.25, 1e-9);
    CHECK_NEAR(readout.markerLeft, 99, 1e-9);
    CHECK_NEAR(readout.labelLeft, 80, 1e-9);
}

TEST_CASE(HoverReadout_TheLabelStopsAtTheEndsOfTheLine) {
    HoverReadout start = LayOutHoverReadout(kWidth, 5, kMarker, kLabel);
    HoverReadout end = LayOutHoverReadout(kWidth, 398, kMarker, kLabel);
    CHECK_NEAR(start.labelLeft, 0, 1e-9);
    CHECK_NEAR(end.labelLeft, kWidth - kLabel, 1e-9);
    CHECK_NEAR(end.markerLeft, 397, 1e-9);
}

TEST_CASE(HoverReadout_APointerPastTheLineReadsItsEnd) {
    HoverReadout before = LayOutHoverReadout(kWidth, -30, kMarker, kLabel);
    HoverReadout after = LayOutHoverReadout(kWidth, 450, kMarker, kLabel);
    CHECK_NEAR(before.part, 0, 1e-9);
    CHECK_NEAR(before.markerLeft, 0, 1e-9);
    CHECK_NEAR(after.part, 1, 1e-9);
    CHECK_NEAR(after.markerLeft, kWidth - kMarker, 1e-9);
}

TEST_CASE(HoverReadout_ALabelWiderThanTheLineIsCentredOnIt) {
    HoverReadout readout = LayOutHoverReadout(30, 10, kMarker, kLabel);
    CHECK_NEAR(readout.labelLeft, -5, 1e-9);
}

TEST_CASE(HoverReadout_ALineNotYetLaidOutShowsNothing) {
    CHECK(!LayOutHoverReadout(0, 10, kMarker, kLabel).shown);
    CHECK(!LayOutHoverReadout(kWidth, std::nan(""), kMarker, kLabel).shown);
}

TEST_CASE(HoverReadout_ClockTextWritesMinutesAndSeconds) {
    CHECK(ClockText(0) == "0:00");
    CHECK(ClockText(187999) == "3:07");  // cut, not rounded up
    CHECK(ClockText(600000) == "10:00");
}

TEST_CASE(HoverReadout_ClockTextAddsHoursFromAnHourOn) {
    CHECK(ClockText(3599999) == "59:59");
    CHECK(ClockText(3729000) == "1:02:09");
}

TEST_CASE(HoverReadout_ClockTextReadsNothingBadAsZero) {
    CHECK(ClockText(-5000) == "0:00");
    CHECK(ClockText(std::nan("")) == "0:00");
    CHECK(ClockText(1e300) == "100000:00:00");  // an absurd length stays a time, never garbage
}
