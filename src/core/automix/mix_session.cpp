#include "mix_session.hpp"

#include <algorithm>

namespace ampify::automix {

namespace {

bool Running(Phase phase) { return phase != Phase::Idle && phase != Phase::Done && phase != Phase::Failed; }

// Phases where both players may be heard: a failure there fades back instead of cutting.
bool Audible(Phase phase) { return phase == Phase::TakingOver || phase == Phase::Mixing; }

constexpr double kSeekSafety = 1.0;     // no seek of the second player closer than this to the mix
constexpr double kMaxLatency = 1.0;     // a seek that lands later than this tells nothing about the next one
constexpr int kLatencyMemory = 8;
constexpr double kStallSeconds = 1.5;   // Apple's player not moving for this long: paused or stuck
constexpr double kSwitchMargin = 0.5;   // Apple's player still on A this long after the switch point: it will not
constexpr double kMaxSwitchDelay = 0.25;  // a later start of B is a stall, not the player's usual delay
constexpr double kSwitchSlack = 0.1;      // Apple's player on B this long before the switch point: the user skipped

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
    // A plan whose mix has not begun is replaced: its second player is silent, nothing is heard.
    if (m_ghostOn) m_decks.StopGhost();
    if (m_armed) m_decks.Disarm(true);
    m_ghostOn = m_armed = m_lagMeasured = false;
    m_plan = plan;
    m_status = Status{};
    m_status.phase = Phase::Waiting;
    m_status.next = plan.next;
    m_cancel.reset();
    m_phaseSince = -1;
    m_lastA = NAN;
    m_lastAMoved = -1;
    m_lastGhost = NAN;
    m_lastGhostMoved = -1;
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

void MixSession::Gain(double main, double ghost) {
    m_gains = {main, ghost};
    m_decks.SetGains(main, ghost);
}

bool MixSession::SwitchedOnPlan() {
    if (!m_decks.MainOnNext()) return false;
    std::optional<double> ghost = m_decks.GhostTime();  // A's time: Apple's player leaves A at the first key
    return ghost && *ghost >= m_plan.keys.front().at - kSwitchSlack;
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
        // Something of the second player is heard: back to Apple's player alone without a jump. Before the switch
        // both play A in step, so the crossing is short; after it the second player's A fades out under B.
        m_recoverFrom = m_gains;
        m_recoverSeconds = quick ? kQuickRecoverSeconds : switched ? kRecoverSeconds : kTakeOverSeconds;
        Enter(Phase::Recovering, now);
        return;
    }
    LetGo();
    Gain(1, 0);
    Enter(Phase::Failed, now);
}

void MixSession::SeekNow(double aTime, double now) {
    m_seekOffset = m_latency;  // the second player wants A's very time; it lands m_latency behind its target
    m_decks.SeekGhost(aTime + m_seekOffset);
    m_lastSeek = now;
    m_status.seeks++;
}

// The second player against Apple's, both on A: the gap between them. Empty when a clock is unknown.
std::optional<double> MixSession::AlignError() {
    std::optional<double> ghost = m_decks.GhostTime();
    std::optional<double> a = m_decks.MainTime();
    if (!ghost || !a) return std::nullopt;
    double error = *ghost - *a;
    m_status.alignErrorMs = error * 1000;
    return error;
}

// Half a second into the mix, B's clock against A's (carried by the second player) says how late Apple's player
// started B: B was sought m_armedDelay further, so it stands at bStart + m_armedDelay + (time since the switch -
// the delay). The status keeps what the compensation left over.
void MixSession::LearnSwitchDelay(double sinceSwitch) {
    std::optional<double> b = m_decks.MainTime();
    if (!b) return;
    double delay = sinceSwitch - (*b - (m_plan.bStart + m_armedDelay));
    m_status.switchLagMs = (delay - m_armedDelay) * 1000;
    if (!(delay >= 0 && delay < kMaxSwitchDelay)) return;
    m_switchDelay = (m_switchDelay * m_switchDelaySamples + delay) / (m_switchDelaySamples + 1);
    m_switchDelaySamples = std::min(m_switchDelaySamples + 1, kLatencyMemory);
}

void MixSession::TickAligning(double now, double aTime) {
    const double first = m_plan.keys.front().at;
    if (now - m_lastSeek < kSettle) return;
    std::optional<double> error = AlignError();
    if (!error) return;
    // Seeks land a few milliseconds apart: a mix should not fail for want of luck. Within 3 ms the two copies of A
    // cross in 0.1 s without a sound of their own.
    const int seeks = m_status.seeks;
    const double tolerance = seeks <= kExactSeeks       ? kTolerance
                             : seeks <= kExactSeeks + 4 ? 2 * kTolerance
                                                        : 3 * kTolerance;
    if (std::abs(*error) <= tolerance) {
        if (!m_armed) {
            m_armedDelay = m_switchDelay;
            if (!m_decks.ArmSwitch(first, m_plan.bStart + m_armedDelay)) {
                return Fail("Apple's player refused the switch", now);
            }
        }
        m_armed = true;
        m_lastCheck = now;
        return Enter(Phase::Ready, now);
    }
    double landed = m_seekOffset - *error;  // how far behind its target this seek landed
    if (landed > 0 && landed < kMaxLatency) {
        m_latency = (m_latency * m_latencySamples + landed) / (m_latencySamples + 1);
        m_latencySamples = std::min(m_latencySamples + 1, kLatencyMemory);
    }
    if (m_status.seeks >= kMaxSeeks || aTime > first - kSeekSafety) {
        return Fail("the second player could not be put in step", now);
    }
    SeekNow(aTime, now);
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
        Gain(m_recoverFrom.a + (1 - m_recoverFrom.a) * p, m_recoverFrom.b * (1 - p));
        if (p < 1) return;
        LetGo();
        return Enter(Phase::Failed, now);
    }
    // While it is heard, the second player carries A: frozen, A falls silent and the mix would never end.
    if (Audible(phase)) {
        std::optional<double> ghost = m_decks.GhostTime();
        if (m_lastGhostMoved < 0 || (ghost && *ghost != m_lastGhost)) {
            m_lastGhost = ghost.value_or(NAN);
            m_lastGhostMoved = now;
        } else if (now - m_lastGhostMoved > kStallSeconds) {
            return Fail("the second player stopped", now, true);
        }
    }
    const double first = m_plan.keys.front().at;
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
            if (*a > first - kSeekSafety - kSettle) return Fail("too late for this mix", now);
            if (*a < first - kPrepareLead) return;
            if (!m_decks.StartGhost()) return Fail("the second player did not start", now);
            m_ghostOn = true;
            m_lastGhost = NAN;
            return Enter(Phase::Preparing, now);
        case Phase::Preparing: {
            // Seeks are measured once the second player plays: its first start takes far longer than a seek.
            std::optional<double> ghost = m_decks.GhostTime();
            if (ghost && !std::isnan(m_lastGhost) && *ghost > m_lastGhost && a) {
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
        case Phase::Ready: {
            if (m_decks.NextItem() != m_plan.next) return Fail("the queue changed", now);
            if (!a) return;
            if (now - m_lastCheck >= 0.5) {
                m_lastCheck = now;
                std::optional<double> error = AlignError();
                if (error && std::abs(*error) > 3 * kTolerance) {
                    if (*a > first - 2 * kSeekSafety || m_status.seeks >= kMaxSeeks) {
                        return Fail("the second player drifted", now);
                    }
                    SeekNow(*a, now);
                    return Enter(Phase::Aligning, now);
                }
            }
            if (*a >= first - kTakeOverLead) return Enter(Phase::TakingOver, now);
            return;
        }
        case Phase::TakingOver: {
            // Both players on A in step: a straight crossfade leaves the sound unchanged.
            double p = std::clamp((now - m_phaseSince) / kTakeOverSeconds, 0.0, 1.0);
            Gain(1 - p, p);
            if (p < 1) return;
            if (m_decks.MainOnNext()) return Enter(Phase::Mixing, now);
            std::optional<double> ghost = m_decks.GhostTime();
            if (ghost && *ghost > first + kSwitchMargin) return Fail("Apple's player did not switch", now);
            return;
        }
        case Phase::Mixing: {
            if (!m_decks.MainOnNext()) return Fail("Apple's player left the next song", now);
            std::optional<double> ghost = m_decks.GhostTime();  // A's time, carried by the second player
            if (!ghost) return;
            if (!m_lagMeasured && now - m_phaseSince >= kLagSettle) {
                m_lagMeasured = true;
                LearnSwitchDelay(*ghost - first);
            }
            Gains gains = GainsAt(m_plan.keys, *ghost);
            Gain(gains.b, gains.a);
            if (*ghost < m_plan.keys.back().at) return;
            LetGo();
            Gain(1, 0);
            return Enter(Phase::Done, now);
        }
        default:
            return;
    }
}

}  // namespace ampify::automix
