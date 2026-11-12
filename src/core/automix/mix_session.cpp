#include "mix_session.hpp"

#include <algorithm>

namespace ampify::automix {

namespace {

bool Running(Phase phase) { return phase != Phase::Idle && phase != Phase::Done && phase != Phase::Failed; }

// Phases where the deck's copy may be heard: a failure there fades back instead of cutting.
bool Audible(Phase phase) { return phase == Phase::TakingOver || phase == Phase::Mixing; }

constexpr double kSeekSafety = 1.0;     // no seek of the second player closer than this to the takeover
constexpr double kAlignSafety = 0.2;    // the deck must have found Apple's sound this long before it
constexpr double kMaxLatency = 1.0;     // a seek that lands later than this tells nothing about the next one
constexpr int kLatencyMemory = 8;
constexpr double kStallSeconds = 1.5;   // a player not moving for this long: paused or stuck
constexpr double kSwitchMargin = 0.5;   // Apple's player still on A this long after the switch point: it will not
constexpr double kMaxSwitchDelay = 0.25;  // a later start of B is a stall, not the player's usual delay
constexpr double kSwitchSlack = 0.2;      // Apple's player on B this long before the switch point: the user skipped

constexpr DeckGains kApplesAlone{1, 0, 1, 1};

double Lerp(double from, double to, double t) { return from + (to - from) * t; }

}  // namespace

std::string_view PhaseName(Phase phase) {
    switch (phase) {
        case Phase::Idle: return "idle";
        case Phase::Waiting: return "waiting";
        case Phase::Preparing: return "preparing";
        case Phase::Aligning: return "aligning";
        case Phase::Ready: return "ready";
        case Phase::TakingOver: return "taking over";
        case Phase::Mixing: return "mixing";
        case Phase::Recovering: return "recovering";
        case Phase::Done: return "done";
        case Phase::Failed: return "failed";
    }
    return "idle";
}

std::string MixSession::Start(const MixPlan& plan, double aDuration, double bDuration) {
    std::string problem = Check(plan, aDuration, bDuration);
    if (!problem.empty()) return problem;
    std::lock_guard<std::mutex> guard(m_lock);
    if (Audible(m_status.phase) || m_status.phase == Phase::Recovering) return "a mix is playing";
    // A plan whose mix has not begun is replaced: nothing of its second player is heard.
    if (m_ghostOn) m_decks.StopGhost();
    if (m_armed) m_decks.Disarm(true);
    m_ghostOn = m_armed = m_lagMeasured = false;
    m_plan = plan;
    m_status = Status{};
    m_status.phase = Phase::Waiting;
    m_status.next = plan.next;
    m_cancel.reset();
    m_phaseSince = -1;
    m_lastA = m_lastGhost = m_lastHeard = NAN;
    m_lastAMoved = m_lastHeardMoved = -1;
    return {};
}

void MixSession::Cancel(const std::string& reason, bool quick) {
    std::lock_guard<std::mutex> guard(m_lock);
    if (!Running(m_status.phase) || m_status.phase == Phase::Recovering) return;
    m_cancel = reason;
    m_cancelQuick = quick;
}

bool MixSession::Active() const {
    std::lock_guard<std::mutex> guard(m_lock);
    return Running(m_status.phase);
}

double MixSession::TickInterval() const {
    std::lock_guard<std::mutex> guard(m_lock);
    switch (m_status.phase) {
        case Phase::Waiting: return 0.2;
        case Phase::Preparing:
        case Phase::Aligning:
        case Phase::Ready: return 0.02;
        default: return 0.01;
    }
}

Status MixSession::GetStatus() const {
    std::lock_guard<std::mutex> guard(m_lock);
    return m_status;
}

void MixSession::Tick(double now) {
    std::lock_guard<std::mutex> guard(m_lock);
    TickLocked(now);
}

void MixSession::Enter(Phase phase, double now) {
    m_status.phase = phase;
    m_phaseSince = now;
}

void MixSession::Gain(const DeckGains& gains) {
    m_gains = gains;
    m_decks.SetGains(gains);
}

bool MixSession::SwitchedOnPlan() {
    if (!m_decks.MainOnNext()) return false;
    // A's time: Apple's player leaves A at the first key. A deck that lost its place no longer tells it: the last
    // time it gave stands, or B, switched on the plan, would go back to its start.
    const double heard = m_decks.HeardTime().value_or(m_lastHeard);
    return heard >= m_plan.keys.front().at - kSwitchSlack;
}

void MixSession::LetGo() {
    if (m_ghostOn) m_decks.StopGhost();
    if (m_armed) m_decks.Disarm(false);  // B plays on from where the plan put it; A no longer plays
    m_ghostOn = m_armed = false;
}

void MixSession::Fail(const std::string& reason, double now, bool quick) {
    m_status.reason = reason;
    const bool switched = m_decks.MainOnNext();
    // Not switched yet, A gets its own end back. On B before the plan's point, the user skipped to it: B goes back
    // to its start instead of the plan's point. Switched on the plan, B plays on.
    if (m_armed && (!switched || !SwitchedOnPlan())) {
        m_decks.Disarm(true);
        m_armed = false;
    }
    if (Audible(m_status.phase)) {
        // The deck's copy is heard: back to Apple's player alone without a jump. Before the switch both play A, so
        // the crossing is short; after it the deck's A fades out under B.
        m_recoverFrom = m_gains;
        m_recoverSeconds = quick ? kQuickRecoverSeconds : switched ? kRecoverSeconds : kTakeOverSeconds;
        Enter(Phase::Recovering, now);
        return;
    }
    LetGo();
    Gain(kApplesAlone);
    Enter(Phase::Failed, now);
}

// The second player is sent ahead of Apple's by what the plan needs; it lands m_latency behind its target.
void MixSession::SeekNow(double aTime, double now) {
    m_wantedLead = LeadFor(m_plan) + kLeadSlack;
    m_decks.SeekGhost(aTime + m_wantedLead + m_latency);
    m_lastSeek = m_lastCheck = now;
    m_status.seeks++;
}

// Apple's player leaves a silence between A's last sample and B's first: B was sought m_armedDelay further to make
// up for it. The deck counts that silence in samples; the status keeps what was not made up for.
void MixSession::LearnSwitchDelay() {
    const std::optional<double> gap = m_decks.SwitchGap();
    if (!gap) return;
    m_lagMeasured = true;
    const double delay = *gap + kUnseenDelay;
    m_status.switchLagMs = (delay - m_armedDelay) * 1000;
    if (!(delay >= 0 && delay < kMaxSwitchDelay)) return;
    m_switchDelay = (m_switchDelay * m_switchDelaySamples + delay) / (m_switchDelaySamples + 1);
    m_switchDelaySamples = std::min(m_switchDelaySamples + 1, kLatencyMemory);
}

// A's time at which the deck's copy starts to replace Apple's.
double MixSession::TakeAt() const { return CarryFrom(m_plan) - kTakeOverLead; }

// The second player far enough ahead for the plan, and not further than what the deck keeps.
bool MixSession::LeadFits(double lead) const {
    return lead >= LeadFor(m_plan) && lead <= MostLead(m_plan) - kLeadSlack;
}

void MixSession::TickAligning(double now, double aTime) {
    const double takeAt = TakeAt();
    if (now - m_lastSeek < kSettle || now - m_lastCheck < kRetry) return;
    m_lastCheck = now;
    std::optional<DeckStep> step = m_decks.Align();
    if (!step) {
        // Silence, or a seek that has not landed yet: the search is tried again until time runs out.
        if (aTime > takeAt - kAlignSafety) return Fail("the second player could not be put in step", now);
        return;
    }
    m_status.leadSeconds = step->lead;
    if (!LeadFits(step->lead)) {
        const double landed = m_latency + (m_wantedLead - step->lead);  // how far behind its target the seek landed
        if (landed > 0 && landed < kMaxLatency) m_latency = landed;
        if (m_status.seeks >= kMaxSeeks || aTime > takeAt - kSeekSafety) {
            return Fail("the second player could not be put in step", now);
        }
        return SeekNow(aTime, now);
    }
    m_armedDelay = m_switchDelay;
    if (!m_decks.ArmSwitch(SwitchTime(m_plan), m_plan.bStart + m_armedDelay)) {
        return Fail("Apple's player refused the switch", now);
    }
    m_armed = true;
    Enter(Phase::Ready, now);
}

void MixSession::TickReady(double now, double aTime) {
    if (m_decks.NextItem() != m_plan.next) return Fail("the queue changed", now);
    const double takeAt = TakeAt();
    if (now - m_lastCheck >= kRetry) {
        m_lastCheck = now;
        m_status.copyErrorDb = m_decks.CopyErrorDb();
        // A stream that dropped a block: the deck looks for Apple's sound again while nothing of it is heard.
        if (m_status.copyErrorDb > kMaxCopyErrorDb) {
            const std::optional<DeckStep> step = m_decks.Align();
            if (step && !LeadFits(step->lead)) return Fail("the second player drifted", now);
            if (step) m_status.copyErrorDb = NAN;
        }
    }
    if (aTime < takeAt) return;
    if (m_status.copyErrorDb > kMaxCopyErrorDb) return Fail("the second player drifted", now);
    Enter(Phase::TakingOver, now);
}

void MixSession::TickLocked(double now) {
    const Phase phase = m_status.phase;
    if (!Running(phase)) return;
    if (m_phaseSince < 0) m_phaseSince = now;
    if (m_cancel) {
        std::string reason = *m_cancel;
        m_cancel.reset();
        return Fail(reason, now, m_cancelQuick);
    }
    if (phase == Phase::Recovering) {
        double p = std::clamp((now - m_phaseSince) / m_recoverSeconds, 0.0, 1.0);
        Gain({Lerp(m_recoverFrom.main, 1, p), m_recoverFrom.tail * (1 - p), Lerp(m_recoverFrom.mainLow, 1, p),
              m_recoverFrom.tailLow});
        if (p < 1) return;
        LetGo();
        Gain(kApplesAlone);
        return Enter(Phase::Failed, now);
    }
    // While it is heard, the deck carries A: starved of the second player's sound, A falls silent and its time
    // stands still.
    if (Audible(phase)) {
        std::optional<double> heard = m_decks.HeardTime();
        if (!heard) return Fail("the second player stopped", now, true);
        if (m_lastHeardMoved < 0 || *heard != m_lastHeard) {
            m_lastHeard = *heard;
            m_lastHeardMoved = now;
        } else if (now - m_lastHeardMoved > kStallSeconds) {
            return Fail("the second player stopped", now, true);
        }
    }
    const double first = m_plan.keys.front().at;
    const double takeAt = TakeAt();
    std::optional<double> a = m_decks.MainTime();  // A until the switch, then B
    if (a) {
        if (m_lastAMoved < 0 || *a != m_lastA) m_lastAMoved = now;
        m_lastA = *a;
    }
    // Apple's player paused or stalled: the hooks say so first, this is the fallback.
    if (phase != Phase::Waiting && m_lastAMoved >= 0 && now - m_lastAMoved > kStallSeconds) {
        return Fail("the song stopped moving", now, true);
    }
    switch (phase) {
        case Phase::Waiting:
            if (m_decks.NextItem() != m_plan.next) return Fail("the queue changed", now);
            if (!a) return;
            // The deck finds Apple's sound only once Apple's player reaches what the second player, sent ahead,
            // played first: the lead is waited for, on top of the second player's start.
            if (*a > takeAt - kSeekSafety - kSettle - LeadFor(m_plan) - kLeadSlack) {
                return Fail("too late for this mix", now);
            }
            if (*a < takeAt - kPrepareLead - LeadFor(m_plan)) return;
            if (!m_decks.StartGhost(m_plan)) return Fail("the second player did not start", now);
            m_ghostOn = true;
            m_lastGhost = NAN;
            return Enter(Phase::Preparing, now);
        case Phase::Preparing: {
            // The second player's first start takes far longer than a seek, and it must be seen to play into its
            // own stream before anything of it may sound.
            std::optional<double> ghost = m_decks.GhostTime();
            if (ghost && !std::isnan(m_lastGhost) && *ghost > m_lastGhost && a && m_decks.GhostCaptured()) {
                SeekNow(*a, now);
                return Enter(Phase::Aligning, now);
            }
            m_lastGhost = ghost ? *ghost : NAN;
            if (now - m_phaseSince > kGhostStartTimeout) return Fail("the second player did not play", now);
            return;
        }
        case Phase::Aligning:
            if (a) TickAligning(now, *a);
            return;
        case Phase::Ready:
            if (a) TickReady(now, *a);
            return;
        case Phase::TakingOver: {
            // Both copies of A, sample for sample: a straight crossfade leaves the sound unchanged.
            double p = std::clamp((now - m_phaseSince) / kTakeOverSeconds, 0.0, 1.0);
            Gain({1 - p, p, 1, 1});
            if (p < 1) return;
            if (m_decks.MainOnNext()) return Enter(Phase::Mixing, now);
            std::optional<double> heard = m_decks.HeardTime();
            if (heard && *heard > first + kSwitchMargin) return Fail("Apple's player did not switch", now);
            return;
        }
        case Phase::Mixing: {
            if (!m_decks.MainOnNext()) return Fail("Apple's player left the next song", now);
            std::optional<double> heard = m_decks.HeardTime();  // A's time, carried by the deck
            if (!heard) return;
            if (!m_lagMeasured) LearnSwitchDelay();
            Gains gains = GainsAt(m_plan.keys, *heard);
            Gain({gains.b, gains.a, gains.bLow, gains.aLow});
            if (*heard < m_plan.keys.back().at) return;
            LetGo();
            Gain(kApplesAlone);
            return Enter(Phase::Done, now);
        }
        default:
            return;
    }
}

}  // namespace ampify::automix
