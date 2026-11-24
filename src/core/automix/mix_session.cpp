#include "mix_session.hpp"

#include <algorithm>
#include <cmath>

namespace ampify::automix {

namespace {

bool Running(Phase phase) { return phase != Phase::Idle && phase != Phase::Done && phase != Phase::Failed; }

// Phases where the deck's copy may be heard: a failure there fades back instead of cutting.
bool Audible(Phase phase) { return phase == Phase::TakingOver || phase == Phase::Mixing; }

constexpr double kSeekSafety = 1.0;     // no seek of the second player closer than this to the takeover
constexpr double kAlignSafety = 0.2;    // the deck must have found Apple's sound this long before it
constexpr double kMaxLatency = 1.0;     // a seek that lands later than this tells nothing about the next one
constexpr int kLatencyMemory = 8;
constexpr double kMinNudge = 0.003;  // a B this close to its beat is left as it is
constexpr double kMaxNudge = 0.06;   // further off, something else than Apple's player is at play: left alone
// The plan's gains given ahead at this pace, this many: twice the longest stall seen (0.37 s, 20/11/2026, three
// seconds after a switch, inside Apple's player). Keys and the echo's edges come exactly where they fall on top.
constexpr double kAheadStep = 0.04;
constexpr int kAheadSteps = 20;
constexpr double kGateEdge = 0.001;
constexpr double kStallSeconds = 1.5;   // a player not moving for this long: paused or stuck
constexpr double kSwitchMargin = 0.5;   // Apple's player still on A this long after the switch point: it will not
constexpr double kMaxSwitchDelay = 0.25;  // a later start of B is a stall, not the player's usual delay
constexpr double kSwitchSlack = 0.2;      // Apple's player on B this long before the switch point: the user skipped
constexpr double kCutArmLead = 3;      // a cut arms Apple's player this long before, so B is sought where it starts
constexpr double kCutArmSafety = 0.5;  // and no later than this before

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

bool MixShown(const Status& status, bool mixed) {
    switch (status.phase) {
        case Phase::Preparing:
        case Phase::Aligning:
        case Phase::Ready:
        case Phase::TakingOver:  // a cut mixes nothing: it shows nothing before its switch
            return status.heardFor > 0 && status.switchIn <= std::min(status.heardFor, kMixShownAhead);
        case Phase::Mixing: return true;
        case Phase::Recovering: return mixed;
        default: return false;
    }
}

std::string MixSession::Start(const MixPlan& plan, double aDuration, double bDuration) {
    std::string problem = Check(plan, aDuration, bDuration);
    if (!problem.empty()) return problem;
    std::lock_guard<std::mutex> guard(m_lock);
    if (Audible(m_status.phase) || m_status.phase == Phase::Recovering) return "a mix is playing";
    // A plan whose mix has not begun is replaced: nothing of its second player is heard.
    if (m_ghostOn) m_decks.StopGhost();
    if (m_armed) m_decks.Disarm(true);
    m_ghostOn = m_armed = m_lagMeasured = m_heldDropped = m_ghostQuiet = false;
    m_aDuration = aDuration;
    m_plan = plan;
    m_status = Status{};
    m_status.phase = Phase::Waiting;
    m_status.next = plan.next;
    m_status.heardFor = (plan.keys.back().at - plan.keys.front().at) / plan.rate + EchoAfter(plan);
    m_cancel.reset();
    m_phaseSince = -1;
    m_lastA = m_lastGhost = m_lastHeard = m_lastAOnA = NAN;
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
    m_endSince = -1;
}

bool MixSession::HeldBackPlayed(double now) {
    if (m_endSince < 0) {
        m_endSince = now;
        QuietGhostIfShort();
    }
    return m_heldDropped || now - m_endSince >= LeftToPlay();
}

// A failed mix fades its echo out with A: only the sound held back is waited for.
double MixSession::LeftToPlay() const {
    return m_decks.SoundHeldBack() + (m_status.phase == Phase::Mixing ? EchoAfter(m_plan) : 0);
}

// Called once nothing more of the second player's sound is needed. Playing on, it would reach the end of A and stop
// its stream before what Sing holds back (or the echo) is heard: that end would be cut, and left over for the next mix.
void MixSession::QuietGhostIfShort() {
    const double left = LeftToPlay();
    if (left <= 0 || m_heldDropped || !m_ghostOn || m_ghostQuiet) return;
    const std::optional<double> ghost = m_decks.GhostTime();
    if (ghost && m_aDuration > 0 && *ghost + left + kGhostEndMargin <= m_aDuration) return;
    m_decks.QuietGhost();
    m_ghostQuiet = true;
}

// After the last key the echo rings on by itself; over its last second it is faded out, not cut when the deck goes.
void MixSession::FadeEcho(double now) {
    if (!Echoes(m_plan) || m_status.phase != Phase::Mixing) return;
    DeckGains gains = m_gains;
    gains.echoSend = 0;
    gains.echoReturn = std::clamp((EchoAfter(m_plan) - (now - m_endSince)) / kEchoFadeSeconds, 0.0, 1.0);
    Gain(gains);
}

void MixSession::Gain(const DeckGains& gains) {
    m_gains = gains;
    m_decks.SetGains(gains);
}

// During a mix the deck gets the plan's gains for the next moments at each tick, each key and each edge of the echo
// exactly where it falls: this thread can stall inside Apple's player (0.18 and 0.37 s on 20/11/2026), and a key
// that came then landed that late.
void MixSession::GainAhead(double heard) {
    auto at = [this](double aTime) {
        const Gains gains = GainsAt(m_plan.keys, aTime);
        return DeckGains{gains.b,       gains.a,       gains.bLow, gains.aLow, gains.bFilter, gains.aFilter,
                         EchoSendAt(m_plan, aTime), Echoes(m_plan) ? 1.0 : 0.0, gains.bThin, gains.aMuffle,
                         GateAt(m_plan, aTime)};
    };
    // From the first key on, A plays at the plan's speed: its time passes that much faster than the listener's.
    const double speed = m_plan.rate;
    std::vector<std::pair<double, DeckGains>> points;
    for (int step = 0; step <= kAheadSteps; step++) {
        const double seconds = step * kAheadStep;
        points.push_back({seconds, at(heard + seconds * speed)});
    }
    std::vector<double> edges;
    for (const GainKey& key : m_plan.keys) edges.push_back(key.at);
    if (Echoes(m_plan)) edges.insert(edges.end(), {m_plan.echo.from, m_plan.echo.to});
    for (const double edge : edges) {
        const double seconds = (edge - heard) / speed;
        if (seconds > 0 && seconds < kAheadSteps * kAheadStep) points.push_back({seconds, at(edge)});
    }
    // The gate goes up and down at once on its edges, a millisecond each: B slams in on the drop, not over a step.
    if (Gates(m_plan)) {
        // Inside an edge's millisecond, its end still goes in: without it the gate would ramp over the next step.
        for (const double edge : {m_plan.gate.beats.front() - kStabRise, m_plan.gate.until}) {
            const double after = (edge - heard) / speed;
            const double before = after - kGateEdge / speed;
            if (after <= 0 || before >= kAheadSteps * kAheadStep) continue;
            if (before > 0) points.push_back({before, at(edge - kGateEdge)});
            points.push_back({after, at(edge)});
        }
    }
    std::sort(points.begin(), points.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    m_gains = points.front().second;
    m_decks.SetGainsAhead(points);
}

bool MixSession::SwitchedOnPlan() {
    if (!m_decks.MainOnNext()) return false;
    // A's time: Apple's player leaves A at the first key. A deck that lost its place no longer tells it: the last
    // time it gave stands, or B, switched on the plan, would go back to its start. A cut has no deck: A's last time
    // on Apple's player tells.
    const double heard = Cuts(m_plan) ? m_lastAOnA : m_decks.HeardTime().value_or(m_lastHeard);
    return heard >= m_plan.keys.front().at - kSwitchSlack;
}

// A cut needs no second player: Apple's player is armed a few seconds before, so that B is sought to where it starts.
void MixSession::TickCutWaiting(double now, double aTime) {
    const double at = m_plan.keys.front().at;
    if (aTime > at - kCutArmSafety) return Fail("too late for this mix", now);
    if (aTime < at - kCutArmLead) return;
    m_armedDelay = m_switchDelay;
    if (!m_decks.ArmSwitch(at, m_plan.bStart + m_armedDelay)) return Fail("Apple's player refused the switch", now);
    m_armed = true;
    m_lastAOnA = aTime;
    m_lastCheck = now;
    Enter(Phase::Ready, now);
}

void MixSession::TickCutReady(double now, double aTime) {
    if (m_decks.MainOnNext()) {
        if (!SwitchedOnPlan()) return Fail("skipped", now);  // the user went to B before the cut
        LetGo();
        return Enter(Phase::Done, now);
    }
    m_lastAOnA = aTime;
    if (now - m_lastCheck >= kRetry) {
        m_lastCheck = now;
        if (m_decks.NextItem() != m_plan.next) return Fail("the queue changed", now);
    }
    if (aTime > m_plan.keys.front().at + kSwitchMargin) return Fail("Apple's player did not switch", now);
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
        // The user moved on: what is held back of A is of a song left, not the end of a fade to hear out.
        if (quick) {
            m_decks.DropHeldBack();
            m_heldDropped = true;
        }
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
// up for it. The deck counts that silence in samples; the status keeps what was not made up for. A next song that
// starts with silence of its own adds it to the count (55 ms on 20/11/2026, where Apple's player took 60 ms at each
// switch): what is made up for is the lower median of the last delays, which one such song does not move.
void MixSession::LearnSwitchDelay() {
    const std::optional<double> gap = m_decks.SwitchGap();
    if (!gap) return;
    m_lagMeasured = true;
    const double delay = *gap + kUnseenDelay;
    const double lag = delay - m_armedDelay;
    m_status.switchLagMs = lag * 1000;
    // Apple's player now and then takes a pass or three more than usual, which no learning foresees (+30 ms on a
    // beat mix, 20/11/2026). On the beat, A, stretched anyway, is set back as much to meet B's beat.
    if (Stretches(m_plan) && std::abs(lag) >= kMinNudge && std::abs(lag) <= kMaxNudge) m_decks.Nudge(lag);
    if (!(delay >= 0 && delay < kMaxSwitchDelay)) return;
    m_switchDelays.push_back(delay);
    if (m_switchDelays.size() > static_cast<size_t>(kLatencyMemory)) m_switchDelays.erase(m_switchDelays.begin());
    std::vector<double> sorted = m_switchDelays;
    std::sort(sorted.begin(), sorted.end());
    m_switchDelay = sorted[(sorted.size() - 1) / 2];
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
    const double takeAt = TakeAt();
    // A look at the queue copies its items: four times a second, not at each of the fifty ticks.
    if (now - m_lastCheck >= kRetry || aTime >= takeAt) {
        if (m_decks.NextItem() != m_plan.next) return Fail("the queue changed", now);
    }
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
    // The last look can be a quarter of a second old: a measure that came in since is read before deciding.
    if (std::isnan(m_status.copyErrorDb)) m_status.copyErrorDb = m_decks.CopyErrorDb();
    // Only a copy measured to be Apple's own sound goes on air. A search can land on the wrong beat at another level
    // (0.29 s off and 1.5 dB under at a match of 0.81, 20/11/2026): until the measure that follows it, or when no
    // measure can be made, nothing proves the copy, and A's end at the wrong level is what the mix would start with.
    if (std::isnan(m_status.copyErrorDb)) return Fail("the second player's copy could not be checked", now);
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
              m_recoverFrom.tailLow, Lerp(m_recoverFrom.mainFilter, 0, p), m_recoverFrom.tailFilter, 0,
              m_recoverFrom.echoReturn * (1 - p), Lerp(m_recoverFrom.mainThin, 0, p), m_recoverFrom.tailMuffle});
        if (p < 1 || !HeldBackPlayed(now)) return;
        LetGo();
        Gain(kApplesAlone);
        return Enter(Phase::Failed, now);
    }
    // The second player can move to a stream the deck cannot take (another rate, 20/11/2026): what it plays is no
    // longer the deck's to give, and what the deck holds runs out.
    if (m_ghostOn && phase >= Phase::Aligning && phase <= Phase::Mixing && !m_decks.GhostCaptured()) {
        return Fail("the second player's sound left the deck", now);
    }
    // While it is heard, the deck carries A: starved of the second player's sound, A falls silent and its time
    // stands still. Once all of it was given, A may end while what is held back plays out.
    if (Audible(phase) && m_endSince < 0) {
        std::optional<double> heard = m_decks.HeardTime();
        if (!heard) return Fail("the second player stopped", now, true);
        if (m_lastHeardMoved < 0 || *heard != m_lastHeard) {
            m_lastHeard = *heard;
            m_lastHeardMoved = now;
        } else if (now - m_lastHeardMoved > kStallSeconds) {
            return Fail("the second player stopped", now, true);
        }
    }
    const double takeAt = TakeAt();
    std::optional<double> a = m_decks.MainTime();  // A until the switch, then B
    if (a) {
        if (m_lastAMoved < 0 || *a != m_lastA) m_lastAMoved = now;
        m_lastA = *a;
        // Apple's player can be on B only from the takeover on, where a look at its item is made anyway.
        if (phase < Phase::TakingOver || (phase == Phase::TakingOver && !m_decks.MainOnNext())) {
            m_status.switchIn = SwitchTime(m_plan) - *a;
        }
    }
    // Apple's player paused or stalled: the hooks say so first, this is the fallback.
    if (phase != Phase::Waiting && m_lastAMoved >= 0 && now - m_lastAMoved > kStallSeconds) {
        return Fail("the song stopped moving", now, true);
    }
    // Once the deck holds the loop, the second player has given all that is needed: paused, it cannot reach the end
    // of A and take its stream away while the loop plays on.
    if (Audible(phase) && Loops(m_plan) && m_ghostOn && !m_ghostQuiet && m_decks.LoopHeld()) {
        m_decks.QuietGhost();
        m_ghostQuiet = true;
    }
    // Nor may it reach the end of A while the deck still needs what it gave: paused a little before, its stream goes
    // on and the deck plays out what it holds, A's last seconds included.
    if (m_ghostOn && !m_ghostQuiet && m_aDuration > 0 && phase != Phase::Preparing) {
        const std::optional<double> ghost = m_decks.GhostTime();
        if (ghost && *ghost >= m_aDuration - kGhostEndMargin) {
            m_decks.QuietGhost();
            m_ghostQuiet = true;
        }
    }
    switch (phase) {
        case Phase::Waiting:
            if (m_decks.NextItem() != m_plan.next) return Fail("the queue changed", now);
            if (!a) return;
            if (Cuts(m_plan)) return TickCutWaiting(now, *a);
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
            if (!a) return;
            if (Cuts(m_plan)) return TickCutReady(now, *a);
            TickReady(now, *a);
            return;
        case Phase::TakingOver: {
            // Both copies of A, sample for sample: a straight crossfade leaves the sound unchanged.
            double p = std::clamp((now - m_phaseSince) / kTakeOverSeconds, 0.0, 1.0);
            Gain({1 - p, p, 1, 1});
            if (p < 1) return;
            if (m_decks.MainOnNext()) return Enter(Phase::Mixing, now);
            // Apple's own A past its end tells that it did not switch. The deck's copy plays on through a stall of
            // Apple's player right at the switch (0.52 s on 20/11/2026), while Apple's A is held at its end.
            if (a && *a > SwitchTime(m_plan) + kSwitchMargin) return Fail("Apple's player did not switch", now);
            return;
        }
        case Phase::Mixing: {
            if (!m_decks.MainOnNext()) return Fail("Apple's player left the next song", now);
            if (m_endSince < 0) {
                std::optional<double> heard = m_decks.HeardTime();  // A's time, carried by the deck
                if (!heard) return;
                if (!m_lagMeasured) LearnSwitchDelay();
                GainAhead(*heard);
                if (*heard < m_plan.keys.back().at) return;
                if (const std::optional<double> drift = m_decks.ApartDrift()) m_status.driftMs = *drift * 1000;
            }
            if (!HeldBackPlayed(now)) return FadeEcho(now);
            LetGo();
            Gain(kApplesAlone);
            return Enter(Phase::Done, now);
        }
        default:
            return;
    }
}

}  // namespace ampify::automix
