#include <cmath>
#include <string>
#include <vector>

#include "../src/core/automix/mix_plan.hpp"
#include "../src/core/automix/mix_session.hpp"
#include "test.hpp"

using namespace ampify::automix;

namespace {

// Apple's player, the second player and the deck, simulated on a clock the test moves. A seek of the second player
// lands `latency` behind its target, as measured in Apple Music on 12/11/2026; the deck finds Apple's sound in the
// second player's as soon as it is asked, unless the test says the song is silent.
class FakeDecks : public MixDecks {
public:
    double t = 0;         // the test's clock
    double aOrigin = 80;  // A's time at t = 0
    std::string next = "B";
    MixPlan plan;
    bool armed = false;
    double armEnd = 0, armAt = 0;
    int disarms = 0;
    int rewinds = 0;        // disarms that sent B back to its start
    double freezeAt = -1;   // the deck starves at this test time
    bool switched = false;
    double switchClock = 0;
    double bLate = MixSession::kSwitchDelay;  // how far behind its point B comes in: Apple's player, as measured
    bool ghostOn = false;
    bool captured = true;  // the second player's stream is seen
    bool silent = false;   // nothing to recognise: Align finds nothing
    bool aligned = false;
    double copyError = -60;
    double foundAgainLead = -1;  // from 0 on: the next search finds a copy in step, this far ahead
    double ghostStartClock = 0, ghostAnchorClock = 0, ghostAnchorTime = 0;
    std::vector<double> latencies = {0.096, 0.102, 0.105, 0.1001, 0.0996};
    size_t seekCount = 0;
    int aligns = 0;
    std::vector<DeckGains> gains;  // every SetGains
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
    bool StartGhost(const MixPlan& forPlan) override {
        plan = forPlan;
        ghostOn = true;
        aligned = false;
        ghostStartClock = t + 0.3;  // the first start buffers
        ghostAnchorClock = ghostStartClock;
        ghostAnchorTime = 0;
        return true;
    }
    std::optional<double> GhostTime() override {
        if (!ghostOn) return std::nullopt;
        if (t < ghostStartClock) return 0.0;
        return ghostAnchorTime + (t - ghostAnchorClock);
    }
    bool GhostCaptured() override { return ghostOn && captured && t >= ghostStartClock; }
    void SeekGhost(double to) override {
        double late = latencies[seekCount++ % latencies.size()];
        ghostAnchorClock = t;
        ghostAnchorTime = to - late;
        aligned = false;
    }
    void StopGhost() override {
        ghostOn = aligned = false;
        stops++;
    }
    std::optional<DeckStep> Align() override {
        aligns++;
        if (foundAgainLead >= 0) {
            aligned = true;
            copyError = -60;
            return DeckStep{foundAgainLead, 1};
        }
        std::optional<double> ghost = GhostTime();
        if (!ghost || silent || *ghost <= ATime()) return std::nullopt;
        // Sent ahead, the second player has not yet played what Apple's player plays now.
        if (t - ghostAnchorClock < *ghost - ATime()) return std::nullopt;
        aligned = true;
        return DeckStep{*ghost - ATime(), 1};
    }
    // The deck's copy of A: Apple's own time until A's speed starts to move, then the plan's ramp, then its rate.
    std::optional<double> HeardTime() override {
        if (!aligned) return std::nullopt;
        const double clock = freezeAt >= 0 && t > freezeAt ? freezeAt : t;
        const double from = CarryFrom(plan);
        const double since = aOrigin + clock - from;
        if (since <= 0 || !Stretches(plan)) return aOrigin + clock;
        const double ramp = RampSeconds(plan);
        if (since < ramp) return from + since + (plan.rate - 1) * since * since / (2 * ramp);
        return plan.keys.front().at + (since - ramp) * plan.rate;
    }
    double CopyErrorDb() override { return aligned ? copyError : NAN; }
    std::optional<double> SwitchGap() override {  // what the streams show of it
        return switched ? std::optional<double>(bLate - MixSession::kUnseenDelay) : std::nullopt;
    }
    void SetGains(const DeckGains& set) override { gains.push_back(set); }
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
        switched = armed = ghostOn = aligned = false;
        gains.clear();
    }
    bool ApplesAlone() const {
        if (gains.empty()) return true;
        const DeckGains& last = gains.back();
        return last.main == 1 && last.tail == 0 && last.mainLow == 1 && last.tailLow == 1;
    }
    bool NothingHeard() const {
        for (const DeckGains& set : gains) {
            if (set.tail != 0 || set.main != 1) return false;
        }
        return true;
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

bool NoJump(const std::vector<DeckGains>& gains, double step) {
    for (size_t i = 1; i < gains.size(); i++) {
        if (std::abs(gains[i].main - gains[i - 1].main) > step) return false;
        if (std::abs(gains[i].tail - gains[i - 1].tail) > step) return false;
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
        CHECK(g.aLow == 1 && g.bLow == 1);  // keys without lows leave both basses alone
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

TEST_CASE(AutoMix_TheLowsOfBothSongsSwapBetweenKeys) {
    std::vector<GainKey> keys = {{0, 1, 0, 1, 0}, {4, 1, 1, 1, 0}, {6, 1, 1, 0, 1}, {10, 0, 1, 0, 1}};
    CHECK(GainsAt(keys, 2).aLow == 1);
    CHECK(GainsAt(keys, 2).bLow == 0);
    Gains middle = GainsAt(keys, 5);
    CHECK_NEAR(middle.aLow, 0.5, 1e-9);
    CHECK_NEAR(middle.bLow, 0.5, 1e-9);
    CHECK(GainsAt(keys, 8).aLow == 0);
    CHECK(GainsAt(keys, 8).bLow == 1);
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
    // The second player runs ahead of what is heard: A must go on for a while after the mix, or it is through A first.
    CHECK_NEAR(RoomAfter(Plan()), 3.0, 1e-9);
    CHECK(!Check(Plan(), 110.9, 200).empty());
    CHECK(Check(Plan(), 111, 200).empty());
    CHECK(!Check(Plan(), 200, 35).empty());   // B ends during the mix
    plan = Plan();
    plan.next.clear();
    CHECK(!Check(plan, 200, 200).empty());
    plan = Plan();
    plan.bStart = -1;
    CHECK(!Check(plan, 200, 200).empty());
    plan = Plan();
    plan.keys.back().bLow = 0.5;
    CHECK(!Check(plan, 200, 200).empty());  // B must have its lows back at the end
    plan = Plan();
    plan.keys.front().aLow = 1.5;
    CHECK(!Check(plan, 200, 200).empty());
}

TEST_CASE(AutoMix_CheckKeepsTheStretchWithinWhatSoundsRight) {
    MixPlan plan = Plan();
    plan.rate = 1.06;
    plan.rateFrom = 92;
    CHECK(Check(plan, 200, 200).empty());
    plan.rate = 1.1;
    CHECK(Check(plan, 200, 200).empty());  // the bounds themselves are allowed
    plan.rate = 0.9;
    CHECK(Check(plan, 200, 200).empty());
    plan.rate = 1.2;
    CHECK(!Check(plan, 200, 200).empty());  // more than 10 %
    plan.rate = 1.06;
    plan.rateFrom = 100;
    CHECK(!Check(plan, 200, 200).empty());  // no time to reach the rate
    plan.rateFrom = 60;
    CHECK(!Check(plan, 200, 200).empty());  // a ramp of 40 s
    plan.rate = 1;  // as recorded: rateFrom says nothing
    CHECK(Check(plan, 200, 200).empty());
}

TEST_CASE(AutoMix_AStretchedPlanKnowsWhereBothSongsAre) {
    MixPlan plan = Plan();
    plan.rate = 1.05;
    plan.rateFrom = 92;
    CHECK(CarryFrom(plan) == 92);
    // 8 s of A at 1.025 on average: 7.805 s for the listener, which Apple's player, still at 1, spends on A too
    CHECK_NEAR(RampSeconds(plan), 8 / 1.025, 1e-9);
    CHECK_NEAR(SwitchTime(plan), 92 + 8 / 1.025, 1e-9);
    // 4.2 s of A at 1.05 are 4 s of B
    CHECK_NEAR(NextTimeAt(plan, 104.2), 34, 1e-9);
    // a faster A eats the second player's lead: 0.05 times the 15.4 s the deck plays it for
    CHECK(LeadFor(plan) > LeadFor(Plan()) + 0.7);
    CHECK(HoldFor(plan) > LeadFor(plan));
    plan.rate = 0.95;  // a slower A leaves more waiting in the deck instead
    CHECK(HoldFor(plan) > HoldFor(Plan()) + 0.7);
    CHECK(CarryFrom(Plan()) == 100);
    CHECK(SwitchTime(Plan()) == 100);
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
    CHECK(plan->keys[1].aLow == 1);
    CHECK(plan->rate == 1);
    CHECK(!PlanFromJson(*ampify::Json::Parse(R"({"next":"B","bStart":30,"keys":[[1,2]]})"), &error));
    CHECK(!error.empty());
    json = ampify::Json::Parse(
        R"({"next":"B","bStart":30,"rate":1.04,"rateFrom":92,"keys":[[100,1,0,1,0],[108,0,1,0,1]]})");
    REQUIRE(json.has_value());
    plan = PlanFromJson(*json, &error);
    REQUIRE(plan.has_value());
    CHECK(plan->rate == 1.04);
    CHECK(plan->rateFrom == 92);
    CHECK(plan->keys[0].bLow == 0);
    CHECK(plan->keys[1].aLow == 0);
    CHECK(!PlanFromJson(*ampify::Json::Parse(R"({"next":"B","bStart":30,"keys":[[100,1,0,1]]})"), &error));
    CHECK(!PlanFromJson(*ampify::Json::Parse(R"({"next":"B","bStart":30,"rate":"fast","keys":[]})"), &error));
}

TEST_CASE(AutoMix_ASessionCarriesTheEndOfTheSongWhileApplesPlayerMovesOn) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Done);
    CHECK(status.reason.empty());
    CHECK(status.seeks == 1);  // one seek ahead is enough: the deck finds the samples, wherever it landed
    CHECK(status.leadSeconds >= LeadFor(Plan()));
    CHECK(std::abs(status.switchLagMs) <= 1.0);
    // Apple's player was told to leave A at the first key, for B at the plan's point and the time it takes to start
    CHECK(decks.armEnd == 100);
    CHECK_NEAR(decks.armAt, 30 + MixSession::kSwitchDelay, 1e-9);
    CHECK(decks.switched);
    CHECK(decks.stops == 1);
    CHECK(decks.disarms == 1);  // A, done, is let go; B plays on where the plan put it
    CHECK(decks.rewinds == 0);
    REQUIRE(!decks.gains.empty());
    CHECK(decks.ApplesAlone());
    CHECK(NoJump(decks.gains, 0.15));
    // halfway through the mix both songs play at the equal-power point
    bool sawMiddle = false;
    for (const DeckGains& set : decks.gains) {
        if (std::abs(set.main - set.tail) < 0.02 && set.main > 0.6 && set.main < 0.8) sawMiddle = true;
    }
    CHECK(sawMiddle);
}

TEST_CASE(AutoMix_TheDeckTakesTheSongBeforeItsSpeedMoves) {
    FakeDecks decks;
    MixPlan plan = Plan();
    plan.rate = 1.05;
    plan.rateFrom = 92;
    plan.keys = {{100, 1, 0, 1, 0}, {104, 1, 1, 0.5, 0.5}, {108, 0, 1, 0, 1}};
    MixSession session(decks);
    REQUIRE(session.Start(plan, 200, 200).empty());
    Run(session, decks, 11.9);  // A at 91.9, just before its speed moves
    CHECK(session.GetStatus().phase == Phase::TakingOver);
    REQUIRE(!decks.gains.empty());
    CHECK(decks.gains.back().tail == 1);  // the deck's copy already carries A
    CHECK(decks.gains.back().main == 0);
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Done);
    // Apple's player, at normal speed, leaves A when the faster copy reaches the first key
    CHECK_NEAR(decks.armEnd, SwitchTime(plan), 1e-9);
    CHECK(decks.armEnd < 100);
    CHECK(std::abs(status.switchLagMs) <= 1.0);
    CHECK(status.leadSeconds >= LeadFor(plan));
    bool swapped = false;
    for (const DeckGains& set : decks.gains) {
        if (set.tailLow < 0.2 && set.mainLow > 0.8 && set.tail > 0) swapped = true;
    }
    CHECK(swapped);  // A's bass left before A did
    CHECK(decks.ApplesAlone());
}

TEST_CASE(AutoMix_TheNextSongsLateStartIsLearnedAndMadeUpFor) {
    FakeDecks decks;
    decks.bLate = 0.095;  // later than the 65 ms the session starts from
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    CHECK(session.GetStatus().phase == Phase::Done);
    CHECK_NEAR(decks.armAt, 30 + MixSession::kSwitchDelay, 1e-9);
    CHECK_NEAR(session.GetStatus().switchLagMs, 30, 1.5);  // 95 ms late, 65 made up for
    // The next mix: B is sought the 95 ms learned further and comes in on its point.
    decks.Reset();
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, decks.t + 60);
    CHECK(session.GetStatus().phase == Phase::Done);
    CHECK_NEAR(decks.armAt, 30.095, 0.0015);
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
    CHECK(decks.ApplesAlone());
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
    CHECK(decks.ApplesAlone());
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
    CHECK(decks.ApplesAlone());
}

TEST_CASE(AutoMix_ADeckThatStarvesDuringTheMixGivesTheSoundBack) {
    FakeDecks decks;
    decks.freezeAt = 23;  // A at 103, in the middle of the mix
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 40);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.reason == "the second player stopped");
    CHECK(!decks.ghostOn);
    CHECK(decks.ApplesAlone());
}

TEST_CASE(AutoMix_ADeckThatLosesItsPlaceAfterTheSwitchLetsTheNextSongPlayOn) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 24);  // A at 104: mixing, Apple's player on B
    REQUIRE(session.GetStatus().phase == Phase::Mixing);
    decks.aligned = false;  // the second player's stream broke: the deck no longer tells A's time
    Run(session, decks, 30);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.reason == "the second player stopped");
    CHECK(decks.rewinds == 0);  // B switched on the plan: it plays on, not from its start again
    CHECK(decks.ApplesAlone());
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

TEST_CASE(AutoMix_ASongTooQuietToRecogniseIsNotMixed) {
    FakeDecks decks;
    decks.silent = true;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.reason == "the second player could not be put in step");
    CHECK(decks.aligns > 3);  // it kept looking while there was time
    CHECK(!decks.ghostOn);
    CHECK(!decks.armed);
    CHECK(decks.NothingHeard());
}

TEST_CASE(AutoMix_ASecondPlayerNobodyCouldCaptureNeverSounds) {
    FakeDecks decks;
    decks.captured = false;  // its sound would go straight to the speakers
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.reason == "the second player did not play");
    CHECK(status.seeks == 0);
    CHECK(!decks.ghostOn);
    CHECK(decks.NothingHeard());
}

TEST_CASE(AutoMix_ASeekThatLandsShortIsDoneAgain) {
    FakeDecks decks;
    decks.latencies = {0.9, 0.9};  // the first seek lands almost a second behind: not enough lead
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Done);
    CHECK(status.seeks == 2);
    CHECK(status.leadSeconds >= LeadFor(Plan()));
}

TEST_CASE(AutoMix_ACopyThatDriftedBeforeTheMixIsNeverHeard) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 17);
    REQUIRE(session.GetStatus().phase == Phase::Ready);
    decks.copyError = 0;  // the two copies no longer say the same thing...
    decks.silent = true;  // ...and the deck cannot find Apple's sound again
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.reason == "the second player drifted");
    CHECK(decks.rewinds == 1);
    CHECK(decks.NothingHeard());
}

TEST_CASE(AutoMix_ASecondPlayerTooFarAheadOfASlowedSongIsSentAgain) {
    FakeDecks decks;
    MixPlan plan = Plan();
    plan.rate = 0.92;  // the deck falls 1.3 s further behind the second player during the mix
    plan.rateFrom = 92;
    // What the deck holds covers the lead it starts with and what a slower A adds to it, not more.
    decks.foundAgainLead = LeadFor(plan) + 1.2;
    MixSession session(decks);
    REQUIRE(session.Start(plan, 200, 200).empty());
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.reason == "the second player could not be put in step");
    CHECK(status.seeks > 1);
    CHECK(decks.NothingHeard());
    CHECK(RoomAfter(plan) > RoomAfter(Plan()) + 1.3);
}

TEST_CASE(AutoMix_ACopyFoundAgainTooCloseIsNeverHeard) {
    FakeDecks decks;
    MixSession session(decks);
    REQUIRE(session.Start(Plan(), 200, 200).empty());
    Run(session, decks, 17);
    REQUIRE(session.GetStatus().phase == Phase::Ready);
    decks.copyError = 0;                            // the second player dropped sound...
    decks.foundAgainLead = LeadFor(Plan()) - 0.1;   // ...and is found again with less lead than the mix needs
    Run(session, decks, 60);
    Status status = session.GetStatus();
    CHECK(status.phase == Phase::Failed);
    CHECK(status.reason == "the second player drifted");
    CHECK(decks.NothingHeard());
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
