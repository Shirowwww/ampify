#pragma once

// AutoMix's session: plays one MixPlan with Apple's player and a second player of Ampify's own (MixDecks).
//
// A few seconds before the mix, the second player starts the current song (A) silently and is put in step with Apple's
// player by seeking: a seek lands about 100 ms behind its target, a few milliseconds more or less each time, so the
// session learns that delay and seeks again until both copies of A are within a millisecond. Apple's player is armed
// to leave A at the plan's first key for the next song (B), at the plan's point of B, without a gap. Just before that,
// the two copies of A cross in 100 ms, so the second player carries A on while Apple's player, now silent, moves to B.
// Then both volumes follow the plan until A is gone, and the second player goes. Anything unexpected (pause, seek, skip,
// a changed queue) brings the sound back to Apple's player alone.
//
// Tick() runs on one thread, every 10 ms or so while Active(); Start, Cancel and GetStatus may come from any thread.

#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "mix_plan.hpp"

namespace ampify::automix {

// The platform's side: Apple's player (A, then B) and the second player, on A. Times in seconds of each song.
class MixDecks {
public:
    virtual ~MixDecks() = default;
    virtual std::optional<double> MainTime() = 0;  // the song Apple's player plays now
    virtual std::string NextItem() = 0;            // the song Apple's queue plays next, empty if none
    virtual bool MainOnNext() = 0;                 // Apple's player moved on to the song the plan is for
    virtual bool StartGhost() = 0;                 // the second player, silent and playing, on the current song
    virtual std::optional<double> GhostTime() = 0;
    virtual void SeekGhost(double to) = 0;
    virtual void StopGhost() = 0;
    virtual void SetGains(double main, double ghost) = 0;  // 0..1 of the user's volume
    virtual bool ArmSwitch(double aEnd, double bAt) = 0;   // A stops at aEnd and B takes over at bAt, gaplessly
    // Undoes ArmSwitch: A gets its own end back. With rewindNext, B goes back to its start, even if it already plays
    // (the user skipped to it before the switch).
    virtual void Disarm(bool rewindNext) = 0;
};

enum class Phase { Idle, Waiting, Preparing, Aligning, Ready, TakingOver, Mixing, Recovering, Done, Failed };

std::string_view PhaseName(Phase phase);

struct Status {
    Phase phase = Phase::Idle;
    std::string next;           // the song of the plan
    std::string reason;         // why it failed or was cancelled
    double alignErrorMs = NAN;  // last measure of the second player against Apple's, in milliseconds
    double switchLagMs = NAN;   // once mixing, how far the next song is behind the plan, by the clocks
    int seeks = 0;              // seeks of the second player for this plan
};

class MixSession {
public:
    static constexpr double kPrepareLead = 12;       // seconds before the mix the second player starts
    static constexpr double kTolerance = 0.001;      // both copies of A within 1 ms...
    static constexpr int kExactSeeks = 6;            // ...for this many seeks, then 2 ms, then 3 ms after 10
    static constexpr double kSettle = 0.4;           // a seek's clock is read this long after it
    static constexpr int kMaxSeeks = 20;
    static constexpr double kTakeOverLead = 0.3;     // the copies of A cross this long before the switch
    static constexpr double kTakeOverSeconds = 0.1;
    static constexpr double kRecoverSeconds = 0.3;
    static constexpr double kQuickRecoverSeconds = 0.05;  // the user paused or skipped: no tail
    static constexpr double kGhostStartTimeout = 8;
    static constexpr double kLagSettle = 0.5;        // B's clock is read this long after the switch
    // Apple's player starts the next song this long after its switch point (50 ms, measured on 12/11/2026 by the
    // clocks and in the recorded sound alike), so B is sought this much further. Learned again at each mix.
    static constexpr double kSwitchDelay = 0.05;

    explicit MixSession(MixDecks& decks) : m_decks(decks) {}

    // Takes a plan: empty when accepted, else why not. A plan whose mix has not begun is replaced.
    std::string Start(const MixPlan& plan, double aDuration, double bDuration);
    // Drops the plan; the sound goes back to Apple's player alone, at once when `quick`. Any time, any thread.
    void Cancel(const std::string& reason, bool quick = false);
    void Tick(double now);        // now: a steady clock, seconds
    bool Active() const;          // Tick is needed
    double TickInterval() const;  // seconds until the next Tick is useful
    Status GetStatus() const;

private:
    void Enter(Phase phase, double now);
    void Gain(double main, double ghost);
    void Fail(const std::string& reason, double now, bool quick = false);
    void SeekNow(double aTime, double now);
    std::optional<double> AlignError();
    void TickLocked(double now);
    void TickAligning(double now, double aTime);
    void LearnSwitchDelay(double sinceSwitch);
    bool SwitchedOnPlan();  // Apple's player moved to B at the plan's point, not before (a skip)
    void LetGo();           // the second player stops and Apple's player is left as it plays

    MixDecks& m_decks;
    mutable std::mutex m_lock;
    MixPlan m_plan;
    Status m_status;
    std::optional<std::string> m_cancel;  // asked by Cancel, done by the next Tick
    bool m_cancelQuick = false;
    bool m_ghostOn = false;
    bool m_armed = false;
    bool m_lagMeasured = false;
    double m_phaseSince = -1;
    double m_lastSeek = 0;
    double m_seekOffset = 0;  // target - A's time at the last seek
    double m_latency = 0.1;   // how far behind its target a seek lands, learned
    int m_latencySamples = 0;
    double m_switchDelay = kSwitchDelay;  // how late Apple's player starts B after the switch, learned
    int m_switchDelaySamples = 0;
    double m_armedDelay = 0;  // the delay B was sought for in this plan
    double m_lastCheck = 0;
    double m_lastA = NAN;
    double m_lastAMoved = -1;
    double m_lastGhost = NAN;
    double m_lastGhostMoved = -1;  // while the second player is heard, when its clock last moved
    Gains m_gains;  // last volumes set: a = Apple's player, b = the second player
    Gains m_recoverFrom;
    double m_recoverSeconds = kRecoverSeconds;
};

}  // namespace ampify::automix
