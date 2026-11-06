// The interface freeze watch (src/core/ui_watchdog.*): each beat is stamped when queued, and the wait until it runs is
// the block. A stall is logged once its beat runs, with what the interface was doing; a summary comes each minute. Time
// is passed in, so the tests run without a clock.

#include <string>
#include <vector>

#include "../src/core/ui_watchdog.hpp"
#include "test.hpp"

namespace {

using ampify::ActivityScope;
using ampify::BeatGate;
using ampify::CurrentActivity;
using ampify::UiStallMonitor;

// One beat queued at `postedMs` that runs at `ranMs`, with the checks of the monitor every 100 ms in between.
std::vector<std::string> BeatWithChecks(UiStallMonitor& monitor, int64_t postedMs, int64_t ranMs,
                                        const char* activity = "") {
    std::vector<std::string> lines;
    monitor.Posted(postedMs);
    for (int64_t now = postedMs + 100; now < ranMs; now += 100) {
        for (const std::string& line : monitor.Check(now, activity)) lines.push_back(line);
    }
    monitor.Ran(ranMs);
    for (const std::string& line : monitor.Check(ranMs, activity)) lines.push_back(line);
    return lines;
}

}  // namespace

TEST_CASE(UiWatchdog_BeatsThatRunAtOnceLogNothing) {
    UiStallMonitor monitor(0);
    std::vector<std::string> lines;

    for (int64_t posted = 0; posted < 5000; posted += 200) {
        for (const std::string& line : BeatWithChecks(monitor, posted, posted + 5)) lines.push_back(line);
    }

    CHECK(lines.empty());
}

TEST_CASE(UiWatchdog_TheBeatPeriodIsNotCountedAsAStall) {
    UiStallMonitor monitor(0);

    // Queued 200 ms apart, each runs within 150 ms: the old silence measure saw 350 ms between runs.
    std::vector<std::string> first = BeatWithChecks(monitor, 0, 10);
    std::vector<std::string> second = BeatWithChecks(monitor, 200, 350);

    CHECK(first.empty());
    CHECK(second.empty());
}

TEST_CASE(UiWatchdog_AWaitBelowTheThresholdIsNotAStall) {
    UiStallMonitor monitor(0);

    std::vector<std::string> lines = BeatWithChecks(monitor, 100, 100 + ampify::kStallMs - 1, "theme pass");

    CHECK(lines.empty());
}

TEST_CASE(UiWatchdog_AStallIsLoggedWithTheRealBlockWhenItsBeatRuns) {
    UiStallMonitor monitor(0);

    std::vector<std::string> lines = BeatWithChecks(monitor, 200, 1100, "theme pass");

    CHECK(lines.size() == 1);
    if (lines.size() == 1) CHECK(lines[0] == "UI thread stalled 900 ms, while: theme pass");
}

TEST_CASE(UiWatchdog_AStallWithoutAnyAmpifyWorkSaysSo) {
    UiStallMonitor monitor(0);

    std::vector<std::string> lines = BeatWithChecks(monitor, 0, 700);

    CHECK(lines.size() == 1);
    if (lines.size() == 1) CHECK(lines[0] == "UI thread stalled 700 ms, while: no Ampify work");
}

TEST_CASE(UiWatchdog_TheActivityIsTheOneSeenWhenTheStallIsNoticed) {
    UiStallMonitor monitor(0);
    monitor.Posted(0);
    monitor.Check(400, "theme pass");      // noticed here
    monitor.Check(900, "player command");  // later work does not rename the stall

    monitor.Ran(950);
    std::vector<std::string> lines = monitor.Check(950, "");

    CHECK(lines.size() == 1);
    if (lines.size() == 1) CHECK(lines[0] == "UI thread stalled 950 ms, while: theme pass");
}

TEST_CASE(UiWatchdog_ABeatThatRanBeforeAnyCheckIsStillLogged) {
    UiStallMonitor monitor(0);
    monitor.Posted(0);

    monitor.Ran(450);
    std::vector<std::string> lines = monitor.Check(460, "theme tick");

    CHECK(lines.size() == 1);
    if (lines.size() == 1) CHECK(lines[0] == "UI thread stalled 450 ms, while: theme tick");
}

TEST_CASE(UiWatchdog_EachMinuteSummarisesBeatsWorstDelayAndStalls) {
    UiStallMonitor monitor(0);
    std::vector<std::string> summaries;

    // A beat every 200 ms that runs after 5 ms, except the one queued at 1000 ms, which waits 800 ms.
    for (int64_t posted = 0; posted <= ampify::kSummaryMs; posted += 200) {
        if (posted > 1000 && posted < 1800) continue;  // the gate holds the next beats while that one waits
        int64_t ran = posted == 1000 ? 1800 : posted + 5;
        for (const std::string& line : BeatWithChecks(monitor, posted, ran)) {
            if (line.rfind("UI watchdog", 0) == 0) summaries.push_back(line);
        }
    }

    CHECK(summaries.size() == 1);
    if (summaries.size() == 1) {
        CHECK(summaries[0] == "UI watchdog, last minute: 298 beats, worst delay 800 ms, 1 stall(s)");
    }
}

TEST_CASE(UiWatchdog_ARunWithoutAQueuedBeatIsIgnored) {
    UiStallMonitor monitor(0);

    monitor.Ran(5000);

    CHECK(monitor.Check(5000, "").empty());
}

TEST_CASE(UiWatchdog_ActivityScopeNamesWorkAndRestoresTheOuterOne) {
    CHECK(std::string(CurrentActivity() ? CurrentActivity() : "") == "");
    {
        ActivityScope outer("theme pass");
        CHECK(std::string(CurrentActivity()) == "theme pass");
        {
            ActivityScope inner("player command");
            CHECK(std::string(CurrentActivity()) == "player command");
        }
        CHECK(std::string(CurrentActivity()) == "theme pass");
    }
    CHECK(std::string(CurrentActivity() ? CurrentActivity() : "") == "");
}

TEST_CASE(UiWatchdog_BeatGateKeepsOneBeatInTheQueueAtATime) {
    BeatGate gate;
    CHECK(gate.TryTake());
    CHECK(!gate.TryTake());  // the first beat still waits in the interface queue
    gate.Release();
    CHECK(gate.TryTake());
}
