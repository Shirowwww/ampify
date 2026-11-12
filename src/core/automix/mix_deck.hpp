#pragma once

// AutoMix's deck: carries the end of the current song (A) while Apple's player moves on to the next one (B).
//
// Apple's player makes a new output stream for each song, so no sound of A can ride its stream past the switch. A
// second player plays A again, a few seconds ahead, into a stream of its own, and that stream is the deck's: what
// the second player writes there waits here, and what leaves in its place is the deck's copy of A, late by exactly
// what it takes to be Apple's own A to the sample (Align). The deck's copy then replaces Apple's in a crossfade of
// two identical sounds, Apple's player is free to move on to B, and the deck owns both songs apart: a volume and a
// low shelf for each, and A's speed (TempoStretch), which the seconds of lead pay for.
//
// Two streams are in step when Windows takes the same frames from both at the same time. It takes them in whole
// periods, from every stream at once, so how far apart two streams are is a whole number of periods, known exactly
// from what each stream had waiting when it was written (Block).
//
// Three threads: the one that runs the mix (Open, Close, Align, SetStretch, SetGains, Time) and the audio threads of
// the two streams (ProcessMain, ProcessTail). Pure C++.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace ampify::automix {

class TempoStretch;

// 0..1 each. `main` is Apple's stream as it comes, `tail` the deck's copy of A; the lows are what is left of each
// one's bass.
struct DeckGains {
    double main = 1;
    double tail = 0;
    double mainLow = 1;
    double tailLow = 1;
};

struct DeckStep {
    double lead = 0;   // seconds the second player runs ahead of what the deck plays
    double match = 0;  // how alike the two copies are where they were compared, 1 for the same sound
};

// One block of a stream, as its player hands it to Windows.
struct DeckBlock {
    // Names the stream, never 0: a player makes a new one for each song and at each seek. Not its address, which
    // Windows hands out again to the next stream.
    std::uint64_t stream = 0;
    size_t queued = 0;             // frames written before this block that Windows had not taken yet
    double now = 0;                // a steady clock, seconds
};

class MixDeck {
public:
    MixDeck();
    ~MixDeck();

    // ---- the mix's thread

    // Starts listening to both streams, at Apple's stream's rate, keeping `holdSeconds` of the second player's sound.
    void Open(int sampleRate, double holdSeconds);
    void Close();  // both streams pass untouched again

    // Finds Apple's last quarter of a second in the second player's sound, around `leadHint` seconds behind its
    // newest sample, and plays from there on, in step with Apple's stream. `aTime` is A's time now, `volume` the
    // user's volume now. Empty when the sound is too quiet to recognise, is not found, or the streams are too new
    // to tell how far apart they are. A lead that cannot be played (not ahead, or further than what is kept) is
    // returned and nothing more. While the deck's copy is heard, nothing moves.
    std::optional<DeckStep> Align(double leadHint, double aTime, double volume);

    // A's speed moves from 1 at `from` (A's time) to `rate` at `first`, evenly in the listener's time, then stays.
    void SetStretch(double from, double first, double rate);

    // Reached in a straight line a fiftieth of a second from now, at the same instant in both streams. `volume` is
    // the user's volume now: the second player's sound waits a few seconds here, so the deck follows the slider.
    void SetGains(const DeckGains& gains, double volume);

    std::optional<double> Time() const;  // A's time the deck plays; empty until Align, frozen while it starves
    double TailSeconds() const;          // how much of the second player's sound came in since Open
    double ResidualDb() const;           // the deck's copy against Apple's while both play A: -60 is the same sound
    // Seconds between the last frame of Apple's stream for A and the first of its stream for B, as heard.
    std::optional<double> SwitchGap() const;

    // ---- the audio threads

    // Apple's stream, in place.
    void ProcessMain(float* stereo, size_t frames, int sampleRate, const DeckBlock& block);
    // The second player's stream, in place: its sound comes in (silence when `silent`), the deck's copy of A goes
    // out. False when what goes out is silence.
    bool ProcessTail(float* stereo, size_t frames, int sampleRate, bool silent, const DeckBlock& block);

private:
    enum class Stretch { Off, Warming, On };

    struct LowShelf {
        double z1[2] = {0, 0};
        double z2[2] = {0, 0};
        void Run(float* stereo, size_t frames, double low, int sampleRate);
        void Clear();
    };

    // How Windows takes one stream: for its last blocks, the frames taken so far (counted from the stream's first
    // frame) and how far that is ahead of the clock.
    struct Feed {
        static constexpr size_t kBlocks = 32;
        std::uint64_t stream = 0;
        std::uint64_t origin = 0;  // the deck's count at the stream's first frame
        std::uint64_t blocks = 0;
        std::array<std::int64_t, kBlocks> taken{};
        std::array<double, kBlocks> ahead{};
        bool Note(const DeckBlock& block, std::uint64_t count, int sampleRate);  // true at a new stream
        bool Settled() const;
        std::int64_t Period() const;
        double Ahead() const;
    };

    struct Point {
        std::int64_t at = 0;  // a frame of the second player's stream
        DeckGains gains;
    };

    bool Audible() const;
    std::optional<std::int64_t> Apart() const;
    DeckGains GainsAt(std::int64_t at) const;
    size_t UntilPoint(std::int64_t at, size_t frames) const;
    bool ReadTail(std::uint64_t from, size_t frames, float* out) const;
    size_t UntilEvent(size_t frames);
    bool NextTail(size_t frames);
    void Measure(const float* stereo, std::uint64_t mainFrame, size_t frames, double scale);

    mutable std::mutex m_lock;
    int m_rate = 0;
    bool m_open = false;
    std::vector<float> m_tail;  // the second player's sound, a ring of m_hold frames
    size_t m_hold = 0;
    std::uint64_t m_tailFrames = 0;  // frames of its sound kept since Open
    std::uint64_t m_tailSent = 0;    // frames its stream was handed since Open, silent blocks included
    std::vector<float> m_history;    // Apple's last second, mono
    std::uint64_t m_mainFrames = 0;  // frames of Apple's stream since Open
    Feed m_mainFeed;
    Feed m_tailFeed;

    bool m_aligned = false;
    // Apple's frame n and the second player's frame n + m_apart leave the speakers together: exactly so for the
    // stream Align measured (m_exact), near enough for the curves on the ones Apple's player makes afterwards.
    std::int64_t m_apart = 0;
    bool m_exact = false;
    // Once Apple's player moved on: where the last sounding frame of its stream for A was heard, and how many empty
    // frames its stream for B began with (counted until the first sounding one).
    std::optional<std::int64_t> m_oldEnd;
    std::uint64_t m_emptyEnd = 0;   // empty frames Apple's stream ends with so far
    std::uint64_t m_emptyHead = 0;
    bool m_headCounted = false;
    std::int64_t m_delay = 0;  // Apple's frame n is the second player's frame n + m_delay, the same samples
    double m_cursor = 0;  // the frame of the second player's sound played next; whole while A plays as recorded
    double m_pos = 0;     // A's time there
    double m_scale = 1;   // the second player's samples times this are Apple's, at m_alignVolume
    double m_alignVolume = 1;
    double m_volume = 1;
    std::vector<Point> m_curve;  // the gains over the second player's frames, straight lines between points
    LowShelf m_mainShelf;
    LowShelf m_tailShelf;

    double m_residualError = 0;
    double m_residualPower = 0;
    size_t m_residualFrames = 0;
    double m_residualDb = NAN;

    std::unique_ptr<TempoStretch> m_stretch;
    Stretch m_state = Stretch::Off;
    double m_from = 0;
    double m_rateTo = 1;
    std::uint64_t m_rampFrames = 0;
    std::uint64_t m_rampDone = 0;
    std::uint64_t m_feed = 0;  // the frame the stretch takes next, its latency ahead of m_cursor
    double m_feedDebt = 0;     // the part of a frame it is owed
    std::vector<float> m_dry;
    std::vector<float> m_wet;
    std::vector<float> m_in;
    std::vector<float> m_check;  // Apple's thread: the deck's copy, to compare
};

}  // namespace ampify::automix
