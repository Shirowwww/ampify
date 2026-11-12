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
        const bool has = tailFlows && (tailSong + kBlock) * 2 <= a.size();
        if (has) {
            std::copy_n(a.begin() + static_cast<std::ptrdiff_t>(tailSong * 2), kBlock * 2, block.begin());
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

TEST_CASE(AutoMixDeck_ASilentSongGivesNothingToFind) {
    Rig rig(std::vector<float>(static_cast<size_t>(12 * kRate) * 2, 0.0f));
    rig.Run(3.0);
    CHECK(!rig.Align().has_value());
    Rig other(Song(12, 1));
    other.Run(3.0);
    // the second player far from where the clocks say: not found rather than found wrong
    CHECK(!other.deck.Align(4.0, other.ATime(), other.volume).has_value());
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
