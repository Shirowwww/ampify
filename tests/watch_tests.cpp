#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/package_watch.hpp"
#include "test.hpp"

namespace {

// One poll of the package. `responsive` says whether an app window answers.
ampify::WatchSnapshot Poll(double seconds, std::vector<uint32_t> app, std::vector<uint32_t> agents = {},
                        bool responsive = true) {
    ampify::WatchSnapshot snapshot;
    snapshot.seconds = seconds;
    snapshot.appProcesses = std::move(app);
    snapshot.agentProcesses = std::move(agents);
    snapshot.appResponsive = responsive;
    return snapshot;
}

ampify::WatchConfig HealthOnly() {
    ampify::WatchConfig config;
    config.inject = false;
    config.reportHealth = true;
    return config;
}

}  // namespace

// Regression: the first process handed over to a second one and exited. The old launcher followed only the
// first pid, saw it gone, stopped, and never reported health, so the safe-mode count stayed at 3.
TEST_CASE(PackageWatch_FollowsAHandOverToAnotherProcess) {
    ampify::PackageWatch watch(HealthOnly());

    CHECK(!watch.Step(Poll(0.0, {100})).stop);
    CHECK(!watch.Step(Poll(0.5, {})).stop);  // the first process has exited, the next one is not up yet
    CHECK(!watch.Step(Poll(1.0, {200})).stop);
    CHECK(!watch.Step(Poll(10.0, {200})).stop);

    ampify::WatchActions healthy = watch.Step(Poll(20.0, {200}));
    CHECK(healthy.markHealthy);
    CHECK(!healthy.stop);
}

TEST_CASE(PackageWatch_ReportsHealthOnceAfterTwentySeconds) {
    ampify::PackageWatch watch(HealthOnly());

    CHECK(!watch.Step(Poll(0.0, {100})).markHealthy);
    CHECK(!watch.Step(Poll(19.9, {100})).markHealthy);
    CHECK(watch.Step(Poll(20.0, {100})).markHealthy);
    CHECK(!watch.Step(Poll(21.0, {100})).markHealthy);  // only once per launch
}

TEST_CASE(PackageWatch_NoHealthWhileTheAppIsHung) {
    ampify::PackageWatch watch(HealthOnly());

    CHECK(!watch.Step(Poll(0.0, {100}, {}, false)).markHealthy);
    CHECK(!watch.Step(Poll(25.0, {100}, {}, false)).markHealthy);  // hung at 25 s: not healthy
    CHECK(watch.Step(Poll(26.0, {100}, {}, true)).markHealthy);
}

TEST_CASE(PackageWatch_NeverReportsHealthWhenTheKernelDoes) {
    ampify::WatchConfig config;
    config.inject = true;
    config.reportHealth = false;
    ampify::PackageWatch watch(config);

    CHECK(!watch.Step(Poll(0.0, {100})).markHealthy);
    CHECK(!watch.Step(Poll(30.0, {100})).markHealthy);
}

TEST_CASE(PackageWatch_InjectsEachProcessOnceAndSkipsHandledOnes) {
    ampify::WatchConfig config;  // inject on, no health report
    ampify::PackageWatch watch(config);
    watch.MarkHandled(100);  // the launcher injected this one itself

    CHECK(watch.Step(Poll(0.0, {100})).inject.empty());

    ampify::WatchActions first = watch.Step(Poll(1.0, {100, 200}, {300}));
    CHECK(first.inject == std::vector<uint32_t>({200, 300}));

    CHECK(watch.Step(Poll(2.0, {100, 200}, {300})).inject.empty());
}

TEST_CASE(PackageWatch_InjectsNothingWhenInjectionIsOff) {
    ampify::PackageWatch watch(HealthOnly());  // safe mode: no injection

    CHECK(watch.Step(Poll(0.0, {100}, {300})).inject.empty());
    CHECK(watch.Step(Poll(1.0, {100}, {300, 400})).inject.empty());
}

TEST_CASE(PackageWatch_StopsOnlyAfterTheAppHasBeenGoneLongEnough) {
    ampify::PackageWatch watch(HealthOnly());

    CHECK(!watch.Step(Poll(0.0, {100})).stop);
    CHECK(!watch.Step(Poll(3.0, {})).stop);

    ampify::WatchActions closed = watch.Step(Poll(5.0, {}));
    CHECK(closed.stop);
    CHECK(closed.stopReason == "Apple Music closed");
}

TEST_CASE(PackageWatch_GivesUpWhenTheAppNeverAppears) {
    ampify::WatchConfig config;
    ampify::PackageWatch watch(config);

    CHECK(!watch.Step(Poll(14.9, {})).stop);

    ampify::WatchActions gaveUp = watch.Step(Poll(15.0, {}));
    CHECK(gaveUp.stop);
    CHECK(!gaveUp.stopReason.empty());
}
