// The left-over library agent (src/core/orphan_agent.*): it is ended only when Apple Music is closed, and only after
// it had its time to leave by itself. The decision is pure; the launcher reads the processes and acts on it.

#include <cstdint>
#include <vector>

#include "../src/core/orphan_agent.hpp"
#include "test.hpp"

using ampify::DecideOrphanAgent;
using ampify::OrphanAction;

TEST_CASE(OrphanAgent_NothingHappensUnlessTheSettingIsOn) {
    CHECK(DecideOrphanAgent(false, false, true, 0) == OrphanAction::None);
    CHECK(DecideOrphanAgent(false, false, true, ampify::kOrphanGraceMs * 2) == OrphanAction::None);
}

TEST_CASE(OrphanAgent_AnAgentNextToAnOpenAppIsNotAnOrphan) {
    CHECK(DecideOrphanAgent(true, true, true, 0) == OrphanAction::None);
    CHECK(DecideOrphanAgent(true, true, true, ampify::kOrphanGraceMs * 2) == OrphanAction::None);
}

TEST_CASE(OrphanAgent_NoAgentMeansNothingToDo) {
    CHECK(DecideOrphanAgent(true, false, false, 0) == OrphanAction::None);
    CHECK(DecideOrphanAgent(true, false, false, ampify::kOrphanGraceMs * 2) == OrphanAction::None);
}

TEST_CASE(OrphanAgent_AnAloneAgentGetsItsGraceBeforeItIsEnded) {
    CHECK(DecideOrphanAgent(true, false, true, 0) == OrphanAction::WaitForExit);
    CHECK(DecideOrphanAgent(true, false, true, ampify::kOrphanGraceMs - 1) == OrphanAction::WaitForExit);
    CHECK(DecideOrphanAgent(true, false, true, ampify::kOrphanGraceMs) == OrphanAction::End);
    CHECK(DecideOrphanAgent(true, false, true, ampify::kOrphanGraceMs * 3) == OrphanAction::End);
}

TEST_CASE(OrphanAgent_EveryAgentIsTriedWhenNoneWasRefused) {
    ampify::OrphanRefusals refusals;

    std::vector<uint32_t> untried = refusals.Untried({120, 340});

    CHECK(untried == std::vector<uint32_t>({120, 340}));
}

TEST_CASE(OrphanAgent_ARefusedAgentIsNotTriedAgain) {
    ampify::OrphanRefusals refusals;
    refusals.Refused(120);

    std::vector<uint32_t> untried = refusals.Untried({120, 340});

    CHECK(untried == std::vector<uint32_t>({340}));
    CHECK(refusals.Untried({120}).empty());
}

TEST_CASE(OrphanAgent_ARefusedIdIsForgottenOnceItsProcessIsGone) {
    ampify::OrphanRefusals refusals;
    refusals.Refused(120);
    refusals.Untried({});  // the refused agent left: Windows may reuse its id

    std::vector<uint32_t> untried = refusals.Untried({120});

    CHECK(untried == std::vector<uint32_t>({120}));
}

TEST_CASE(OrphanAgent_GraceIsTenSeconds) {
    CHECK(ampify::kOrphanGraceMs == 10000);
}
