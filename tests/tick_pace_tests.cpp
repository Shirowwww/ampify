#include <vector>

#include "../src/core/theme/tick_pace.hpp"
#include "test.hpp"

using ampify::theme::TickPace;

TEST_CASE(TickPace_ANewRequestAlwaysApplies) {
    TickPace pace;
    for (int i = 0; i < 10; i++) {
        pace.Ticked(0);
        CHECK(pace.Due(true));
    }
}

TEST_CASE(TickPace_QuietPassesComeLessOftenUpToEveryFourthTick) {
    TickPace pace;
    std::vector<bool> due;
    for (int i = 0; i < 16; i++) {
        bool now = pace.Due(false);
        due.push_back(now);
        if (now) pace.Ticked(0);
    }
    // 1 quiet pass: one tick skipped; 2: two; then three at most.
    std::vector<bool> expected = {true,  false, true,  false, false, true,  false, false,
                                  false, true,  false, false, false, true,  false, false};
    CHECK(due == expected);
}

TEST_CASE(TickPace_APassThatWritesBringsTheUsualPaceBack) {
    TickPace pace;
    for (int i = 0; i < 12; i++) {
        if (pace.Due(false)) pace.Ticked(0);
    }
    while (!pace.Due(false)) {
    }
    pace.Ticked(2);  // Apple put a value back: the theme wrote it again
    CHECK(pace.Due(false));
}

TEST_CASE(TickPace_TheTickAfterAPassAfterAChangeIsSkipped) {
    TickPace pace;
    CHECK(pace.Due(false));
    pace.Ticked(5);
    pace.PassedAfterChange(3);
    CHECK(!pace.Due(false));  // that pass went over everything already
    CHECK(pace.Due(false));   // and something moved: the usual pace
}

TEST_CASE(TickPace_ForcedAppliesAtOnceAfterAQuietSpell) {
    TickPace pace;
    for (int i = 0; i < 12; i++) {
        if (pace.Due(false)) pace.Ticked(0);
    }
    pace.Force();
    CHECK(pace.Due(false));
}
