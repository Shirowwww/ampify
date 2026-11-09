// The timing of the installer window's motion (src/setup/setup_motion.hpp): its curve and its retargeting value.

#include <cmath>

#include "../src/setup/setup_motion.hpp"
#include "test.hpp"

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 0.002f; }

}  // namespace

TEST_CASE(SetupMotion_TheCurveStartsAtZeroEndsAtOneAndRisesFastFirst) {
    const ampify::setup::Curve& curve = ampify::setup::kEaseOut;
    CHECK(curve.At(-1) == 0.0f);
    CHECK(curve.At(0) == 0.0f);
    CHECK(curve.At(1) == 1.0f);
    CHECK(curve.At(2) == 1.0f);
    CHECK(curve.At(0.25f) > 0.6f);  // an ease-out covers most of the way in the first quarter
    CHECK(curve.At(0.5f) > curve.At(0.25f));
    CHECK(curve.At(0.75f) > curve.At(0.5f));
}

TEST_CASE(SetupMotion_ALinearCurveGivesBackItsProgress) {
    ampify::setup::Curve linear{0.0f, 0.0f, 1.0f, 1.0f};
    for (float t : {0.1f, 0.3f, 0.5f, 0.9f}) CHECK(Near(linear.At(t), t));
}

TEST_CASE(SetupMotion_ATweenEndsOnItsTargetAndStopsMoving) {
    ampify::setup::Tween tween(0);
    tween.To(1, 1000, 200);
    CHECK(tween.Moving(1000));
    CHECK(tween.At(1000) == 0.0f);
    CHECK(tween.At(1100) > 0.5f);
    CHECK(!tween.Moving(1200));
    CHECK(tween.EndsAt() == 1200u);
    CHECK(tween.At(5000) == 1.0f);
}

TEST_CASE(SetupMotion_ATweenSentBackMidwayLeavesFromWhereItIs) {
    ampify::setup::Tween tween(0);
    tween.To(1, 0, 200);
    float midway = tween.At(50);
    tween.To(0, 50, 200);
    CHECK(Near(tween.At(50), midway));  // no jump when the pointer leaves before the hover finished
    CHECK(tween.At(250) == 0.0f);
    tween.Jump(1);
    CHECK(!tween.Moving(250));
    CHECK(tween.At(250) == 1.0f);
}
