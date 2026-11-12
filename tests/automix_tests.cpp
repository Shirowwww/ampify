#include <cmath>
#include <string>
#include <vector>

#include "../src/core/automix/mix_plan.hpp"
#include "../src/core/automix/mix_session.hpp"
#include "test.hpp"

using namespace ampify::automix;

namespace {

// Apple's player and the second player, simulated on a clock the test moves. A seek of the second player lands
// `latency` behind its target, a few milliseconds more or less each time, as measured in Apple Music on 12/11/2026.
class FakeDecks : public MixDecks {
public:
    double t = 0;         // the test's clock
    double aOrigin = 80;  // A's time at t = 0
    std::string next = "B";
    bool armed = false;
    double armEnd = 0, armAt = 0;
    int disarms = 0;
    int rewinds = 0;        // disarms that sent B back to its start
    double freezeAt = -1;   // the second player's clock stops at this test time
    bool switched = false;
    double switchClock = 0;
    double bLate = 0.05;  // how far behind its point B comes in: Apple's player, as measured
    bool ghostOn = false;
    double ghostStartClock = 0, ghostAnchorClock = 0, ghostAnchorTime = 0;
    std::vector<double> latencies = {0.096, 0.102, 0.105, 0.1001, 0.0996, 0.1012, 0.0985, 0.1003, 0.1005, 0.0999};
    size_t seekCount = 0;
    std::vector<std::pair<double, double>> gains;  // (Apple's player, second player) of every SetGains
    int stops = 0;

    double ATime() const { return aOrigin + t; }

    std::optional<double> MainTime() override {
        if (armed && !switched && ATime() >= armEnd) {
            switched = true;
            switchClock = armEnd - aOrigin;
        }
        if (switched) return armAt + (t - switchClock) - bLate;
        return ATime();
    }
    std::string NextItem() override { return switched ? std::string() : next; }
    bool MainOnNext() override {
        MainTime();
        return switched;
    }
    bool StartGhost() override {
        ghostOn = true;
        ghostStartClock = t + 0.3;  // the first start buffers
        ghostAnchorClock = ghostStartClock;
        ghostAnchorTime = 0;
        return true;
    }
    std::optional<double> GhostTime() override {
        if (!ghostOn) return std::nullopt;
        double clock = freezeAt >= 0 && t > freezeAt ? freezeAt : t;
        if (clock < ghostStartClock) return 0.0;
        return ghostAnchorTime + (clock - ghostAnchorClock);
    }
    void SeekGhost(double to) override {
        double late = latencies[seekCount++ % latencies.size()];
        ghostAnchorClock = t;
        ghostAnchorTime = to - late;
    }
    void StopGhost() override {
        ghostOn = false;
        stops++;
    }
    void SetGains(double main, double ghost) override { gains.emplace_back(main, ghost); }
    bool ArmSwitch(double aEnd, double bAt) override {
        armed = true;
        armEnd = aEnd;
        armAt = bAt;
        return true;
    }
    void Disarm(bool rewindNext) override {
        armed = false;
        disarms++;
        if (!rewindNext) return;
        rewinds++;
        if (switched) {  // B already plays: from its start again
            armAt = 0;
            bLate = 0;
            switchClock = t;
        }
    }
    // The queue moved on: a new A at 80 s, nothing armed, no second player.
    void Reset() {
        aOrigin = 80 - t;
        switched = armed = ghostOn = false;
        gains.clear();
    }
};

MixPlan Plan() {
    MixPlan plan;
    plan.next = "B";
    plan.bStart = 30;
    plan.keys = {{100, 1, 0}, {108, 0, 1}};
    return plan;
}

// Runs the session in 10 ms steps until it rests or the test's clock reaches `until`.
void Run(MixSession& session, FakeDecks& decks, double until) {
    while (decks.t < until && session.Active()) {
        session.Tick(decks.t);
        decks.t += 0.01;
    }
}

bool NoJump(const std::vector<std::pair<double, double>>& gains, double step) {
    for (size_t i = 1; i < gains.size(); i++) {
        if (std::abs(gains[i].first - gains[i - 1].first) > step) return false;
        if (std::abs(gains[i].second - gains[i - 1].second) > step) return false;
    }
    return true;
}

}  // namespace

TEST_CASE(AutoMix_GainsKeepTheirPowerAcrossACrossfade) {
    std::vector<GainKey> keys = {{10, 1, 0}, {20, 0, 1}};
    Gains before = GainsAt(keys, 5);
    CHECK(before.a == 1 && before.b == 0);
    Gains middle = GainsAt(keys, 15);
    CHECK_NEAR(middle.a, std::sqrt(0.5), 1e-9);
    CHECK_NEAR(middle.b, std::sqrt(0.5), 1e-9);
    for (double at = 10; at <= 20; at += 0.5) {
        Gains g = GainsAt(keys, at);
        CHECK_NEAR(g.a * g.a + g.b * g.b, 1.0, 1e-9);
    }
    Gains after = GainsAt(keys, 25);
    CHECK(after.a == 0 && after.b == 1);
}

TEST_CASE(AutoMix_GainsStartAndStopWithoutACorner) {
    std::vector<GainKey> keys = {{0, 1, 0}, {10, 0, 1}};
    // smoothstep: the curve barely moves near both ends
    CHECK(GainsAt(keys, 0.1).a > 0.999);
    CHECK(GainsAt(keys, 9.9).b > 0.999);
}

TEST_CASE(AutoMix_CheckRefusesPlansThatCannotBePlayed) {
    CHECK(Check(Plan(), 200, 200).empty());
    MixPlan plan = Plan();
    plan.keys.back() = {108, 0.2, 1};
    CHECK(!Check(plan, 200, 200).empty());  // A must be gone at the end
    plan = Plan();
    plan.keys.front() = {100, 1, 0.3};
    CHECK(!Check(plan, 200, 200).empty());  // B comes in from silence
    plan = Plan();
    plan.keys = {{108, 1, 0}, {100, 0, 1}};
    CHECK(!Check(plan, 200, 200).empty());  // keys out of order
    plan = Plan();
    plan.keys.back().at = 170;
    CHECK(!Check(plan, 200, 200).empty());  // longer than a minute
    CHECK(!Check(Plan(), 105, 200).empty());  // A ends during the mix
    CHECK(!Check(Plan(), 200, 35).empty());   // B ends during the mix
    plan = Plan();
    plan.next.clear();
    CHECK(!Check(plan, 200, 200).empty());
    plan = Plan();
    plan.bStart = -1;
    CHECK(!Check(plan, 200, 200).empty());
}

TEST_CASE(AutoMix_ThePlanReadsFromTheScriptsObject) {
    std::optional<ampify::Json> json =
        ampify::Json::Parse(R"({"next":"B","bStart":30,"keys":[[100,1,0],[104,0.8,0.6],[108,0,1]]})");
    REQUIRE(json.has_value());
    std::string error;
    std::optional<MixPlan> plan = PlanFromJson(*json, &error);
    REQUIRE(plan.has_value());
    CHECK(plan->next == "B");
    CHECK(plan->keys.size() == 3);
    CHECK(plan->keys[1].b == 0.6);
    CHECK(!PlanFromJson(*ampify::Json::Parse(R"({"next":"B","bStart":30,"keys":[[1,2]]})"), &error));
    CHECK(!error.empty());
}

TEST_CASE(AutoMix_ASessionCarriesTheEndOfTheSongWhileApplesPlayerMovesOn) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Done);
    CHECK(status.reason.empty());
    CHECK(status.seeks >= 1);
    CHECK(std::abs(status.alignErrorMs) <= 1.0);
    CHECK(std::abs(status.switchLagMs) <= 1.0);
    // Apple's player was told to leave A at the first key, for B at the plan's point and the time it takes to start
    CHECK(decks.armEnd == 100);
    CHECK_NEAR(decks.armAt, 30 + MixSession::kSwitchDelay, 1e-9);
    CHECK(decks.switched);
    CHECK(decks.stops == 1);
    CHECK(decks.disarms == 1);  // A, done, is let go; B plays on where the plan put it
    CHECK(decks.rewinds == 0);
    REQUIRE(!decks.gains.empty());
    CHECK(decks.gains.back() == std::make_pair(1.0, 0.0));
    CHECK(NoJump(decks.gains, 0.15));
    // halfway through the mix both songs play at the equal-power point
    bool sawMiddle = false;
    for (auto [main, ghost] : decks.gains) {
        if (std::abs(main - ghost) < 0.02 && main > 0.6 && main < 0.8) sawMiddle = true;
    }
    CHECK(sawMiddle);
}

TEST_CASE(AutoMix_TheNextSongsLateStartIsLearnedAndMadeUpFor) {
    FakeDecks decks;
    decks.bLate = 0.08;  // later than the 50 ms the session starts from
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    CHECK(session.GetStatus().phase == Phase::Done);
    CHECK_NEAR(decks.armAt, 30 + MixSession::kSwitchDelay, 1e-9);
    CHECK_NEAR(session.GetStatus().switchLagMs, 30, 1.5);  // 80 ms late, 50 made up for
    // The next mix: B is sought the 80 ms learned further and comes in on its point.
    decks.Reset();
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, decks.t + 60);
    CHECK(session.GetStatus().phase == Phase::Done);
    CHECK_NEAR(decks.armAt, 30.08, 0.0015);
    CHECK(std::abs(session.GetStatus().switchLagMs) <= 1.5);
}

TEST_CASE(AutoMix_TheSecondPlayerStartsOnlyShortlyBeforeTheMix) {
    FakeDecks decks;
    decks.aOrigin = 20;  // 80 s before the mix
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    CHECK(!decks.ghostOn);
    CHECK(session.GetStatus().phase == Phase::Waiting);
    CHECK(session.TickInterval() >= 0.2);  // nothing to do yet: the thread may sleep
}

TEST_CASE(AutoMix_ACancelledMixFadesTheOldSongOut) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 24);  // A at 104: mixing, Apple's player on B
    REQUIRE(session.GetStatus().phase == Phase::Mixing);
    session.Cancel("cancelled");
    Run(session, decks, 30);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.reason == "cancelled");
    CHECK(decks.rewinds == 0);  // B goes on, A's end is just faded out
    CHECK(decks.disarms == 1);  // and A, done, is let go
    CHECK(!decks.ghostOn);
    CHECK(decks.gains.back() == std::make_pair(1.0, 0.0));
    CHECK(NoJump(decks.gains, 0.15));
}

TEST_CASE(AutoMix_ACancelBeforeTheSwitchPutsEverythingBack) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 19.85);  // A at 99.85: the copies of A have crossed, Apple's player still on A
    REQUIRE(session.GetStatus().phase == Phase::TakingOver);
    session.Cancel("paused", true);
    Run(session, decks, 25);
    CHECK(session.GetStatus().phase == Phase::Failed);
    CHECK(decks.rewinds == 1);  // A plays to its own end, B from its start
    CHECK(!decks.switched);
    CHECK(!decks.ghostOn);
    CHECK(decks.gains.back() == std::make_pair(1.0, 0.0));
}

TEST_CASE(AutoMix_ASkipBeforeTheMixStartsTheNextSongFromItsBeginning) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 17);  // A at 97: ready, B sought to the plan's point
    REQUIRE(session.GetStatus().phase == Phase::Ready);
    decks.switched = true;  // the user skips: Apple's player goes to B, where the plan put it
    decks.switchClock = decks.t;
    session.Cancel("skipped", true);
    Run(session, decks, 20);
    CHECK(session.GetStatus().phase == Phase::Failed);
    CHECK(decks.rewinds == 1);
    std::optional<double> b = decks.MainTime();
    REQUIRE(b.has_value());
    CHECK(*b < 3.5);  // from its start, not from the plan's 30 s
    CHECK(!decks.ghostOn);
    CHECK(decks.gains.back() == std::make_pair(1.0, 0.0));
}

TEST_CASE(AutoMix_ASecondPlayerThatFreezesDuringTheMixGivesTheSoundBack) {
    FakeDecks decks;
    decks.freezeAt = 23;  // A at 103, in the middle of the mix
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 40);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.reason == "the second player stopped");
    CHECK(!decks.ghostOn);
    CHECK(decks.gains.back() == std::make_pair(1.0, 0.0));
}

TEST_CASE(AutoMix_AChangedQueueDropsThePlanBeforeAnythingIsHeard) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    decks.next = "C";
    Run(session, decks, 5);
    CHECK(session.GetStatus().phase == Phase::Failed);
    CHECK(session.GetStatus().reason == "the queue changed");
    CHECK(!decks.ghostOn);
}

TEST_CASE(AutoMix_ASecondPlayerThatNeverLandsInStepGivesUp) {
    FakeDecks decks;
    decks.latencies = {0.06, 0.14};  // far apart: the learned delay never fits
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.seeks == MixSession::kMaxSeeks);
    CHECK(!decks.ghostOn);
    CHECK(!decks.armed);
    CHECK(decks.gains.back() == std::make_pair(1.0, 0.0));  // nothing of the second player was heard
}

TEST_CASE(AutoMix_ASecondPlayerThatLandsALittleWideIsTakenAfterAFewTries) {
    FakeDecks decks;
    decks.latencies = {0.0984, 0.1016};  // never within 1 ms of the learned delay, always within 2
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Done);
    CHECK(status.seeks > MixSession::kExactSeeks);
    CHECK(std::abs(status.alignErrorMs) <= 2.0);
}

TEST_CASE(AutoMix_ANewPlanReplacesOneWhoseMixHasNotBegun) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 15);  // second player started, aligning or ready
    CHECK(decks.ghostOn);
    MixPlan later = Plan();
    later.keys = {{120, 1, 0}, {128, 0, 1}};
    CHECK(session.Start(later, 200, 200).empty());
    CHECK(!decks.ghostOn);
    CHECK(session.GetStatus().phase == Phase::Waiting);
}
