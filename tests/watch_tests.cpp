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
    CHECK(first.inject == std::vector<uint32_t>({200}));  // the agent waits until it has settled

    ampify::WatchActions settled = watch.Step(Poll(9.0, {100, 200}, {300}));
    CHECK(settled.inject == std::vector<uint32_t>({300}));

    CHECK(watch.Step(Poll(10.0, {100, 200}, {300})).inject.empty());
}

// Regression (05/11/2026): the kernel loaded into AMPLibraryAgent.exe while it opened the library left the library
// closed for good, and nothing played. The agent gets the kernel once it has run for agentSettleAfter seconds.
TEST_CASE(PackageWatch_InjectsTheAgentOnlyOnceItHasSettled) {
    ampify::WatchConfig config;
    ampify::PackageWatch watch(config);

    CHECK(watch.Step(Poll(0.0, {100})).inject == std::vector<uint32_t>({100}));
    CHECK(watch.Step(Poll(2.0, {100}, {300})).inject.empty());
    CHECK(watch.Step(Poll(9.9, {100}, {300})).inject.empty());
    CHECK(watch.Step(Poll(10.0, {100}, {300})).inject == std::vector<uint32_t>({300}));
    // A new agent, after the first one restarted, waits its own time.
    CHECK(watch.Step(Poll(11.0, {100}, {400})).inject.empty());
    CHECK(watch.Step(Poll(19.0, {100}, {400})).inject == std::vector<uint32_t>({400}));
}

// An agent that ends before it settled is forgotten: a later agent given the same pid waits its own time.
TEST_CASE(PackageWatch_ForgetsAnAgentThatEndedBeforeItSettled) {
    ampify::WatchConfig config;
    ampify::PackageWatch watch(config);

    CHECK(watch.Step(Poll(0.0, {100}, {300})).inject == std::vector<uint32_t>({100}));
    CHECK(watch.Step(Poll(1.0, {100}, {})).inject.empty());
    CHECK(watch.Step(Poll(9.0, {100}, {300})).inject.empty());
    CHECK(watch.Step(Poll(17.0, {100}, {300})).inject == std::vector<uint32_t>({300}));
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

// Regression: the launcher read the process list twice and walked every window four times a second for as long as
// Apple Music stayed open, about 85 ms of processor a second. Once the app has settled it looks less often.
TEST_CASE(PackageWatch_SettlesThirtySecondsAfterTheAppAppears) {
    ampify::WatchConfig config;  // the kernel reports health itself
    ampify::PackageWatch watch(config);

    CHECK(!watch.Step(Poll(0.0, {})).settled);
    CHECK(!watch.Step(Poll(2.0, {100})).settled);
    CHECK(!watch.Step(Poll(31.9, {100})).settled);
    CHECK(watch.Step(Poll(32.0, {100})).settled);
}

TEST_CASE(PackageWatch_SettlesOnlyOnceHealthIsReported) {
    ampify::PackageWatch watch(HealthOnly());

    CHECK(!watch.Step(Poll(0.0, {100}, {}, false)).settled);
    CHECK(!watch.Step(Poll(40.0, {100}, {}, false)).settled);  // hung: health still awaited

    ampify::WatchActions healthy = watch.Step(Poll(41.0, {100}));
    CHECK(healthy.markHealthy);
    CHECK(healthy.settled);
}

TEST_CASE(PackageWatch_AsksForTheWindowsOnlyWhileHealthIsAwaited) {
    ampify::PackageWatch reporting(HealthOnly());
    CHECK(reporting.NeedsResponsiveness());
    reporting.Step(Poll(0.0, {100}));
    CHECK(reporting.Step(Poll(20.0, {100})).markHealthy);
    CHECK(!reporting.NeedsResponsiveness());

    ampify::WatchConfig config;  // the kernel reports health itself
    CHECK(!ampify::PackageWatch(config).NeedsResponsiveness());
}

TEST_CASE(PackageWatch_GivesUpWhenTheAppNeverAppears) {
    ampify::WatchConfig config;
    ampify::PackageWatch watch(config);

    CHECK(!watch.Step(Poll(14.9, {})).stop);

    ampify::WatchActions gaveUp = watch.Step(Poll(15.0, {}));
    CHECK(gaveUp.stop);
    CHECK(!gaveUp.stopReason.empty());
}
