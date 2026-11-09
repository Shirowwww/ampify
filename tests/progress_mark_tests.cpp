#include "../src/core/progress_mark.hpp"
#include "test.hpp"

using ampify::LayOutProgressMark;
using ampify::ProgressMark;

namespace {

constexpr double kWidth = 400;
constexpr double kTick = 2;

}  // namespace

TEST_CASE(ProgressMark_NothingShowsWithoutAStart) {
    ProgressMark mark = LayOutProgressMark(kWidth, -1, -1, kTick);
    CHECK(!mark.start && !mark.end);
}

TEST_CASE(ProgressMark_AStartAloneShowsOneTickCentredOnIt) {
    ProgressMark mark = LayOutProgressMark(kWidth, 0.25, -1, kTick);
    CHECK(mark.start && !mark.end);
    CHECK_NEAR(mark.startLeft, 99, 1e-9);
    CHECK_NEAR(mark.shadeWidth, 0, 1e-9);
}

TEST_CASE(ProgressMark_TheShadeRunsBetweenTheCentresOfTheTwoTicks) {
    ProgressMark mark = LayOutProgressMark(kWidth, 0.25, 0.5, kTick);
    CHECK(mark.start && mark.end);
    CHECK_NEAR(mark.startLeft, 99, 1e-9);
    CHECK_NEAR(mark.endLeft, 199, 1e-9);
    CHECK_NEAR(mark.shadeLeft, 100, 1e-9);
    CHECK_NEAR(mark.shadeWidth, 100, 1e-9);
}

TEST_CASE(ProgressMark_TicksStayWholeAtTheEndsOfTheLine) {
    ProgressMark mark = LayOutProgressMark(kWidth, 0, 1, kTick);
    CHECK_NEAR(mark.startLeft, 0, 1e-9);
    CHECK_NEAR(mark.endLeft, kWidth - kTick, 1e-9);
}

TEST_CASE(ProgressMark_EndsGivenBackwardsMarkTheSameStretch) {
    ProgressMark forwards = LayOutProgressMark(kWidth, 0.2, 0.6, kTick);
    ProgressMark backwards = LayOutProgressMark(kWidth, 0.6, 0.2, kTick);
    CHECK_NEAR(backwards.startLeft, forwards.startLeft, 1e-9);
    CHECK_NEAR(backwards.shadeWidth, forwards.shadeWidth, 1e-9);
}

TEST_CASE(ProgressMark_ALineNotYetLaidOutShowsNothing) {
    ProgressMark mark = LayOutProgressMark(0, 0.5, 0.7, kTick);
    CHECK(!mark.start && !mark.end);
}
