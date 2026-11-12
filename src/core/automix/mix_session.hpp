#pragma once

// AutoMix's session: plays one MixPlan with Apple's player, a second player of Ampify's own and the deck that mixes
// them (MixDecks; mix_deck.hpp is the deck).
//
// A few seconds before the mix, the second player starts the current song (A) a little ahead of Apple's player, into
// a stream nobody hears. The deck finds Apple's very samples in it, and Apple's player is armed to leave A for the
// next song (B), at the plan's point of B, without a gap. Just before A's speed starts to move (or before the first
// key when it does not), the deck's copy of A replaces Apple's in a straight crossfade of two identical sounds. Apple's
// player, now silent, moves on to B; the deck plays the plan's volumes, lows and speed until A is gone, and the second
// player goes. Anything unexpected (pause, seek, skip, a changed queue) brings the sound back to Apple's player alone.
//
// Tick() runs on one thread, every 10 ms or so while Active(); Start, Cancel and GetStatus may come from any thread.

#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "mix_deck.hpp"
#include "mix_plan.hpp"

namespace ampify::automix {

// The platform's side: Apple's player (A, then B), the second player and the deck. Times in seconds of each song.
class MixDecks {
public:
    virtual ~MixDecks() = default;
    virtual std::optional<double> MainTime() = 0;  // the song Apple's player plays now
    virtual std::string NextItem() = 0;            // the song Apple's queue plays next, empty if none
    virtual bool MainOnNext() = 0;                 // Apple's player moved on to the song the plan is for
    // The second player, playing the current song into a stream of its own, and the deck open for `plan`.
    virtual bool StartGhost(const MixPlan& plan) = 0;
    virtual std::optional<double> GhostTime() = 0;  // the second player's own clock
    virtual bool GhostCaptured() = 0;               // its sound reaches the deck, and only the deck
    virtual void SeekGhost(double to) = 0;
    virtual void StopGhost() = 0;                     // and the deck closes
    virtual std::optional<DeckStep> Align() = 0;      // the deck finds Apple's samples in the second player's sound
    virtual std::optional<double> HeardTime() = 0;    // A's time in the deck's copy; stands still when it starves
    virtual double CopyErrorDb() = 0;                 // the deck's copy against Apple's, NAN while unknown
    // Seconds between A's last sample and B's first in Apple's player, counted in samples; none until it is known.
    virtual std::optional<double> SwitchGap() = 0;
    virtual void SetGains(const DeckGains& gains) = 0;
    virtual bool ArmSwitch(double aEnd, double bAt) = 0;  // A stops at aEnd and B takes over at bAt, gaplessly
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
    double copyErrorDb = NAN;   // the deck's copy of A against Apple's, last measure: -60 is the same sound
    double leadSeconds = NAN;   // how far ahead of the deck's reading the second player runs
    double switchLagMs = NAN;   // once mixing, how far the next song is behind the plan, counted in samples
    int seeks = 0;              // seeks of the second player for this plan
};

class MixSession {
public:
    // The second player starts this long, plus the lead the plan needs, before the deck takes A.
    static constexpr double kPrepareLead = 8;
    static constexpr double kSettle = 0.4;           // the deck looks for Apple's sound this long after a seek
    static constexpr double kRetry = 0.25;           // and again this long after a search that found nothing
    static constexpr int kMaxSeeks = 4;
    static constexpr double kLeadSlack = 0.5;        // the lead may miss what the plan needs by this much
    static constexpr double kMaxCopyErrorDb = -12;   // above this the two copies are not the same sound in step
    static constexpr double kTakeOverLead = 0.3;     // the copies of A cross this long before A must be the deck's
    static constexpr double kTakeOverSeconds = 0.1;
    static constexpr double kRecoverSeconds = 0.3;
    static constexpr double kQuickRecoverSeconds = 0.05;  // the user paused or skipped: no tail
    static constexpr double kGhostStartTimeout = 8;
    // Apple's player starts the next song this long after its switch point, so B is sought this much further.
    // Measured on 12/11/2026 in the recorded sound: 60 or 70 ms. Counted again at each mix.
    static constexpr double kSwitchDelay = 0.065;
    // Of that, what the streams do not show: on every mix recorded, and for every point B was sent to, B sounded
    // 30.0 ms later than the silence between the two streams and at the head of the new one add up to.
    static constexpr double kUnseenDelay = 0.03;

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
    void Gain(const DeckGains& gains);
    void Fail(const std::string& reason, double now, bool quick = false);
    void SeekNow(double aTime, double now);
    void TickLocked(double now);
    double TakeAt() const;
    bool LeadFits(double lead) const;
    void TickAligning(double now, double aTime);
    void TickReady(double now, double aTime);
    void LearnSwitchDelay();
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
    double m_wantedLead = 0;  // what the last seek aimed for
    double m_latency = 0.1;   // how far behind its target a seek lands, learned
    double m_switchDelay = kSwitchDelay;  // how late Apple's player starts B after the switch, learned
    int m_switchDelaySamples = 0;
    double m_armedDelay = 0;  // the delay B was sought for in this plan
    double m_lastCheck = 0;
    double m_lastA = NAN;
    double m_lastAMoved = -1;
    double m_lastGhost = NAN;      // the second player's clock at the last tick, while it starts
    double m_lastHeard = NAN;
    double m_lastHeardMoved = -1;  // while the deck's copy is heard, when its time last moved
    DeckGains m_gains;             // last gains set
    DeckGains m_recoverFrom;
    double m_recoverSeconds = kRecoverSeconds;
};

}  // namespace ampify::automix
