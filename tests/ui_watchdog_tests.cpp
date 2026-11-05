// The interface freeze watch (src/core/ui_watchdog.*): a stall is logged once the heartbeat resumes, with what the
// interface was doing; a summary comes each minute. Time is passed in, so the tests run without a clock.

#include <string>
#include <vector>

#include "../src/core/ui_watchdog.hpp"
#include "test.hpp"

namespace {

using ampify::ActivityScope;
using ampify::BeatGate;
using ampify::CurrentActivity;
using ampify::UiStallMonitor;

}  // namespace

TEST_CASE(UiWatchdog_SteadyHeartbeatLogsNothing) {
    UiStallMonitor monitor(0);
    std::vector<std::string> lines;
    for (int64_t now = 100; now <= 5000; now += 100) {
        if (now % 200 == 0) monitor.Beat(now);
        for (const std::string& line : monitor.Check(now, "")) lines.push_back(line);
    }
    CHECK(lines.empty());
}

TEST_CASE(UiWatchdog_AGapBelowTheThresholdIsNotAStall) {
    UiStallMonitor monitor(0);
    monitor.Beat(100);
    CHECK(monitor.Check(100 + ampify::kStallMs - 1, "theme pass").empty());
    monitor.Beat(100 + ampify::kStallMs - 1);
    CHECK(monitor.Check(100 + ampify::kStallMs, "theme pass").empty());
}

TEST_CASE(UiWatchdog_AStallIsLoggedWithItsLengthWhenTheHeartbeatResumes) {
    UiStallMonitor monitor(0);
    monitor.Beat(200);
    CHECK(monitor.Check(300, "").empty());
    CHECK(monitor.Check(700, "theme pass").empty());  // silent 500 ms: the stall is noticed, not yet logged
    monitor.Beat(1100);
    std::vector<std::string> lines = monitor.Check(1100, "theme pass");
    CHECK(lines.size() == 1);
    if (lines.size() == 1) CHECK(lines[0] == "UI thread stalled 900 ms, while: theme pass");
}

TEST_CASE(UiWatchdog_AStallWithoutAnyAmpifyWorkSaysSo) {
    UiStallMonitor monitor(0);
    monitor.Beat(0);
    monitor.Check(600, "");
    monitor.Beat(700);
    std::vector<std::string> lines = monitor.Check(700, "");
    CHECK(lines.size() == 1);
    if (lines.size() == 1) CHECK(lines[0] == "UI thread stalled 700 ms, while: no Ampify work");
}

TEST_CASE(UiWatchdog_TheActivityIsTheOneSeenWhenTheStallIsNoticed) {
    UiStallMonitor monitor(0);
    monitor.Beat(0);
    monitor.Check(400, "theme pass");     // noticed here
    monitor.Check(900, "player command"); // later work does not rename the stall
    monitor.Beat(950);
    std::vector<std::string> lines = monitor.Check(950, "");
    CHECK(lines.size() == 1);
    if (lines.size() == 1) CHECK(lines[0].find("while: theme pass") != std::string::npos);
}

TEST_CASE(UiWatchdog_EachMinuteSummarisesBeatsWorstGapAndStalls) {
    UiStallMonitor monitor(0);
    std::vector<std::string> summaries;
    // Beats every 200 ms, except 1000, 1200 and 1400 ms: one stall, the worst gap of the minute.
    for (int64_t now = 0; now <= ampify::kSummaryMs + 100; now += 100) {
        if (now % 200 == 0 && (now < 1000 || now > 1400)) monitor.Beat(now);
        for (const std::string& line : monitor.Check(now, "")) {
            if (line.rfind("UI watchdog", 0) == 0) summaries.push_back(line);
        }
    }
    CHECK(summaries.size() == 1);
    if (summaries.size() == 1) {
        // The last beat before the silence is at 800 ms, the next at 1600 ms: the worst gap is 800 ms.
        CHECK(summaries[0] == "UI watchdog, last minute: 298 beats, worst gap 800 ms, 1 stall(s)");
    }
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
    ampify::BeatGate gate;
    CHECK(gate.TryTake());
    CHECK(!gate.TryTake());  // the first beat still waits in the interface queue
    gate.Release();
    CHECK(gate.TryTake());
}
