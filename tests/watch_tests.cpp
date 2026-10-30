#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../core/package_watch.hpp"
#include "test.hpp"

namespace {

// One poll of the package. `responsive` says whether an app window answers.
amk::WatchSnapshot Poll(double seconds, std::vector<uint32_t> app, std::vector<uint32_t> agents = {},
                        bool responsive = true) {
    amk::WatchSnapshot snapshot;
    snapshot.seconds = seconds;
    snapshot.appProcesses = std::move(app);
    snapshot.agentProcesses = std::move(agents);
    snapshot.appResponsive = responsive;
    return snapshot;
}

amk::WatchConfig HealthOnly() {
    amk::WatchConfig config;
    config.inject = false;
    config.reportHealth = true;
    return config;
}

}  // namespace

// Regression: the first process handed over to a second one and exited. The old launcher followed only the
// first pid, saw it gone, stopped, and never reported health, so the safe-mode count stayed at 3.
TEST_CASE(PackageWatch_FollowsAHandOverToAnotherProcess) {
    amk::PackageWatch watch(HealthOnly());

    CHECK(!watch.Step(Poll(0.0, {100})).stop);
    CHECK(!watch.Step(Poll(0.5, {})).stop);  // the first process has exited, the next one is not up yet
    CHECK(!watch.Step(Poll(1.0, {200})).stop);
    CHECK(!watch.Step(Poll(10.0, {200})).stop);

    amk::WatchActions healthy = watch.Step(Poll(20.0, {200}));
    CHECK(healthy.markHealthy);
    CHECK(!healthy.stop);
}

TEST_CASE(PackageWatch_ReportsHealthOnceAfterTwentySeconds) {
    amk::PackageWatch watch(HealthOnly());

    CHECK(!watch.Step(Poll(0.0, {100})).markHealthy);
    CHECK(!watch.Step(Poll(19.9, {100})).markHealthy);
    CHECK(watch.Step(Poll(20.0, {100})).markHealthy);
    CHECK(!watch.Step(Poll(21.0, {100})).markHealthy);  // only once per launch
}

TEST_CASE(PackageWatch_NoHealthWhileTheAppIsHung) {
    amk::PackageWatch watch(HealthOnly());

    CHECK(!watch.Step(Poll(0.0, {100}, {}, false)).markHealthy);
    CHECK(!watch.Step(Poll(25.0, {100}, {}, false)).markHealthy);  // hung at 25 s: not healthy
    CHECK(watch.Step(Poll(26.0, {100}, {}, true)).markHealthy);
}

TEST_CASE(PackageWatch_NeverReportsHealthWhenTheKernelDoes) {
    amk::WatchConfig config;
    config.inject = true;
    config.reportHealth = false;
    amk::PackageWatch watch(config);

    CHECK(!watch.Step(Poll(0.0, {100})).markHealthy);
    CHECK(!watch.Step(Poll(30.0, {100})).markHealthy);
}

TEST_CASE(PackageWatch_InjectsEachProcessOnceAndSkipsHandledOnes) {
    amk::WatchConfig config;  // inject on, no health report
    amk::PackageWatch watch(config);
    watch.MarkHandled(100);  // the launcher injected this one itself

    CHECK(watch.Step(Poll(0.0, {100})).inject.empty());

    amk::WatchActions first = watch.Step(Poll(1.0, {100, 200}, {300}));
    CHECK(first.inject == std::vector<uint32_t>({200, 300}));

    CHECK(watch.Step(Poll(2.0, {100, 200}, {300})).inject.empty());
}

TEST_CASE(PackageWatch_InjectsNothingWhenInjectionIsOff) {
    amk::PackageWatch watch(HealthOnly());  // safe mode: no injection

    CHECK(watch.Step(Poll(0.0, {100}, {300})).inject.empty());
    CHECK(watch.Step(Poll(1.0, {100}, {300, 400})).inject.empty());
}

TEST_CASE(PackageWatch_StopsOnlyAfterTheAppHasBeenGoneLongEnough) {
    amk::PackageWatch watch(HealthOnly());

    CHECK(!watch.Step(Poll(0.0, {100})).stop);
    CHECK(!watch.Step(Poll(3.0, {})).stop);

    amk::WatchActions closed = watch.Step(Poll(5.0, {}));
    CHECK(closed.stop);
    CHECK(closed.stopReason == "Apple Music fermé");
}

TEST_CASE(PackageWatch_GivesUpWhenTheAppNeverAppears) {
    amk::WatchConfig config;
    amk::PackageWatch watch(config);

    CHECK(!watch.Step(Poll(14.9, {})).stop);

    amk::WatchActions gaveUp = watch.Step(Poll(15.0, {}));
    CHECK(gaveUp.stop);
    CHECK(!gaveUp.stopReason.empty());
}
