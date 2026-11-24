#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "../src/core/automix/mix_deck.hpp"
#include "../src/core/automix/mix_plan.hpp"
#include "../src/core/automix/tempo_stretch.hpp"
#include "test.hpp"

using namespace ampify::automix;

namespace {

constexpr int kRate = 24000;
constexpr size_t kBlock = 240;  // 10 ms
constexpr double kPi = std::numbers::pi;

// Something like music for the deck: two notes under a slow swell, and noise so that no two moments are alike.
std::vector<float> Song(double seconds, unsigned seed) {
    const size_t frames = static_cast<size_t>(seconds * kRate);
    std::vector<float> song(frames * 2);
    unsigned state = seed;
    double low = 0;
    for (size_t i = 0; i < frames; i++) {
        state = state * 1664525u + 1013904223u;
        const double noise = (state >> 8) / 8388608.0 - 1.0;
        low += 0.2 * (noise - low);
        const double t = static_cast<double>(i) / kRate;
        const double tone = 0.3 * std::sin(2 * kPi * 220 * t) + 0.2 * std::sin(2 * kPi * 523.25 * t + 1);
        const double swell = 0.6 + 0.4 * std::sin(2 * kPi * 0.7 * t);
        const double value = (tone + 0.5 * low) * swell * 0.5;
        song[i * 2] = static_cast<float>(value);
        song[i * 2 + 1] = static_cast<float>(value * 0.8 + 0.05 * noise);
    }
    return song;
}

// A bar of Song played over and over, each pass with noise of its own on top: alike from pass to pass, as a beat's
// bars are, but never the same sound twice.
std::vector<float> LoopedSong(double seconds, double bar, unsigned seed) {
    const std::vector<float> pass = Song(bar, seed);
    const size_t barFrames = pass.size() / 2;
    const size_t frames = static_cast<size_t>(seconds * kRate);
    std::vector<float> song(frames * 2);
    unsigned state = seed * 7919u + 1;
    for (size_t i = 0; i < frames; i++) {
        for (size_t c = 0; c < 2; c++) {
            state = state * 1664525u + 1013904223u;
            const double noise = (state >> 8) / 8388608.0 - 1.0;
            song[i * 2 + c] = static_cast<float>(pass[(i % barFrames) * 2 + c] + 0.07 * noise);
        }
    }
    return song;
}

// Bright music: partials up to three quarters of the band, as loud high as low, each swelling at its own pace. Read
// `delay` frames later, it is the same music exactly, as a second decoder that resamples it from another place gives
// it: half a frame off, the two differ by 8 dB over the whole band.
std::vector<float> Partials(double seconds, unsigned seed, double delay = 0) {
    constexpr int kCount = 32;
    struct Partial {
        double hz, phase, swellHz, swellPhase;
    };
    unsigned state = seed;
    auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) / 16777216.0;
    };
    std::vector<Partial> partials(kCount);
    for (Partial& p : partials) p = {100 * std::pow(90.0, next()), 2 * kPi * next(), 0.5 + 4 * next(), 2 * kPi * next()};
    const size_t frames = static_cast<size_t>(seconds * kRate);
    std::vector<float> song(frames * 2);
    for (size_t i = 0; i < frames; i++) {
        const double t = (static_cast<double>(i) - delay) / kRate;
        double value = 0;
        for (const Partial& p : partials) {
            value += (0.6 + 0.4 * std::sin(2 * kPi * p.swellHz * t + p.swellPhase)) * std::sin(2 * kPi * p.hz * t + p.phase);
        }
        song[i * 2] = song[i * 2 + 1] = static_cast<float>(value * 0.05);
    }
    return song;
}

std::vector<float> Tone(double seconds, double hz, double level) {
    const size_t frames = static_cast<size_t>(seconds * kRate);
    std::vector<float> tone(frames * 2);
    for (size_t i = 0; i < frames; i++) {
        tone[i * 2] = tone[i * 2 + 1] = static_cast<float>(level * std::sin(2 * kPi * hz * i / kRate));
    }
    return tone;
}

// Apple's stream and the second player's, as Windows plays them: every 10 ms it takes one block from each, and each
// player then writes one more. The second player plays A `lead` frames ahead of Apple's player; Apple's player plays
// A, then B (in a new stream) once the test says so, at the user's volume. The second player's stream began earlier
// and may have more waiting: what is heard is Apple's frame n with the second player's frame n + Apart().
struct Rig {
    MixDeck deck;
    std::vector<float> a;
    std::vector<float> b;
    std::vector<float> ghost;  // what the second player gives of A, when it is not A to the sample
    size_t lead = static_cast<size_t>(2.0 * kRate);
    double volume = 0.5;
    bool tailFlows = true;  // false: the second player has nothing to give, its blocks are silent
    bool onB = false;
    size_t at = 0;   // A's frame Apple's stream is at
    size_t bAt = 0;  // B's frame, once on B
    size_t aEnd = SIZE_MAX;  // Apple's player writes nothing of A from this frame on
    size_t tailEarly = 3 * kBlock;   // the second player's stream began this long before the test
    size_t mainQueued = kBlock;      // what each stream has waiting when a block is written
    size_t tailQueued = kBlock;
    size_t tailSong = 0;             // A's frame the second player gives next
    double now = 100;
    std::vector<float> sent;     // what Apple's player put in its stream
    std::vector<float> mainOut;  // what the deck left there
    std::vector<float> tailOut;  // what the deck put in the second player's stream
    std::vector<float> out;      // what is heard
    static constexpr std::uint64_t kTailStream = 1, kStreamA = 2, kStreamB = 3;

    explicit Rig(std::vector<float> songA, std::vector<float> songB = {}) : a(std::move(songA)), b(std::move(songB)) {
        deck.Open(kRate, 6.0);
    }

    double ATime() const { return static_cast<double>(at) / kRate; }
    std::ptrdiff_t Apart() const {
        return static_cast<std::ptrdiff_t>(tailEarly + mainQueued) - static_cast<std::ptrdiff_t>(tailQueued);
    }

    void TailBlock(size_t pass) {
        std::vector<float> block(kBlock * 2, 0.0f);
        const std::vector<float>& given = ghost.empty() ? a : ghost;
        const bool has = tailFlows && (tailSong + kBlock) * 2 <= given.size();
        if (has) {
            std::copy_n(given.begin() + static_cast<std::ptrdiff_t>(tailSong * 2), kBlock * 2, block.begin());
            tailSong += kBlock;
        }
        // written a little after Windows took its block, never at quite the same moment
        deck.ProcessTail(block.data(), kBlock, kRate, !has, {kTailStream, tailQueued, now + 0.0003 + (pass % 5) * 0.0004});
        tailOut.insert(tailOut.end(), block.begin(), block.end());
    }

    void Run(double seconds) {
        if (tailOut.empty()) {
            tailSong = lead - tailEarly;
            for (size_t n = 0; n < tailEarly / kBlock; n++) {
                now += 0.01;
                TailBlock(n);
            }
        }
        const size_t blocks = static_cast<size_t>(std::llround(seconds * kRate / kBlock));
        std::vector<float> block(kBlock * 2);
        for (size_t n = 0; n < blocks; n++) {
            now += 0.01;
            TailBlock(n);
            const std::vector<float>& song = onB ? b : a;
            const size_t from = onB ? bAt : at;
            for (size_t i = 0; i < kBlock * 2; i++) {
                const bool there = from * 2 + i < song.size() && (onB || from + i / 2 < aEnd);
                block[i] = there ? static_cast<float>(song[from * 2 + i] * volume) : 0.0f;
            }
            sent.insert(sent.end(), block.begin(), block.end());
            deck.ProcessMain(block.data(), kBlock, kRate,
                             {onB ? kStreamB : kStreamA, mainQueued, now + 0.0005 + (n % 3) * 0.001});
            mainOut.insert(mainOut.end(), block.begin(), block.end());
            at += kBlock;
            if (onB) bAt += kBlock;
        }
        const std::ptrdiff_t apart = Apart();
        for (size_t frame = out.size() / 2; frame < mainOut.size() / 2; frame++) {
            const std::ptrdiff_t tail = static_cast<std::ptrdiff_t>(frame) + apart;
            REQUIRE(tail >= 0 && static_cast<size_t>(tail) < tailOut.size() / 2);
            for (size_t c = 0; c < 2; c++) {
                out.push_back(mainOut[frame * 2 + c] + tailOut[static_cast<size_t>(tail) * 2 + c]);
            }
        }
    }

    // Apple's player moves on to B: its new stream gets its first block `silentBlocks` after the old one's last, and
    // begins with `leading` frames of nothing.
    void SwitchToB(size_t silentBlocks, size_t leading = 0) {
        onB = true;
        b.insert(b.begin(), leading * 2, 0.0f);
        for (size_t n = 0; n < silentBlocks; n++) {
            now += 0.01;
            TailBlock(n);
            sent.insert(sent.end(), kBlock * 2, 0.0f);
            mainOut.insert(mainOut.end(), kBlock * 2, 0.0f);
        }
    }

    std::optional<DeckStep> Align(double hintError = 0.1) {
        return deck.Align(static_cast<double>(lead) / kRate + hintError, ATime(), volume);
    }
};

double Rms(const std::vector<float>& samples, size_t fromFrame, size_t toFrame) {
    double sum = 0;
    for (size_t i = fromFrame * 2; i < toFrame * 2; i++) sum += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(sum / static_cast<double>((toFrame - fromFrame) * 2));
}

double Db(double ratio) { return 20 * std::log10(std::max(ratio, 1e-9)); }

// The loudest difference between what came out and `expected(frame, channel)` over a stretch of the output.
template <typename Expected>
double WorstError(const std::vector<float>& out, size_t fromFrame, size_t toFrame, Expected expected) {
    double worst = 0;
    for (size_t i = fromFrame; i < toFrame; i++) {
        for (size_t c = 0; c < 2; c++) worst = std::max(worst, std::abs(out[i * 2 + c] - expected(i, c)));
    }
    return worst;
}

// Upward zero crossings per second of the left channel: the pitch of a plain tone.
double Pitch(const std::vector<float>& out, size_t fromFrame, size_t toFrame) {
    int crossings = 0;
    for (size_t i = fromFrame + 1; i < toFrame; i++) {
        if (out[(i - 1) * 2] < 0 && out[i * 2] >= 0) crossings++;
    }
    return crossings * static_cast<double>(kRate) / static_cast<double>(toFrame - fromFrame);
}

// The crossover's amounts for a cutoff (mix_deck.cpp): A's low-pass from 20 kHz at 0 to 30 Hz at 1, B's high-pass
// from 2 Hz at 0 to 16 kHz at 1.
double MuffleFor(double hz) { return std::log(hz / 20000) / std::log(30.0 / 20000); }
double ThinFor(double hz) { return std::log(hz / 2) / std::log(16000.0 / 2); }

// The loudest step from one frame to the next in the left channel, from `fromFrame` on.
double WorstStep(const std::vector<float>& out, size_t fromFrame, size_t toFrame) {
    double worst = 0;
    for (size_t i = fromFrame; i < toFrame; i++) worst = std::max<double>(worst, std::abs(out[i * 2] - out[(i - 1) * 2]));
    return worst;
}

}  // namespace

TEST_CASE(AutoMixDeck_FindsApplesSamplesInTheSecondPlayersSound) {
    Rig rig(Song(12, 1));
    rig.Run(1.0);
    // The second player started two seconds ahead: what Apple's player plays now, it never played.
    CHECK(!rig.Align().has_value());
    CHECK(!rig.deck.Time().has_value());  // nothing found yet: Apple's stream is only listened to
    rig.Run(2.0);
    std::optional<DeckStep> step = rig.Align();
    REQUIRE(step.has_value());
    CHECK(step->match > 0.999);
    // the second player is exactly `lead` ahead of what the deck reads, one block of margin for what just came in
    CHECK_NEAR(step->lead, 2.0, 0.011);
    CHECK_NEAR(*rig.deck.Time(), rig.ATime(), 1e-9);
    rig.Run(1.0);
    CHECK_NEAR(*rig.deck.Time(), rig.ATime(), 1e-9);
    CHECK(rig.out == rig.sent);  // while Apple's player plays alone, not a sample is touched
    CHECK(rig.deck.ResidualDb() < -80);  // and the deck's copy is the same sound, at the same level
}

TEST_CASE(AutoMixDeck_PutInStepAgainForgetsWhatItMeasuredBefore) {
    Rig rig(Song(12, 1));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.Run(0.5);
    REQUIRE(rig.deck.ResidualDb() < -80);
    rig.tailSong += 3 * kBlock;  // the second player dropped 30 ms: the deck's copy is no longer Apple's sound
    rig.lead += 3 * kBlock;
    rig.Run(2.5);  // heard two seconds later, when the deck reaches what came after the hole
    REQUIRE(rig.deck.ResidualDb() > -12);
    REQUIRE(rig.Align().has_value());
    CHECK(std::isnan(rig.deck.ResidualDb()));  // unknown again, not the old measure
    rig.Run(0.5);
    CHECK(rig.deck.ResidualDb() < -80);
}

TEST_CASE(AutoMixDeck_ACopyNoLongerReadIsNoLongerKnownToBeApples) {
    // The second player stopped giving sound to the deck (20/11/2026: it moved to a stream of its own at another rate):
    // once what it gave is played, the copy is not known to be Apple's sound any more, whatever was measured before.
    Rig rig(Song(12, 1));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.Run(0.5);
    REQUIRE(rig.deck.ResidualDb() < -80);
    rig.tailFlows = false;
    rig.Run(2.5);
    CHECK(std::isnan(rig.deck.ResidualDb()));
}

TEST_CASE(AutoMixDeck_ASilentSongGivesNothingToFind) {
    Rig rig(std::vector<float>(static_cast<size_t>(12 * kRate) * 2, 0.0f));
    rig.Run(3.0);
    CHECK(!rig.Align().has_value());
    Rig other(Song(12, 1));
    other.Run(3.0);
    // the second player far from where the clocks say: not found rather than found wrong
    CHECK(!other.deck.Align(4.0, other.ATime(), other.volume).has_value());
}

TEST_CASE(AutoMixDeck_APlaceThatOnlyLooksLikeApplesSoundIsNotTaken) {
    // A beat loops every 0.29 s, with something new on each pass: one pass is much like the next (0.8 to 0.9 alike),
    // never the same sound. The second player began 1.97 s into the song, two seconds ahead: until Apple's player has
    // played a quarter of a second past that, Apple's sound is not in the second player's yet, but the next pass of
    // the loop is (20/11/2026, a real search took it, 0.29 s off and 1.5 dB under).
    Rig rig(LoopedSong(12, 0.29, 5));
    rig.Run(2.1);
    const std::optional<DeckStep> early = rig.Align(0.0);
    CHECK(!early.has_value() || std::abs(early->lead - 2.0) < 0.011);
    rig.Run(0.3);
    const std::optional<DeckStep> step = rig.Align(0.0);
    REQUIRE(step.has_value());
    CHECK_NEAR(step->lead, 2.0, 0.011);
    CHECK(step->match > 0.999);
}

TEST_CASE(AutoMixDeck_FindsApplesSoundHalfAFrameOffAtItsLevel) {
    // Each player resamples the song to the stream's rate from where it began playing: Apple's sound can lie between
    // two of the second player's frames. Bright music then matches at 0.92 frame for frame (0.87 seen in real songs,
    // 20/11/2026), for a copy in step to a fiftieth of a millisecond.
    Rig rig(Partials(8, 3));
    rig.ghost = Partials(8, 3, 0.5);
    rig.Run(3.0);
    const std::optional<DeckStep> step = rig.Align();
    REQUIRE(step.has_value());
    CHECK_NEAR(step->lead, 2.0, 0.011);
    CHECK(step->match > 0.99);
    CHECK_NEAR(step->scale, rig.volume, 0.005);  // not the level of a copy that only half matches
}

TEST_CASE(AutoMixDeck_ACopyHalfAFrameOffIsMeasuredAsApplesSound) {
    // Half a frame apart, two copies of bright music differ by 8 dB over the whole band, by far less under a few
    // kilohertz; a lost block or a wrong place differs by more than 0 dB in any band.
    Rig rig(Partials(8, 3));
    rig.ghost = Partials(8, 3, 0.5);
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.Run(1.0);
    CHECK(rig.deck.ResidualDb() < -15);
}

TEST_CASE(AutoMixDeck_TakingOverLeavesTheSoundUnchanged) {
    Rig rig(Song(12, 2));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.Run(0.5);
    const size_t from = rig.at;
    for (int step = 1; step <= 10; step++) {  // the session's crossfade: 100 ms in 10 ms steps
        rig.deck.SetGains({1 - step / 10.0, step / 10.0, 1, 1}, rig.volume);
        rig.Run(0.01);
    }
    rig.Run(1.0);  // the deck's copy alone
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, from, rig.at, apples) < 1e-5);
    // from then on Apple's stream is silent and the old song is the second player's stream alone
    CHECK(Rms(rig.mainOut, rig.at - kRate / 2, rig.at) == 0);
    CHECK(Rms(rig.tailOut, rig.at - kRate / 2, rig.at) > 0.01);
}

TEST_CASE(AutoMixDeck_StaysInStepWhateverTheStreamsHaveWaiting) {
    // The second player's stream began 70 ms before Apple's and keeps 30 ms waiting where Apple's keeps 10: the
    // same frame of the song must still leave the speakers at the same instant from both.
    // Apple's stream was playing long before the deck opened, and what it had waiting then was not a whole number of
    // blocks (48 frames more): the deck's count of it starts anywhere.
    Rig rig(Song(12, 2));
    rig.tailEarly = 7 * kBlock;
    rig.tailQueued = 3 * kBlock;
    rig.mainQueued = kBlock + 48;
    rig.Run(3.0);
    std::optional<DeckStep> step = rig.Align();
    REQUIRE(step.has_value());
    CHECK_NEAR(step->lead, 2.0 - 0.02 + 48.0 / kRate, 1e-9);  // 20 ms of its lead wait in its stream
    rig.Run(0.5);
    const size_t from = rig.at;
    for (int n = 1; n <= 10; n++) {
        rig.deck.SetGains({1 - n / 10.0, n / 10.0, 1, 1}, rig.volume);
        rig.Run(0.01);
    }
    rig.Run(1.0);
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, from, rig.at, apples) < 1e-5);
}

TEST_CASE(AutoMixDeck_AShortStopOfTheSecondPlayerIsNotHeard) {
    Rig rig(Song(12, 2));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, rig.volume);
    rig.Run(0.5);
    const size_t from = rig.at;
    rig.tailFlows = false;  // half a second with nothing to give: two seconds of its sound are waiting
    rig.Run(0.5);
    rig.tailFlows = true;
    rig.Run(2.0);
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, from, rig.at, apples) < 1e-5);
    CHECK_NEAR(*rig.deck.Time(), rig.ATime(), 1e-9);
}

TEST_CASE(AutoMixDeck_CountsTheSilenceApplesPlayerLeavesBetweenTwoSongs) {
    Rig rig(Song(8.0, 1), Song(8.0, 2));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    CHECK(!rig.deck.SwitchGap().has_value());  // still on the old song
    // The last 100 frames of the old stream are empty, then 60 ms pass, and the new stream starts with 130 empty ones.
    rig.aEnd = rig.at + kRate - 100;
    rig.Run(1.0);
    rig.SwitchToB(6, 130);
    rig.Run(0.2);
    CHECK(!rig.deck.SwitchGap().has_value());  // too little of the new stream seen to tell
    rig.Run(0.6);
    REQUIRE(rig.deck.SwitchGap().has_value());
    CHECK_NEAR(*rig.deck.SwitchGap(), 0.06 + 230.0 / kRate, 0.5 / kRate);
}

TEST_CASE(AutoMixDeck_ASongThatStartsSilentDoesNotTellTheGap) {
    Rig rig(Song(8.0, 1), Song(8.0, 2));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.Run(1.0);
    rig.SwitchToB(6, kRate / 2);  // half a second of nothing: the song's own silence, not the player's
    rig.Run(1.5);
    CHECK(!rig.deck.SwitchGap().has_value());
}

TEST_CASE(AutoMixDeck_MixesTheNextSongUnderTheOldOne) {
    Rig rig(Song(12, 3), Song(12, 4));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, rig.volume);
    rig.Run(0.5);
    rig.onB = true;  // Apple's player moved on; its stream now carries B
    rig.deck.SetGains({0.6, 0.8, 1, 1}, rig.volume);
    rig.Run(0.06);  // the gains get there within a few blocks
    const size_t from = rig.at;
    rig.Run(1.0);
    auto mixed = [&](size_t frame, size_t channel) {
        return 0.6 * rig.sent[frame * 2 + channel] + 0.8 * rig.volume * rig.a[frame * 2 + channel];
    };
    CHECK(WorstError(rig.out, from, rig.at, mixed) < 1e-5);
    // the user turns the volume down: Apple's stream follows at once, and so does the deck's copy
    rig.volume = 0.25;
    rig.deck.SetGains({0.6, 0.8, 1, 1}, rig.volume);
    rig.Run(0.02);
    const size_t quieter = rig.at;
    rig.Run(0.5);
    CHECK(WorstError(rig.out, quieter, rig.at, mixed) < 1e-5);
}

// The session moves the gains at each of its ticks, every 10 ms, and the deck reaches each one over 20 ms: a new one
// must start from where the last has got to, not wait behind it, or a second of moving gains (a fade, a filter
// sweep) leaves everything after it half a second late.
TEST_CASE(AutoMixDeck_KeepsUpWithGainsThatMoveFasterThanItsRamps) {
    Rig rig(Song(12, 3), Song(12, 4));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, rig.volume);
    rig.Run(0.5);
    for (int tick = 1; tick <= 100; tick++) {  // A from full to half over a second
        rig.deck.SetGains({0, 1 - 0.5 * tick / 100.0, 1, 1}, rig.volume);
        rig.Run(0.01);
    }
    const size_t half = rig.at;
    auto copy = [&](double gain) {
        return [&rig, gain](size_t frame, size_t channel) {
            return gain * rig.volume * rig.a[frame * 2 + channel];
        };
    };
    rig.Run(0.05);
    CHECK(WorstError(rig.out, half + kRate / 50, rig.at, copy(0.5)) < 1e-5);  // half way, as asked
    rig.deck.SetGains({0, 0, 1, 1}, rig.volume);  // then gone at once
    const size_t cut = rig.at;
    rig.Run(0.3);
    CHECK(Rms(rig.out, cut + kRate / 20, rig.at) < 1e-6);  // gone within the ramp, not half a second later
}

TEST_CASE(AutoMixDeck_PlaysTheGainsGivenAheadWhenNoMoreCome) {
    // The mix's thread can stall inside Apple's player (0.18 s on 20/11/2026): gains given for the next moments play
    // on meanwhile, A gone where they say and not once the thread is back.
    Rig rig(Song(12, 3), Song(12, 4));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, rig.volume);  // the deck's A alone
    rig.Run(0.5);
    const size_t from = rig.at;
    rig.deck.SetGainsAhead({{0.1, {0, 1, 1, 1}}, {0.12, {0, 0, 1, 1}}, {0.3, {0, 0, 1, 1}}}, rig.volume);
    rig.Run(0.3);  // and nothing more
    auto copy = [&rig](size_t frame, size_t channel) { return rig.volume * rig.a[frame * 2 + channel]; };
    CHECK(WorstError(rig.out, from, from + kRate / 10 - kBlock, copy) < 1e-5);  // whole up to 0.1 s
    CHECK(Rms(rig.out, from + kRate * 12 / 100 + kBlock, rig.at) < 1e-6);         // gone from 0.12 s
}

TEST_CASE(AutoMixDeck_TakesTheBassOfOneSongOnly) {
    // A is a bass note, B a higher one: with A's lows gone, B is all that is left.
    std::vector<float> a = Song(12, 5);
    const std::vector<float> bass = Tone(12, 60, 0.5);
    for (size_t i = 0; i < a.size(); i++) a[i] = a[i] * 0.02f + bass[i];
    Rig rig(a, Tone(12, 2000, 0.5));
    rig.volume = 1;
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, 1);
    rig.Run(0.5);
    const double before = Rms(rig.out, rig.at - kRate / 4, rig.at);
    rig.deck.SetGains({0, 1, 1, 0}, 1);
    rig.Run(1.0);
    const double without = Rms(rig.out, rig.at - kRate / 4, rig.at);
    CHECK(Db(without / before) < -24);  // 60 Hz, a good octave under the shelf: nearly all of the 30 dB
    rig.onB = true;
    rig.deck.SetGains({1, 0, 0, 1}, 1);  // B without its lows: a 2 kHz note does not care
    rig.Run(1.0);
    const double high = Rms(rig.out, rig.at - kRate / 4, rig.at);
    CHECK(std::abs(Db(high / (0.5 / std::sqrt(2.0)))) < 0.5);
    rig.deck.SetGains({1, 0, 1, 1}, 1);  // and with the shelf gone it is Apple's stream again, to the sample
    rig.Run(0.5);
    const size_t from = rig.at;
    rig.Run(0.5);
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, from, rig.at, apples) == 0);
}

TEST_CASE(AutoMixDeck_ThinsTheOldSongOutAndMufflesTheNewOne) {
    // A is a low note, B a high one: A's high-pass at its top and B's low-pass at its bottom leave next to nothing.
    Rig rig(Tone(12, 100, 0.5), Tone(12, 5000, 0.5));
    rig.volume = 1;
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, 1);
    rig.Run(0.5);
    const double low = Rms(rig.out, rig.at - kRate / 4, rig.at);
    rig.deck.SetGains({0, 1, 1, 1, 0, 1}, 1);
    rig.Run(1.0);
    CHECK(Db(Rms(rig.out, rig.at - kRate / 4, rig.at) / low) < -40);  // 100 Hz under a 2 kHz high-pass
    rig.onB = true;
    rig.deck.SetGains({1, 0, 1, 1, 0, 0}, 1);
    rig.Run(0.5);
    const double high = Rms(rig.out, rig.at - kRate / 4, rig.at);
    CHECK(std::abs(Db(high / (0.5 / std::sqrt(2.0)))) < 0.5);
    rig.deck.SetGains({1, 0, 1, 1, 1, 0}, 1);
    rig.Run(1.0);
    CHECK(Db(Rms(rig.out, rig.at - kRate / 4, rig.at) / high) < -35);  // 5 kHz over a 400 Hz low-pass
    rig.deck.SetGains({1, 0, 1, 1, 0, 0}, 1);  // and open again, it is Apple's stream to the sample
    rig.Run(0.5);
    const size_t from = rig.at;
    rig.Run(0.5);
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, from, rig.at, apples) == 0);
}

TEST_CASE(AutoMixDeck_ASweepComesInAndGoesOutWithoutAClick) {
    // Even nearly open, a filter shifts a low note: switched in or out at once, the wave would jump.
    Rig rig(Tone(12, 100, 0.5));
    rig.volume = 1;
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, 1);
    rig.Run(0.5);
    const size_t from = rig.at;
    for (int n = 1; n <= 50; n++) {  // the session's pace: a new amount every 10 ms, up to a quarter and back to 0
        const double amount = 0.25 * (n <= 25 ? n : 50 - n) / 25.0;
        rig.deck.SetGains({0, 1, 1, 1, 0, amount}, 1);
        rig.Run(0.01);
    }
    rig.Run(0.5);
    // from the step into the first frame the filter may touch: the note's own steepest step, and a little
    CHECK(WorstStep(rig.out, from, rig.at) < 1.2 * 0.5 * 2 * std::sin(kPi * 100 / kRate));
    const size_t open = rig.at;
    rig.Run(0.5);
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, open, rig.at, apples) < 1e-5);
}

TEST_CASE(AutoMixDeck_TheCrossoverHandsEachSongsSoundOverAtOneCutoff) {
    // A and B are the same 1 kHz note. With A's low-pass and B's high-pass at 1 kHz each lets half its power through,
    // so that the two add up to the power of either: the crossover hands one song to the other without a dip or a
    // bump. Shut, each takes its song away; open again, B is Apple's stream to the sample.
    Rig rig(Tone(12, 1000, 0.5), Tone(12, 1000, 0.5));
    rig.volume = 1;
    const double muffleAt1k = MuffleFor(1000);
    const double thinAt1k = ThinFor(1000);
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, 1);
    rig.Run(0.5);
    const double a = Rms(rig.out, rig.at - kRate / 4, rig.at);
    rig.deck.SetGains({0, 1, 1, 1, 0, 0, 0, 0, 0, muffleAt1k}, 1);
    rig.Run(1.0);
    CHECK(std::abs(Db(Rms(rig.out, rig.at - kRate / 4, rig.at) / a) + 3.01) < 0.3);
    rig.deck.SetGains({0, 1, 1, 1, 0, 0, 0, 0, 0, 1}, 1);
    rig.Run(1.0);
    CHECK(Db(Rms(rig.out, rig.at - kRate / 4, rig.at) / a) < -60);  // 1 kHz over a 30 Hz low-pass
    rig.onB = true;
    rig.deck.SetGains({1, 0, 1, 1}, 1);
    rig.Run(0.5);
    const double b = Rms(rig.out, rig.at - kRate / 4, rig.at);
    rig.deck.SetGains({1, 0, 1, 1, 0, 0, 0, 0, thinAt1k, 0}, 1);
    rig.Run(1.0);
    CHECK(std::abs(Db(Rms(rig.out, rig.at - kRate / 4, rig.at) / b) + 3.01) < 0.3);
    rig.deck.SetGains({1, 0, 1, 1, 0, 0, 0, 0, 1, 0}, 1);
    rig.Run(1.0);
    CHECK(Db(Rms(rig.out, rig.at - kRate / 4, rig.at) / b) < -60);  // 1 kHz under a high-pass near the top
    rig.deck.SetGains({1, 0, 1, 1}, 1);
    rig.Run(0.5);
    const size_t from = rig.at;
    rig.Run(0.5);
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, from, rig.at, apples) == 0);
}

TEST_CASE(AutoMixDeck_TheCrossoverFallsFourPolesSteepAndStillAddsUp) {
    // An octave past its cutoff a four-pole Butterworth lets 1/257 of the power through (two poles: 1/17), and at any
    // frequency A's low-pass and B's high-pass at one cutoff still add up to all of it.
    Rig rig(Tone(12, 2000, 0.5), Tone(12, 2000, 0.5));
    rig.volume = 1;
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, 1);
    rig.Run(0.5);
    const double wholeA = Rms(rig.out, rig.at - kRate / 4, rig.at);
    rig.deck.SetGains({0, 1, 1, 1, 0, 0, 0, 0, 0, MuffleFor(1000)}, 1);
    rig.Run(1.0);
    const double a = Rms(rig.out, rig.at - kRate / 4, rig.at) / wholeA;
    const double octave = std::tan(kPi * 2000 / kRate) / std::tan(kPi * 1000 / kRate);  // as the filter warps it
    CHECK(std::abs(Db(a) - 10 * std::log10(1 / (1 + std::pow(octave, 8)))) < 0.5);
    rig.onB = true;
    rig.deck.SetGains({1, 0, 1, 1}, 1);
    rig.Run(0.5);
    const double wholeB = Rms(rig.out, rig.at - kRate / 4, rig.at);
    rig.deck.SetGains({1, 0, 1, 1, 0, 0, 0, 0, ThinFor(1000), 0}, 1);
    rig.Run(1.0);
    const double b = Rms(rig.out, rig.at - kRate / 4, rig.at) / wholeB;
    CHECK(std::abs(a * a + b * b - 1) < 0.01);
}

TEST_CASE(AutoMixDeck_TheCrossoverGivesTheNextSongItsBassBackWithoutAClick) {
    // As the crossover ends B's high-pass comes down out of the bass and lets go of B, Apple's stream from then on. A
    // filter still shifting a low note when it lets go makes the wave jump: open, it must leave the note as it is.
    Rig rig(Tone(12, 40, 0.5), Tone(12, 40, 0.5));
    rig.volume = 1;
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.onB = true;
    rig.deck.SetGains({1, 0, 1, 1, 0, 0, 0, 0, ThinFor(150), 0}, 1);
    rig.Run(0.5);
    for (int n = 1; n < 30; n++) {  // the crossover's last 0.3 s at the session's pace, a new amount every 10 ms
        rig.deck.SetGains({1, 0, 1, 1, 0, 0, 0, 0, ThinFor(150) * (1 - n / 30.0), 0}, 1);
        rig.Run(0.01);
    }
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, rig.at - kRate / 100, rig.at, apples) < 0.1);  // within a fifth of the note, still filtered
    rig.deck.SetGains({1, 0, 1, 1}, 1);
    rig.Run(0.5);
    const size_t open = rig.at;
    rig.Run(0.5);
    CHECK(WorstError(rig.out, open, rig.at, apples) == 0);
}

TEST_CASE(AutoMixDeck_AnEchoRepeatsWhatWasThrownEachRepeatQuieter) {
    Rig rig(Song(12, 8));
    rig.volume = 1;
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetEcho(0.5, 0.5);
    rig.deck.SetGains({0, 1, 1, 1, 0, 0, 0, 1}, 1);  // the deck's copy alone, the echo heard but fed nothing
    rig.Run(0.5);
    const double level = Rms(rig.out, rig.at - kRate / 4, rig.at);
    const size_t thrown = rig.at;
    rig.deck.SetGains({0, 1, 1, 1, 0, 0, 1, 1}, 1);  // a quarter of a second of A thrown into it
    rig.Run(0.25);
    rig.deck.SetGains({0, 0, 1, 1, 0, 0, 0, 1}, 1);  // and A gone
    rig.Run(2.0);
    auto window = [&](double from, double to) {
        return Rms(rig.out, thrown + static_cast<size_t>(from * kRate), thrown + static_cast<size_t>(to * kRate));
    };
    const double first = window(0.53, 0.74), second = window(1.03, 1.24), third = window(1.53, 1.74);
    CHECK(Db(first / level) > -14);  // 3 dB under A, and this song's lows (most of it) thinned out
    CHECK(Db(first / level) < -3);
    CHECK(Db(second / first) < -6);  // half, and thinner again
    CHECK(Db(third / second) < -6);
    CHECK(Db(second / first) > -12);
    CHECK(window(0.3, 0.48) == 0);  // nothing between A and its first repeat
    CHECK(Db(window(0.8, 0.98) / level) < -80);  // nor between repeats
    rig.deck.SetGains({0, 0, 1, 1, 0, 0, 0, 0}, 1);  // the echo's return closed: silence, though it still rings
    rig.Run(0.25);
    CHECK(Rms(rig.out, rig.at - kRate / 8, rig.at) == 0);
}

// A plan may bring its echo back louder, to be heard over the next song's drop: the repeats follow its level.
TEST_CASE(AutoMixDeck_AnEchoComesBackAtThePlansLevel) {
    auto repeats = [](double level) {
        Rig rig(Song(12, 8));
        rig.volume = 1;
        rig.Run(3.0);
        REQUIRE(rig.Align().has_value());
        rig.deck.SetEcho(0.5, 0.5, level);
        rig.deck.SetGains({0, 1, 1, 1, 0, 0, 1, 1}, 1);
        const size_t thrown = rig.at;
        rig.Run(0.25);
        rig.deck.SetGains({0, 0, 1, 1, 0, 0, 0, 1}, 1);
        rig.Run(1.5);
        auto window = [&](double from, double to) {
            return Rms(rig.out, thrown + static_cast<size_t>(from * kRate), thrown + static_cast<size_t>(to * kRate));
        };
        return std::array<double, 2>{window(0.53, 0.74), window(1.03, 1.24)};
    };
    const std::array<double, 2> plain = repeats(kEchoLevel), loud = repeats(2 * kEchoLevel);
    CHECK_NEAR(Db(loud[0] / plain[0]), 6.02, 0.1);
    CHECK_NEAR(Db(loud[1] / plain[1]), 6.02, 0.1);  // the level is the return's: the loop rings as before
}

TEST_CASE(AutoMixDeck_ALoopPlaysTheSameBarOverAndOverWithoutAClick) {
    // A note of 101 Hz turns half a cycle in the half second looped, and the loop starts on its crest: joined end to
    // start as they are, the wave would jump from trough to crest at each turn.
    std::vector<float> a = Song(12, 9);
    const std::vector<float> note = Tone(12, 101, 0.5);
    for (size_t i = 0; i < a.size(); i++) a[i] = a[i] * 0.02f + note[i];
    Rig rig(a);
    rig.volume = 1;
    const double from = 5 + 0.25 / 101;
    rig.deck.SetLoop(from, 0.5);  // as the session does: before the deck finds Apple's sound
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    CHECK(!rig.deck.LoopHeld());
    rig.deck.SetGains({0, 1, 1, 1}, 1);
    rig.Run(1.0);  // the second player, two seconds ahead, is through the loop
    CHECK(rig.deck.LoopHeld());
    rig.tailFlows = false;  // and is paused: the deck needs nothing more of it
    rig.Run(3.5);
    CHECK_NEAR(*rig.deck.Time(), 7.5, 0.02);  // A's time runs on through five turns of the loop
    const size_t start = static_cast<size_t>(std::llround(from * kRate)), length = kRate / 2, seam = kRate / 50;
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, 4 * kRate, start + length - seam, apples) < 1e-5);  // A as it is up to the seam
    auto firstTurn = [&](size_t frame, size_t channel) {
        return static_cast<double>(rig.out[(start + (frame - start) % length) * 2 + channel]);
    };
    REQUIRE(rig.out.size() >= (start + 4 * length) * 2);
    CHECK(WorstError(rig.out, start + length, start + 4 * length, firstTurn) == 0);  // then the same turn again
    // The sound steps no more than A's own does into each turn, and hardly more across the seam that leads there.
    auto steepest = [&](size_t from, size_t to) {
        double worst = 0;
        for (size_t i = from; i < to; i++) {
            for (size_t c = 0; c < 2; c++) worst = std::max<double>(worst, std::abs(rig.out[i * 2 + c] - rig.out[(i - 1) * 2 + c]));
        }
        return worst;
    };
    const double own = steepest(4 * kRate + 1, start + length - seam);
    for (size_t k = 1; k <= 3; k++) {
        const size_t turn = start + k * length;
        CHECK(steepest(turn, turn + 1) <= own);
        CHECK(steepest(turn - seam, turn) < 1.3 * own);
    }
}

TEST_CASE(AutoMixDeck_ARolledLoopPlaysEachRollsFirstFramesOverAndOverWithoutAClick) {
    std::vector<float> a = Song(12, 9);
    const std::vector<float> note = Tone(12, 101, 0.5);
    for (size_t i = 0; i < a.size(); i++) a[i] = a[i] * 0.02f + note[i];
    Rig rig(a);
    rig.volume = 1;
    const double from = 5 + 0.25 / 101;
    // The half-second bar twice, then its first quarter of a second twice, then its first eighth four times.
    rig.deck.SetLoop(from, 0.5, {{from + 1.0, 0.25}, {from + 1.5, 0.125}});
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, 1);
    rig.Run(1.0);
    REQUIRE(rig.deck.LoopHeld());
    rig.tailFlows = false;
    rig.Run(3.0);
    CHECK_NEAR(*rig.deck.Time(), 7.0, 0.02);  // A's time runs on through the rolls
    const size_t start = static_cast<size_t>(std::llround(from * kRate)), seam = kRate / 50;
    const size_t half = kRate / 4, eighth = kRate / 8;
    auto turnOf = [&](size_t rollStart, size_t length) {
        return [&rig, start, rollStart, length](size_t frame, size_t channel) {
            return static_cast<double>(rig.out[(start + (frame - rollStart) % length) * 2 + channel]);
        };
    };
    // Within each turn, up to its seam, the roll's first frames as the loop's first turn played them.
    for (size_t k = 0; k < 2; k++) {
        const size_t turn = start + kRate + k * half;
        CHECK(WorstError(rig.out, turn, turn + half - seam, turnOf(start + kRate, half)) == 0);
    }
    for (size_t k = 0; k < 4; k++) {
        const size_t turn = start + kRate * 3 / 2 + k * eighth;
        CHECK(WorstError(rig.out, turn, turn + eighth - seam, turnOf(start + kRate * 3 / 2, eighth)) == 0);
    }
    // No step into a turn, nor across the seam that leads there, louder than A's own.
    const double own = WorstStep(rig.out, 4 * kRate + 1, start + kRate / 2 - seam);
    for (size_t turn = start + kRate; turn < start + 2 * kRate; turn += eighth) {
        CHECK(WorstStep(rig.out, turn - seam, turn + 1) < 1.3 * own);
    }
}

// B comes in as stabs on its beats while A plays on, dipping under each; past the gate's end B plays whole.
TEST_CASE(AutoMixDeck_TheGateLetsTheNextSongInOnItsStabsOnly) {
    Rig rig(Song(12, 3), Song(12, 4));
    rig.volume = 1;
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, 1);
    rig.Run(0.5);
    rig.onB = true;
    // A at 3.5 s: stabs of 0.2 s every half second from 4, A a half under each, until 6.
    Gate gate{{4.0, 4.5, 5.0, 5.5}, 0.2, 0.5, 6.0};
    rig.deck.SetGate(gate);
    rig.deck.SetGains({1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1}, 1);
    rig.Run(2.5);  // A at 6.0
    rig.deck.SetGains({1, 1, 1, 1}, 1);  // the gate down: B whole
    rig.Run(0.5);
    auto at = [](double aTime) { return static_cast<size_t>(std::llround(aTime * kRate)); };
    auto mixed = [&](double b, double a) {
        return [&rig, b, a](size_t frame, size_t channel) {
            return b * rig.sent[frame * 2 + channel] + a * rig.a[frame * 2 + channel];
        };
    };
    for (double beat : gate.beats) {
        // Within a stab, B whole and A at half; between stabs, A alone and whole.
        CHECK(WorstError(rig.out, at(beat + 0.001), at(beat + 0.199), mixed(1, 0.5)) < 1e-4);
        CHECK(WorstError(rig.out, at(beat + 0.215), at(beat + 0.495), mixed(0, 1)) < 1e-4);
    }
    CHECK(WorstError(rig.out, at(3.8), at(3.99), mixed(0, 1)) < 1e-4);  // before its first stab, B held out
    CHECK(WorstError(rig.out, at(6.05), at(6.45), mixed(1, 1)) < 1e-4);  // and whole past the gate's end
    // Each stab opens and closes without a click: no step louder than the songs' own.
    const double own = WorstStep(rig.out, at(6.05), at(6.45));
    for (double beat : gate.beats) {
        CHECK(WorstStep(rig.out, at(beat - 0.01), at(beat + 0.01)) < 2 * own);
        CHECK(WorstStep(rig.out, at(beat + 0.19), at(beat + 0.22)) < 2 * own);
    }
}

TEST_CASE(AutoMixDeck_StandsStillWhenTheSecondPlayerStops) {
    Rig rig(Song(12, 6));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0, 1, 1, 1}, rig.volume);
    rig.tailFlows = false;  // two seconds of its sound are waiting, then nothing
    rig.Run(3.0);
    const double stopped = *rig.deck.Time();
    CHECK_NEAR(stopped, 3.0 + 2.0, 0.02);
    rig.Run(0.5);
    CHECK(*rig.deck.Time() == stopped);
    CHECK(Rms(rig.out, rig.at - kRate / 4, rig.at) == 0);  // silence, not a loop of old sound
}

TEST_CASE(AutoMixDeck_ClosedItLeavesApplesStreamAlone) {
    Rig rig(Song(12, 7));
    rig.Run(3.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetGains({0.5, 0.5, 1, 1}, rig.volume);
    rig.Run(0.2);
    rig.deck.Close();
    const size_t from = rig.at;
    rig.Run(0.5);
    CHECK(!rig.deck.Time().has_value());
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, from, rig.at, apples) == 0);
}

TEST_CASE(AutoMixStretch_AtNormalSpeedItGivesTheSoundBackLate) {
    TempoStretch stretch(kRate);
    const std::vector<float> song = Song(4, 8);
    const size_t frames = song.size() / 2;
    std::vector<float> out(song.size());
    for (size_t at = 0; at + kBlock <= frames; at += kBlock) {
        stretch.Process(&song[at * 2], kBlock, &out[at * 2], kBlock);
    }
    // what comes out is what went in Latency() frames earlier: the deck counts on it to swap the two copies
    const size_t latency = static_cast<size_t>(stretch.Latency());
    CHECK(latency > 0);
    double error = 0, power = 0;
    for (size_t i = static_cast<size_t>(2 * kRate); i < static_cast<size_t>(3 * kRate); i++) {
        const double in = song[(i - latency) * 2];
        error += (out[i * 2] - in) * (out[i * 2] - in);
        power += in * in;
    }
    CHECK(Db(std::sqrt(error / power)) < -20);
}

TEST_CASE(AutoMixDeck_ANudgeSetsTheStretchedOldSongBackAtTheSamePitch) {
    // Apple's player now and then starts the next song a pass or three late (+30 ms on a beat mix, 20/11/2026), as
    // only shows once it plays: A, stretched anyway, is then set back by as much over a second, at the same pitch.
    for (const double nudge : {0.03, -0.02}) {
        Rig rig(Tone(30, 440, 0.5));
        rig.lead = static_cast<size_t>(3.0 * kRate);
        const std::vector<float> rustle = Song(30, 9);
        for (size_t i = 0; i < rig.a.size(); i++) rig.a[i] += rustle[i] * 0.05f;
        MixPlan plan;
        plan.keys = {{14, 1, 0}, {24, 0, 1}};
        plan.rate = 1.05;
        plan.rateFrom = 8;
        rig.Run(4.0);
        REQUIRE(rig.Align().has_value());
        CHECK(!rig.deck.Nudge(nudge));  // not while A plays as recorded: it would jump
        rig.deck.SetStretch(plan.rateFrom, plan.keys.front().at, plan.rate);
        rig.deck.SetGains({0, 1, 1, 1}, rig.volume);
        rig.Run(SwitchTime(plan) - rig.ATime());
        REQUIRE(rig.deck.Nudge(nudge));
        const size_t from = rig.at;
        rig.Run(4.0);
        CHECK_NEAR(*rig.deck.Time(), 18.2 - nudge, 0.012);  // 4 s at 1.05, less the nudge
        CHECK_NEAR(Pitch(rig.out, from, rig.at), 440, 2);
    }
}

TEST_CASE(AutoMixDeck_ANudgeTheStretchCannotGiveIsRefused) {
    // A nudge takes frames from what the stretch reads over a second: past a quarter of a second, it would read
    // backwards. Refused, the stretched A plays on as planned.
    Rig rig(Tone(30, 440, 0.5));
    rig.lead = static_cast<size_t>(3.0 * kRate);
    MixPlan plan;
    plan.keys = {{14, 1, 0}, {24, 0, 1}};
    plan.rate = 1.05;
    plan.rateFrom = 8;
    rig.Run(4.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetStretch(plan.rateFrom, plan.keys.front().at, plan.rate);
    rig.deck.SetGains({0, 1, 1, 1}, rig.volume);
    rig.Run(SwitchTime(plan) - rig.ATime());
    CHECK(!rig.deck.Nudge(2.0));
    CHECK(!rig.deck.Nudge(-0.3));
    REQUIRE(rig.deck.Nudge(0.2));
    CHECK(!rig.deck.Nudge(0.1));  // with the one under way, more than a quarter of a second
    rig.Run(4.0);
    CHECK_NEAR(*rig.deck.Time(), 18.2 - 0.2, 0.012);
}

TEST_CASE(AutoMixDeck_PlaysTheOldSongFasterAtTheSamePitch) {
    // A is one note: played faster by resampling it would rise by 5 %, a good semitone at this size.
    Rig rig(Tone(30, 440, 0.5));
    rig.lead = static_cast<size_t>(3.0 * kRate);
    // The note alone looks the same everywhere, so a rustle is laid under it for the deck to recognise it by.
    const std::vector<float> rustle = Song(30, 9);
    for (size_t i = 0; i < rig.a.size(); i++) rig.a[i] += rustle[i] * 0.05f;
    MixPlan plan;
    plan.keys = {{14, 1, 0}, {24, 0, 1}};
    plan.rate = 1.05;
    plan.rateFrom = 8;
    rig.Run(4.0);
    REQUIRE(rig.Align().has_value());
    rig.deck.SetStretch(plan.rateFrom, plan.keys.front().at, plan.rate);
    rig.deck.SetGains({0, 1, 1, 1}, rig.volume);
    // Apple's player, at normal speed, reaches the switch point when the deck's faster copy reaches the first key.
    rig.Run(SwitchTime(plan) - rig.ATime());
    CHECK_NEAR(*rig.deck.Time(), 14.0, 0.012);
    // Until its speed moved, the deck's copy was Apple's, to the sample, stretch or not...
    auto apples = [&](size_t frame, size_t channel) { return static_cast<double>(rig.sent[frame * 2 + channel]); };
    CHECK(WorstError(rig.out, static_cast<size_t>(4.2 * kRate), static_cast<size_t>(7.4 * kRate), apples) < 1e-5);
    // ...and the stretched copy took over without a hole: the level holds through the swap and the ramp.
    const double level = Rms(rig.sent, kRate, 2 * kRate);
    for (double t = 7.5; t < 13.5; t += 0.1) {
        const size_t from = static_cast<size_t>(t * kRate);
        CHECK(std::abs(Db(Rms(rig.out, from, from + kRate / 10) / level)) < 1.0);
    }
    const size_t switched = rig.at;
    rig.Run(4.0);
    // 4 s at 1.05: 4.2 s of the song, and the note has not moved
    CHECK_NEAR(*rig.deck.Time(), 18.2, 0.012);
    CHECK_NEAR(Pitch(rig.out, switched + kRate / 2, rig.at), 440, 2);
}
