#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <vector>

#include "../src/core/sing/light_filter.hpp"
#include "../src/core/sing/pace_watch.hpp"
#include "../src/core/sing/real_fft.hpp"
#include "../src/core/sing/resampler.hpp"
#include "../src/core/sing/studio_stream.hpp"
#include "../src/core/sing/vocal_splitter.hpp"
#include "../src/core/sing_pack.hpp"
#include "support.hpp"
#include "test.hpp"

using namespace ampify::sing;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kHalfOut = 1 - kCompensate * 0.5;  // what stays of a song whose vocals are half of it, at voice 0

// The vocals of every song are half of it.
struct HalfModel : VocalModel {
    explicit HalfModel(const ModelSpec& modelSpec = kStandard) : VocalModel(modelSpec) {}
    bool Run(const float* spectrum, float* vocals) override {
        for (size_t i = 0; i < spec.SpectrumSize(); i++) vocals[i] = 0.5f * spectrum[i];
        return true;
    }
};

struct FailingModel : VocalModel {
    bool Run(const float*, float*) override { return false; }
};

struct CapturingModel : VocalModel {
    std::vector<float> seen = std::vector<float>(kSpectrumSize);
    bool Run(const float* spectrum, float* vocals) override {
        std::copy(spectrum, spectrum + kSpectrumSize, seen.begin());
        std::copy(spectrum, spectrum + kSpectrumSize, vocals);
        return true;
    }
};

// Noise from an LCG, left then right for each sample, that a numpy version of the same STFT was given.
void ReferenceNoise(std::vector<float>& left, std::vector<float>& right) {
    left.resize(kChunk);
    right.resize(kChunk);
    std::uint32_t state = 12345;
    for (size_t i = 0; i < kChunk; i++) {
        for (std::vector<float>* channel : {&left, &right}) {
            state = state * 1664525u + 1013904223u;
            (*channel)[i] = static_cast<float>(((state >> 8) / 8388608.0 - 1.0) * 0.5);
        }
    }
}

// Forty sines of random pitch between 200 Hz and 15 kHz: noise that stays inside the model's bins.
std::vector<float> BandNoise(unsigned seed) {
    std::vector<float> out(kChunk, 0.0f);
    std::uint32_t state = seed;
    auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) / 16777216.0;
    };
    for (int tone = 0; tone < 40; tone++) {
        const double hz = 200 + next() * 14800, phase = next() * 2 * kPi;
        for (size_t i = 0; i < kChunk; i++) {
            out[i] += static_cast<float>(0.02 * std::sin(2 * kPi * hz * static_cast<double>(i) / kModelRate + phase));
        }
    }
    return out;
}

std::vector<float> Sine(int rate, double seconds, double hz, double left, double right) {
    const size_t frames = static_cast<size_t>(seconds * rate);
    std::vector<float> out(frames * 2);
    for (size_t i = 0; i < frames; i++) {
        const double value = std::sin(2 * kPi * hz * static_cast<double>(i) / rate);
        out[i * 2] = static_cast<float>(left * value);
        out[i * 2 + 1] = static_cast<float>(right * value);
    }
    return out;
}

// Something like a song, different on each side, all inside the band the model hears.
std::vector<float> Song(int rate, double seconds) {
    const size_t frames = static_cast<size_t>(seconds * rate);
    std::vector<float> out(frames * 2);
    for (size_t i = 0; i < frames; i++) {
        const double t = static_cast<double>(i) / rate;
        out[i * 2] = static_cast<float>(0.3 * std::sin(2 * kPi * 440 * t) + 0.1 * std::sin(2 * kPi * 3000 * t));
        out[i * 2 + 1] =
            static_cast<float>(0.25 * std::sin(2 * kPi * 660 * t + 1) + 0.1 * std::sin(2 * kPi * 2000 * t));
    }
    return out;
}

// Plays song frames [from, to) through the stream in 10 ms blocks, the worker running between blocks if `work`.
std::vector<float> Play(StudioStream& stream, VocalSplitter& splitter, const std::vector<float>& song, size_t from,
                        size_t to, int rate, double voice, bool work = true, double music = 1.0) {
    const size_t block = static_cast<size_t>(rate / 100);
    std::vector<float> out(song.begin() + static_cast<std::ptrdiff_t>(from * 2),
                           song.begin() + static_cast<std::ptrdiff_t>(to * 2));
    for (size_t at = 0; at < to - from; at += block) {
        stream.Process(out.data() + at * 2, std::min(block, to - from - at), voice, false, music);
        while (work && stream.Work(splitter)) {
        }
    }
    return out;
}

// The largest gap between out[n] (out starts at song frame `from`) and `scale` times the song `delay` frames back.
double WorstGap(const std::vector<float>& out, const std::vector<float>& song, size_t from, size_t delay, size_t first,
                size_t last, double scale) {
    double worst = 0;
    for (size_t n = first; n < last; n++) {
        for (size_t channel = 0; channel < 2; channel++) {
            const double expected = scale * song[(from + n - delay) * 2 + channel];
            worst = std::max(worst, std::fabs(out[n * 2 + channel] - expected));
        }
    }
    return worst;
}

double Rms(const std::vector<float>& stereo, size_t channel, size_t first, size_t last) {
    double sum = 0;
    for (size_t i = first; i < last; i++) sum += double(stereo[i * 2 + channel]) * stereo[i * 2 + channel];
    return std::sqrt(sum / static_cast<double>(last - first));
}

double Db(double ratio) { return 20 * std::log10(ratio); }

size_t Frames(StudioStream& stream, int rate) { return static_cast<size_t>(std::llround(stream.Delay() * rate)); }

// Sends `in` through `first` then `second` in 10 ms blocks at the first one's input rate.
std::vector<float> Through(Resampler& first, Resampler& second, const std::vector<float>& in, size_t block) {
    std::vector<float> middle(first.MaxOutput(block) * 2), out;
    std::vector<float> last(second.MaxOutput(first.MaxOutput(block)) * 2);
    for (size_t at = 0; at < in.size() / 2; at += block) {
        const size_t count = std::min(block, in.size() / 2 - at);
        const size_t made = first.Process(in.data() + at * 2, count, middle.data());
        const size_t back = second.Process(middle.data(), made, last.data());
        out.insert(out.end(), last.begin(), last.begin() + static_cast<std::ptrdiff_t>(back * 2));
    }
    return out;
}

// Error of a round trip against the input `latency` frames back, in dB under the input.
double RoundTripErrorDb(int rate) {
    Resampler down(rate, kModelRate), up(kModelRate, rate);
    const std::vector<float> in = Sine(rate, 1.0, 1000, 0.5, 0.5);
    const std::vector<float> out = Through(down, up, in, static_cast<size_t>(rate / 100));
    const size_t latency = static_cast<size_t>(down.Latency() + up.OutputLatency());
    double error = 0, power = 0;
    for (size_t n = latency + 2000; n < out.size() / 2 - 2000; n++) {
        for (size_t channel = 0; channel < 2; channel++) {
            const double reference = in[(n - latency) * 2 + channel];
            error += (out[n * 2 + channel] - reference) * (out[n * 2 + channel] - reference);
            power += reference * reference;
        }
    }
    return 10 * std::log10(error / power);
}

}  // namespace

// ---- the model's STFT

// The transform against a direct DFT, at the bins its two stages treat apart (the edges, the middle, the Nyquist
// bin carried in bin 0), then back to the samples.
TEST_CASE(Sing_RealFftMatchesADirectDftAndComesBack) {
    constexpr size_t n = RealFft7680::kSize;
    std::vector<float> x(n), back(n);
    for (size_t i = 0; i < n; i++) x[i] = static_cast<float>(std::sin(0.37 * i) + 0.5 * std::cos(0.011 * i * i));
    std::vector<std::complex<float>> spectrum(RealFft7680::kHalf);
    RealFft7680 fft;

    fft.Forward(x.data(), spectrum.data());

    auto dft = [&](size_t bin) {
        std::complex<double> sum;
        for (size_t i = 0; i < n; i++) sum += double(x[i]) * std::polar(1.0, -2 * std::numbers::pi * bin * i / n);
        return sum;
    };
    for (size_t bin : {size_t{1}, size_t{2}, size_t{1279}, size_t{1280}, size_t{1920}, size_t{2561}, size_t{3839}}) {
        CHECK_NEAR(spectrum[bin].real(), dft(bin).real(), 2e-2);
        CHECK_NEAR(spectrum[bin].imag(), dft(bin).imag(), 2e-2);
    }
    CHECK_NEAR(spectrum[0].real(), dft(0).real(), 2e-2);
    CHECK_NEAR(spectrum[0].imag(), dft(n / 2).real(), 2e-2);
    fft.Inverse(spectrum.data(), back.data());
    double worst = 0;
    for (size_t i = 0; i < n; i++) worst = std::max(worst, double(std::fabs(back[i] / n - x[i])));
    CHECK(worst < 1e-5);
}

TEST_CASE(Sing_AnalyseGivesNumpysStftOfTheSameNoise) {
    std::vector<float> left, right, spectrum(kSpectrumSize);
    ReferenceNoise(left, right);
    HalfModel model;
    VocalSplitter splitter(model);

    splitter.Analyse(left.data(), right.data(), spectrum.data());

    // The numpy STFT of the same noise (torch.stft's conventions): (channel, bin, frame) -> value.
    auto at = [&](size_t channel, size_t bin, size_t frame) {
        return spectrum[channel * kChannelPlane + bin * kFrames + frame];
    };
    CHECK_NEAR(at(0, 0, 0), -4.7878336906433105, 2e-3);
    CHECK_NEAR(at(0, 5, 0), -36.421852111816406, 2e-3);
    CHECK_NEAR(at(1, 100, 17), -17.436662673950195, 2e-3);
    CHECK_NEAR(at(2, 3071, 255), 19.59156608581543, 2e-3);
    CHECK_NEAR(at(3, 1234, 128), -5.2249274253845215, 2e-3);
    CHECK_NEAR(at(1, 7, 255), -0.06372980028390884, 2e-3);
}

TEST_CASE(Sing_SynthesiseGivesNumpysIstftOfTheSameSpectrum) {
    std::vector<float> left, right, spectrum(kSpectrumSize), outLeft(kChunk), outRight(kChunk);
    ReferenceNoise(left, right);
    HalfModel model;
    VocalSplitter splitter(model);

    splitter.Analyse(left.data(), right.data(), spectrum.data());
    splitter.Synthesise(spectrum.data(), outLeft.data(), outRight.data());

    // The numpy inverse of that spectrum: the noise above 16.5 kHz is gone, so it differs from x.
    CHECK_NEAR(outLeft[0], -0.5218795537948608, 1e-4);
    CHECK_NEAR(outLeft[1], 0.09139186888933182, 1e-4);
    CHECK_NEAR(outRight[5000], 0.13018165528774261, 1e-4);
    CHECK_NEAR(outLeft[kChunk / 2], 0.35834959149360657, 1e-4);
    CHECK_NEAR(outRight[kChunk - 1], -0.13124258816242218, 1e-4);
    CHECK_NEAR(outLeft[kChunk - 3000], 0.07226432114839554, 1e-4);
}

// Away from the window's edges, where the reflected padding bends the signal; the numpy version does the same.
TEST_CASE(Sing_StftRoundTripGivesBackNoiseInsideTheModelsBins) {
    const std::vector<float> left = BandNoise(1), right = BandNoise(2);
    std::vector<float> spectrum(kSpectrumSize), outLeft(kChunk), outRight(kChunk);
    HalfModel model;
    VocalSplitter splitter(model);

    splitter.Analyse(left.data(), right.data(), spectrum.data());
    splitter.Synthesise(spectrum.data(), outLeft.data(), outRight.data());

    double worst = 0;
    for (size_t i = kFftSize / 2; i < kChunk - kFftSize / 2; i++) {
        worst = std::max({worst, double(std::fabs(outLeft[i] - left[i])), double(std::fabs(outRight[i] - right[i]))});
    }
    CHECK(worst < 1e-4);
}

// The precise model's STFT keeps every bin up to Nyquist, which rides bin 0 of the real FFT: white noise comes back.
TEST_CASE(Sing_PreciseStftRoundTripGivesBackFullBandNoise) {
    std::vector<float> left(kPrecise.Chunk()), right(kPrecise.Chunk());
    std::uint32_t state = 777;
    for (size_t i = 0; i < left.size(); i++) {
        for (std::vector<float>* channel : {&left, &right}) {
            state = state * 1664525u + 1013904223u;
            (*channel)[i] = static_cast<float>(((state >> 8) / 8388608.0 - 1.0) * 0.5);
        }
    }
    std::vector<float> spectrum(kPrecise.SpectrumSize()), outLeft(left.size()), outRight(left.size());
    HalfModel model(kPrecise);
    VocalSplitter splitter(model);

    splitter.Analyse(left.data(), right.data(), spectrum.data());
    splitter.Synthesise(spectrum.data(), outLeft.data(), outRight.data());

    double worst = 0;
    for (size_t i = kPrecise.fftSize / 2; i < left.size() - kPrecise.fftSize / 2; i++) {
        worst = std::max({worst, double(std::fabs(outLeft[i] - left[i])), double(std::fabs(outRight[i] - right[i]))});
    }
    CHECK(worst < 1e-4);
}

TEST_CASE(Sing_VocalsClearTheLowestBinsBeforeTheModel) {
    const std::vector<float> left = BandNoise(3), right = BandNoise(4);
    std::vector<float> outLeft(kChunk), outRight(kChunk);
    CapturingModel model;
    VocalSplitter splitter(model);

    REQUIRE(splitter.Vocals(left.data(), right.data(), outLeft.data(), outRight.data()));

    for (size_t channel = 0; channel < 4; channel++) {
        for (size_t frame = 0; frame < kFrames; frame++) {
            for (size_t bin = 0; bin < kClearedLowBins; bin++) {
                CHECK(model.seen[channel * kChannelPlane + bin * kFrames + frame] == 0.0f);
            }
        }
    }
    CHECK(model.seen[3 * kFrames + 100] != 0.0f);
    CHECK_NEAR(outLeft[kChunk / 2], left[kChunk / 2], 1e-4);
}

TEST_CASE(Sing_VocalsReportAModelThatFailed) {
    const std::vector<float> left = BandNoise(5);
    std::vector<float> out(kChunk, 7.0f);
    FailingModel model;
    VocalSplitter splitter(model);

    const bool ran = splitter.Vocals(left.data(), left.data(), out.data(), out.data());

    CHECK(!ran);
    CHECK(out[0] == 7.0f);
}

// ---- the resampler

TEST_CASE(Sing_ResamplerRoundTripFrom96kKeepsASine) {
    const double errorDb = RoundTripErrorDb(96000);

    CHECK(errorDb < -60);
}

TEST_CASE(Sing_ResamplerRoundTripFrom48kKeepsASine) {
    const double errorDb = RoundTripErrorDb(48000);

    CHECK(errorDb < -60);
}

TEST_CASE(Sing_ResamplerLatencyIsWholeOnBothSides) {
    const int rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
    for (int in : rates) {
        for (int out : rates) {
            const Resampler resampler(in, out);

            const std::int64_t latency = resampler.Latency();

            CHECK(latency * out % in == 0);
            CHECK(static_cast<std::int64_t>(resampler.OutputLatency()) * in == latency * out);
        }
    }
}

TEST_CASE(Sing_ResamplerGivesTheRatiosCountOfFrames) {
    Resampler resampler(44100, 96000);
    const std::vector<float> in(4410 * 2, 0.25f);
    std::vector<float> out(resampler.MaxOutput(441) * 2);
    size_t total = 0;

    for (size_t at = 0; at < 4410; at += 441) total += resampler.Process(in.data() + at * 2, 441, out.data());

    CHECK(total == 9600);
}

// A 30 kHz tone at 96 kHz would fold to 14.1 kHz at 44.1 kHz.
TEST_CASE(Sing_ResamplerStopsWhatWouldFoldBack) {
    Resampler down(96000, kModelRate);
    const std::vector<float> in = Sine(96000, 0.5, 30000, 0.5, 0.5);
    std::vector<float> out(down.MaxOutput(in.size() / 2) * 2);

    const size_t made = down.Process(in.data(), in.size() / 2, out.data());

    CHECK(Db(Rms(out, 0, 1000, made) / (0.5 / std::sqrt(2.0))) < -80);
}

// ---- the studio stream

TEST_CASE(Sing_StudioStreamTakesTheVocalsAwayExactlyTheDelayLate) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 4.0);
    const size_t delay = Frames(stream, kRate);

    const std::vector<float> out = Play(stream, splitter, song, 0, song.size() / 2, kRate, 0.0);

    CHECK_NEAR(stream.Delay(), 1.3, 1e-9);
    CHECK(WorstGap(out, song, 0, delay, delay + kRate / 5, song.size() / 2, kHalfOut) < 1e-3);
    CHECK(stream.Misses() == 0);
}

TEST_CASE(Sing_StudioStreamRunsThePreciseModelAtItsOwnDelay) {
    constexpr int kRate = 48000;
    HalfModel model(kPrecise);
    VocalSplitter splitter(model);
    StudioStream stream(kRate, kPrecise);
    const std::vector<float> song = Song(kRate, 9.0);
    const size_t delay = Frames(stream, kRate);

    const std::vector<float> out = Play(stream, splitter, song, 0, song.size() / 2, kRate, 0.0);

    CHECK_NEAR(stream.Delay(), kPrecise.delaySeconds, 1e-4);
    CHECK(WorstGap(out, song, 0, delay, delay + kRate / 5, song.size() / 2, 1 - kPrecise.compensate * 0.5) < 2e-3);
    CHECK(stream.Misses() == 0);
}

TEST_CASE(Sing_StudioStreamRefusesASplitterOfAnotherModel) {
    HalfModel model(kPrecise);
    VocalSplitter splitter(model);
    StudioStream stream(44100);
    std::vector<float> block(882, 0.1f);
    stream.Process(block.data(), block.size() / 2, 1.0);

    CHECK(!stream.Work(splitter));
}

TEST_CASE(Sing_StudioStreamIsSilentWhileBuffering) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 2.0);
    const size_t delay = Frames(stream, kRate);

    const std::vector<float> before = Play(stream, splitter, song, 0, delay, kRate, 0.0);
    const bool bufferingThen = stream.Buffering();
    const std::vector<float> after = Play(stream, splitter, song, delay, delay + kRate / 10, kRate, 0.0);

    CHECK(*std::max_element(before.begin(), before.end()) == 0.0f);
    CHECK(*std::min_element(before.begin(), before.end()) == 0.0f);
    CHECK(bufferingThen);
    CHECK(!stream.Buffering());
    CHECK(Rms(after, 0, 0, kRate / 10) > 0.05);
}

TEST_CASE(Sing_StudioStreamBuffersAgainAfterAFlush) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 7.0);
    const size_t delay = Frames(stream, kRate);
    const size_t seek = 3 * kRate;
    Play(stream, splitter, song, 0, seek, kRate, 0.0);

    stream.Flush();
    const bool bufferingAtOnce = stream.Buffering();
    const std::vector<float> out = Play(stream, splitter, song, seek, song.size() / 2, kRate, 0.0);

    CHECK(bufferingAtOnce);
    CHECK(Rms(out, 0, kRate / 100, delay) == 0.0);
    CHECK(WorstGap(out, song, seek, delay, delay + kRate / 5, out.size() / 2, kHalfOut) < 1e-3);
}

TEST_CASE(Sing_StudioStreamFadesOutOver10MsOnAFlushRatherThanCutting) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 5.0);
    const size_t delay = Frames(stream, kRate);
    const size_t seek = 3 * kRate;
    const size_t fade = kRate / 100;
    Play(stream, splitter, song, 0, seek, kRate, 1.0);  // the whole voice kept: the song as it is, delayed

    stream.Flush();
    const std::vector<float> out = Play(stream, splitter, song, seek, seek + kRate / 10, kRate, 1.0);

    double worst = 0;
    for (size_t j = 0; j < fade; j++) {
        const double gain = 1.0 - static_cast<double>(j + 1) / static_cast<double>(fade);
        worst = std::max(worst, std::fabs(out[j * 2] - song[(seek + j - delay) * 2] * gain));
    }
    CHECK(worst < 1e-6);
    CHECK(Rms(out, 0, 0, fade / 2) > 0.01);
    CHECK(Rms(out, 0, fade, kRate / 10) == 0.0);
}

TEST_CASE(Sing_StudioStreamPlaysTheSongAsItIsWhenTheWorkerIsLate) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Sine(kRate, 9.0, 440, 0.5, 0.5);
    const size_t delay = Frames(stream, kRate);
    const std::vector<float> warm = Play(stream, splitter, song, 0, 3 * kRate, kRate, 0.0);

    const std::vector<float> starved = Play(stream, splitter, song, 3 * kRate, 5 * kRate, kRate, 0.0, false);
    const std::uint64_t missed = stream.Misses();
    const std::vector<float> back = Play(stream, splitter, song, 5 * kRate, 9 * kRate, kRate, 0.0);

    std::vector<float> all = warm;
    all.insert(all.end(), starved.begin(), starved.end());
    all.insert(all.end(), back.begin(), back.end());
    double jump = 0;
    for (size_t n = delay + 1; n < all.size() / 2; n++) {
        jump = std::max(jump, double(std::fabs(all[n * 2] - all[n * 2 - 2])));
    }
    CHECK(missed > 0);
    CHECK(stream.Misses() - missed < 300);  // the worker catches up, then nothing more is missed
    CHECK(WorstGap(starved, song, 3 * kRate, delay, kRate, 2 * kRate, 1.0) < 1e-6);
    CHECK(WorstGap(back, song, 5 * kRate, delay, 3 * kRate, 4 * kRate, kHalfOut) < 1e-3);
    CHECK(jump < 0.05);  // the sine alone moves by 0.031 at most from one sample to the next
}

TEST_CASE(Sing_StudioStreamSpreadsAVoiceChangeOver50Ms) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 4.0);
    const size_t delay = Frames(stream, kRate);
    const size_t change = 3 * kRate;
    Play(stream, splitter, song, 0, change, kRate, 0.0);

    const std::vector<float> out = Play(stream, splitter, song, change, change + kRate / 5, kRate, 1.0);

    double worst = 0;
    for (size_t n = 0; n < out.size() / 2; n++) {
        const double voice = std::min(1.0, static_cast<double>(n + 1) / (0.05 * kRate));
        const double scale = 1 - (1 - voice) * kCompensate * 0.5;
        for (size_t channel = 0; channel < 2; channel++) {
            worst = std::max(worst, std::fabs(out[n * 2 + channel] - scale * song[(change + n - delay) * 2 + channel]));
        }
    }
    CHECK(worst < 1e-3);
}

// The player's volume is in the sound that comes in: turned down, the change is heard at once, not a delay later.
TEST_CASE(Sing_StudioStreamHearsAChangeOfVolumeAtOnce) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 4.0);
    const size_t delay = Frames(stream, kRate);
    const size_t change = 3 * kRate, block = kRate / 100;
    auto play = [&](size_t from, size_t to, double volume) {
        std::vector<float> out(song.begin() + static_cast<std::ptrdiff_t>(from * 2),
                               song.begin() + static_cast<std::ptrdiff_t>(to * 2));
        for (float& sample : out) sample *= static_cast<float>(volume);  // as Apple's player gives it
        for (size_t at = 0; at < to - from; at += block) {
            stream.Process(out.data() + at * 2, std::min(block, to - from - at), 0.0, false, 1.0, volume);
            while (stream.Work(splitter)) {
            }
        }
        return out;
    };
    play(0, change, 0.5);

    const std::vector<float> out = play(change, change + kRate / 5, 0.25);

    // After its 10 ms ramp, the song from a delay ago, which came in at 0.5, is heard at 0.25.
    CHECK(WorstGap(out, song, change, delay, kRate / 50, out.size() / 2, 0.25 * kHalfOut) < 1e-3);
}

// The singer alone: the music gone, the vocals the model found (half the song here, lifted by its compensation).
TEST_CASE(Sing_StudioStreamKeepsTheSingerAloneWithTheMusicOff) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 4.0);
    const size_t delay = Frames(stream, kRate);

    const std::vector<float> out = Play(stream, splitter, song, 0, song.size() / 2, kRate, 1.0, true, 0.0);

    CHECK(WorstGap(out, song, 0, delay, delay + kRate / 5, song.size() / 2, 0.5 * kCompensate) < 1e-3);
}

// Part of the music back under the singer: a quarter of it here.
TEST_CASE(Sing_StudioStreamKeepsPartOfTheMusicUnderTheSinger) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 4.0);
    const size_t delay = Frames(stream, kRate);

    const std::vector<float> out = Play(stream, splitter, song, 0, song.size() / 2, kRate, 1.0, true, 0.25);

    const double vocals = 0.5 * kCompensate;
    CHECK(WorstGap(out, song, 0, delay, delay + kRate / 5, song.size() / 2, 0.25 * (1 - vocals) + vocals) < 1e-3);
}

TEST_CASE(Sing_StudioStreamWorksAt96k) {
    constexpr int kRate = 96000;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 4.0);
    const size_t delay = Frames(stream, kRate);

    const std::vector<float> out = Play(stream, splitter, song, 0, song.size() / 2, kRate, 0.0);

    CHECK(WorstGap(out, song, 0, delay, delay + kRate / 5, song.size() / 2, kHalfOut) < 1e-3);
    CHECK(stream.Misses() == 0);
}

TEST_CASE(Sing_StudioStreamRaisesADelayItCouldNeverMeet) {
    const StudioStream stream(48000, 0.2, 0.5);

    const double delay = stream.Delay();

    CHECK(delay > 1.0);
    CHECK(delay < 1.05);
}

// The next window a few STFT hops later: what both share is taken over, and the model sees the same spectrum as
// with a whole analysis, while the vocals are made only where they are wanted.
TEST_CASE(Sing_ALaterWindowGivesTheSameSpectrumAsAWholeAnalysis) {
    constexpr size_t kShift = 17;
    const std::vector<float> left = BandNoise(7), right = BandNoise(8);
    std::vector<float> laterLeft(left.begin() + kShift * kFftHop, left.end()), laterRight(right.begin() + kShift * kFftHop, right.end());
    const std::vector<float> fresh = BandNoise(9);
    laterLeft.insert(laterLeft.end(), fresh.begin(), fresh.begin() + kShift * kFftHop);
    laterRight.insert(laterRight.end(), fresh.begin(), fresh.begin() + kShift * kFftHop);
    std::vector<float> outLeft(kChunk), outRight(kChunk), wholeLeft(kChunk), wholeRight(kChunk);
    CapturingModel incremental, whole;
    VocalSplitter splitter(incremental), reference(whole);
    VocalSplitter::Spectrum last;
    const size_t from = kChunk - 40000;
    REQUIRE(splitter.Vocals(left.data(), right.data(), last, VocalSplitter::kNewWindow, 0, outLeft.data(), outRight.data()));

    REQUIRE(splitter.Vocals(laterLeft.data(), laterRight.data(), last, kShift, from, outLeft.data(), outRight.data()));
    REQUIRE(reference.Vocals(laterLeft.data(), laterRight.data(), wholeLeft.data(), wholeRight.data()));

    CHECK(incremental.seen == whole.seen);
    double worst = 0;
    for (size_t i = from; i < kChunk; i++) {
        worst = std::max({worst, double(std::fabs(outLeft[i] - wholeLeft[i])), double(std::fabs(outRight[i] - wholeRight[i]))});
    }
    CHECK(worst < 1e-6);
}

// A pause keeps what the stream holds: playing again goes on from there at once, with no silence and nothing lost.
TEST_CASE(Sing_StudioStreamPlaysOnFromWhereItPausedWithNoSilence) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 6.0);
    const std::vector<float> silence(kRate * 2, 0.0f);
    const size_t delay = Frames(stream, kRate);
    const size_t pause = 3 * kRate;
    const size_t fade = kRate / 100;
    Play(stream, splitter, song, 0, pause, kRate, 1.0);

    std::vector<float> paused = silence;
    for (size_t at = 0; at < paused.size() / 2; at += kRate / 100) {
        stream.Process(paused.data() + at * 2, kRate / 100, 1.0, true);  // the player's stream says nothing
    }
    const double lag = stream.Lag();
    const std::vector<float> after = Play(stream, splitter, song, pause, pause + kRate / 2, kRate, 1.0);

    CHECK(Rms(paused, 0, fade, paused.size() / 2) == 0.0);
    CHECK_NEAR(lag, stream.Delay() - static_cast<double>(fade) / kRate, 1e-9);  // the fade out was heard
    // Faded back in over 10 ms, from just after the 10 ms faded out when it paused.
    CHECK(WorstGap(after, song, pause + fade, delay, fade, kRate / 2, 1.0) < 1e-6);
}

// The player hands over a little more once paused: taken in, it is heard after the pause, and Lag() counts it.
TEST_CASE(Sing_PausedStudioStreamTakesThePlayersLastSoundButNotItsSilence) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 6.0);
    const size_t delay = Frames(stream, kRate);
    const size_t pause = 3 * kRate;
    const size_t last = kRate / 20;  // 50 ms of sound after the pause
    const size_t fade = kRate / 100;
    Play(stream, splitter, song, 0, pause, kRate, 1.0);

    std::vector<float> tail(song.begin() + pause * 2, song.begin() + (pause + last) * 2);
    stream.Process(tail.data(), last, 1.0, true);
    std::vector<float> quiet(kRate / 10 * 2, 0.0f);
    stream.Process(quiet.data(), kRate / 10, 1.0, true);
    const double lag = stream.Lag();
    const std::vector<float> after = Play(stream, splitter, song, pause + last, pause + last + kRate / 2, kRate, 1.0);

    CHECK_NEAR(lag, stream.Delay() + static_cast<double>(last - fade) / kRate, 1e-9);
    CHECK(WorstGap(after, song, pause + last + fade, delay + last, fade, kRate / 2, 1.0) < 1e-6);
}

// The deck's stream starts in step with Apple's, which a pause may have made lag more than the delay.
TEST_CASE(Sing_StudioStreamFlushedWithALagHoldsThatLong) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 5.0);
    const size_t lag = Frames(stream, kRate) + kRate / 10;

    stream.Flush(StudioStream::Fade::None, static_cast<double>(lag) / kRate);
    const std::vector<float> out = Play(stream, splitter, song, 0, song.size() / 2, kRate, 0.0);

    CHECK(Rms(out, 0, 0, lag) == 0.0);
    CHECK(WorstGap(out, song, 0, lag, lag + kRate / 5, out.size() / 2, kHalfOut) < 1e-3);
}

// ---- the light filter

namespace {

// A tone's amplitude in one channel over frames [first, last), by projection on it.
double ToneLevel(const std::vector<float>& stereo, size_t channel, size_t first, size_t last, double hz, int rate) {
    double sine = 0, cosine = 0;
    for (size_t i = first; i < last; i++) {
        const double angle = 2 * kPi * hz * static_cast<double>(i) / rate;
        sine += stereo[i * 2 + channel] * std::sin(angle);
        cosine += stereo[i * 2 + channel] * std::cos(angle);
    }
    return 2 * std::hypot(sine, cosine) / static_cast<double>(last - first);
}

// What the mixes around a voice have: a sound on each side of its own (here a tone left at 5 kHz, right at 7 kHz).
void AddSides(std::vector<float>& stereo, int rate) {
    for (size_t i = 0; i < stereo.size() / 2; i++) {
        const double t = static_cast<double>(i) / rate;
        stereo[i * 2] += static_cast<float>(0.1 * std::sin(2 * kPi * 5000 * t));
        stereo[i * 2 + 1] += static_cast<float>(0.1 * std::sin(2 * kPi * 7000 * t));
    }
}

void Light(LightFilter& filter, std::vector<float>& stereo, int rate, double voice, size_t block, double music = 1.0) {
    for (size_t at = 0; at < stereo.size() / 2; at += block) {
        filter.Process(stereo.data() + at * 2, std::min(block, stereo.size() / 2 - at), rate, voice, music);
    }
}

}  // namespace

TEST_CASE(Sing_LightFilterRemovesACentredVoiceTone) {
    constexpr int kRate = 44100;
    LightFilter filter;
    std::vector<float> song = Sine(kRate, 1.0, 1000, 0.5, 0.5);
    AddSides(song, kRate);

    Light(filter, song, kRate, 0.0, 441);

    for (size_t channel = 0; channel < 2; channel++) {
        CHECK(Db(ToneLevel(song, channel, 22050, 44100, 1000, kRate) / 0.5) < -40);
    }
    CHECK(std::fabs(Db(ToneLevel(song, 0, 22050, 44100, 5000, kRate) / 0.1)) < 0.5);
    CHECK(std::fabs(Db(ToneLevel(song, 1, 22050, 44100, 7000, kRate) / 0.1)) < 0.5);
}

// The other way round: the centred voice tone stays, the sounds on each side go.
TEST_CASE(Sing_LightFilterKeepsACentredVoiceToneWithTheMusicOff) {
    constexpr int kRate = 44100;
    LightFilter filter;
    std::vector<float> song = Sine(kRate, 1.0, 1000, 0.5, 0.5);
    AddSides(song, kRate);

    Light(filter, song, kRate, 1.0, 441, 0.0);

    for (size_t channel = 0; channel < 2; channel++) {
        CHECK(std::fabs(Db(ToneLevel(song, channel, 22050, 44100, 1000, kRate) / 0.5)) < 0.5);
    }
    CHECK(Db(ToneLevel(song, 0, 22050, 44100, 5000, kRate) / 0.1) < -20);
    CHECK(Db(ToneLevel(song, 1, 22050, 44100, 7000, kRate) / 0.1) < -20);
}

// A sound on one side alone stays as it is, where the older filter halved it and put it on both sides.
TEST_CASE(Sing_LightFilterLeavesAToneOnOneSideAsItIs) {
    constexpr int kRate = 48000;
    LightFilter filter;
    std::vector<float> tone = Sine(kRate, 1.0, 1000, 0.5, 0.0);

    Light(filter, tone, kRate, 0.0, 480);

    CHECK(std::fabs(Db(ToneLevel(tone, 0, 24000, 48000, 1000, kRate) / 0.5)) < 0.1);
    CHECK(Rms(tone, 1, 24000, 48000) < 1e-5);
}

TEST_CASE(Sing_LightFilterKeepsACentredBassTone) {
    constexpr int kRate = 192000;
    LightFilter filter;
    std::vector<float> song = Sine(kRate, 1.0, 60, 0.5, 0.5);
    AddSides(song, kRate);

    Light(filter, song, kRate, 0.0, 1920);

    CHECK(std::fabs(Db(ToneLevel(song, 0, 96000, 192000, 60, kRate) / 0.5)) < 0.5);
    CHECK(std::fabs(Db(ToneLevel(song, 1, 96000, 192000, 60, kRate) / 0.5)) < 0.5);
}

// About 5.5 ms at every rate Apple plays.
TEST_CASE(Sing_LightFilterLagsTheSameTimeAtEveryRate) {
    for (const int rate : {44100, 48000, 88200, 96000, 176400, 192000}) {
        const double ms = 1000.0 * static_cast<double>(LightFilter::DelayFrames(rate)) / rate;
        CHECK(ms > 5.0 && ms < 6.0);
    }
}

TEST_CASE(Sing_LightFilterAtFullVoiceOnlyDelaysTheSong) {
    constexpr int kRate = 48000;
    LightFilter filter;
    const std::vector<float> song = Song(kRate, 0.5);
    std::vector<float> out = song;
    const size_t delay = LightFilter::DelayFrames(kRate);

    Light(filter, out, kRate, 1.0, 480);

    float worst = 0;
    for (size_t i = 2 * delay; i < out.size() / 2; i++) {
        for (size_t channel = 0; channel < 2; channel++) {
            worst = std::max(worst, std::fabs(out[i * 2 + channel] - song[(i - delay) * 2 + channel]));
        }
    }
    CHECK(worst < 1e-5f);
}

// Nothing has come through the filter at first: the song goes on as it is, then crossfades, never a gap.
TEST_CASE(Sing_LightFilterStartsWithoutAGap) {
    constexpr int kRate = 44100;
    LightFilter filter;
    std::vector<float> song(kRate / 5 * 2);
    std::uint32_t seed = 1;
    for (float& sample : song) {
        seed = seed * 1664525u + 1013904223u;
        sample = static_cast<float>(seed >> 8) / static_cast<float>(1u << 24) - 0.5f;  // noise: every frequency
    }
    std::vector<float> out = song;
    const size_t delay = LightFilter::DelayFrames(kRate);

    Light(filter, out, kRate, 1.0, 441);

    CHECK(std::equal(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(delay * 2), song.begin()));
    for (size_t at = 0; at + 128 <= 3 * delay; at += 64) {
        CHECK(std::fabs(Db(Rms(out, 0, at, at + 128) / Rms(song, 0, at, at + 128))) < 2);
    }
}

TEST_CASE(Sing_LightFilterGivesTheSameWhateverTheBlocks) {
    constexpr int kRate = 44100;
    LightFilter whole, single;
    std::vector<float> one = Song(kRate, 0.3);
    AddSides(one, kRate);
    std::vector<float> other = one;

    Light(whole, one, kRate, 0.2, 4096);
    Light(single, other, kRate, 0.2, 1);

    CHECK(one == other);
}

// A mono song cannot be told from its voice this way: past its first frame, the filter leaves it alone.
TEST_CASE(Sing_LightFilterStandsAsideOnAMonoSong) {
    constexpr int kRate = 44100;
    LightFilter filter;
    std::vector<float> song = Sine(kRate, 0.5, 1000, 0.5, 0.5);

    Light(filter, song, kRate, 0.0, 441);

    CHECK(std::fabs(Db(ToneLevel(song, 0, 4410, 22050, 1000, kRate) / 0.5)) < 0.1);
}

TEST_CASE(Sing_LightFilterResetForgetsTheLastSong) {
    constexpr int kRate = 44100;
    LightFilter filter;
    std::vector<float> song = Song(kRate, 0.2);
    std::vector<float> quiet(kRate / 10 * 2, 0.0f);
    Light(filter, song, kRate, 0.0, 441);

    filter.Reset();
    Light(filter, quiet, kRate, 0.0, 441);

    CHECK(std::all_of(quiet.begin(), quiet.end(), [](float sample) { return sample == 0.0f; }));
}

// ---- the studio files

namespace {

void WriteText(const std::filesystem::path& file, std::string_view text) { std::ofstream(file) << text << "\n"; }

namespace pack = ampify::sing_pack;

// The runtime and the standard model as ampify sing install leaves them, every file present.
void FillPack(const std::filesystem::path& folder) {
    for (std::string_view entry : pack::kRuntimeEntries) {
        WriteText(folder / std::filesystem::path(std::string(entry)).filename(), "dll");
    }
    WriteText(folder / std::string(pack::kRuntimeMarker), pack::kRuntime.sha256);
    WriteText(folder / std::string(pack::kStandard.file), "model");
    WriteText(folder / std::string(pack::kStandard.marker), pack::kStandard.download.sha256);
}

}  // namespace

TEST_CASE(Sing_PackIsReadyOnlyWithItsMarkersAndEveryFile) {
    ampify::test::ScratchDir whole("sing-pack"), missing("sing-pack"), stale("sing-pack");
    FillPack(whole.path());
    FillPack(missing.path());
    std::filesystem::remove(missing.path() / std::string(pack::kDirectMlLibrary));
    FillPack(stale.path());
    WriteText(stale.path() / std::string(pack::kStandard.marker), "an older model's sha-256");

    CHECK(pack::Ready(whole.path(), pack::kStandard));
    CHECK(!pack::Ready(whole.path(), pack::kPrecise));
    CHECK(!pack::Ready(missing.path(), pack::kStandard));
    CHECK(!pack::Ready(stale.path(), pack::kStandard));
}

TEST_CASE(Sing_PackDownloadsOnlyWhatAModelStillLacks) {
    ampify::test::ScratchDir empty("sing-pack"), standard("sing-pack");
    FillPack(standard.path());

    CHECK(pack::DownloadSize(empty.path(), pack::kStandard) == pack::kRuntime.size + pack::kStandard.download.size);
    CHECK(pack::DownloadSize(standard.path(), pack::kStandard) == 0);
    CHECK(pack::DownloadSize(standard.path(), pack::kPrecise) == pack::kPrecise.download.size);
    CHECK(pack::FindModel("precise") == &pack::kPrecise);
    CHECK(pack::FindModel("loud") == nullptr);
}

TEST_CASE(Sing_StudioStreamHoldsTheDelayAndLessWhileItFillsAgain) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 5.0);
    const size_t seek = 3 * kRate;
    Play(stream, splitter, song, 0, seek, kRate, 1.0);
    const double before = stream.Held();

    stream.Flush();
    const double atOnce = stream.Held();
    Play(stream, splitter, song, seek, seek + kRate / 2, kRate, 1.0);

    CHECK_NEAR(before, stream.Delay(), 1e-9);
    CHECK(atOnce == 0.0);
    CHECK_NEAR(stream.Held(), 0.5, 1e-3);
}

// A card that runs the precise model in 0.5 s a window keeps up with its hop of a second, yet each hop's vocals come
// 0.2 s after their sound: measured on a stream run in real time (20/11/2026), the voice came back for 0.2 s every
// second. The budget is what the delay leaves after two hops and the ahead, or the hop when that is shorter.
TEST_CASE(Sing_EachModelHasTheRunBudgetItsDelayLeaves) {
    CHECK_NEAR(kStandard.RunBudget(), 0.4, 1e-9);
    CHECK_NEAR(kPrecise.RunBudget(), 0.3, 1e-9);
}

TEST_CASE(Sing_PaceWatchCallsAModelOverItsBudgetMostOfTheTimeTooSlow) {
    PaceWatch watch(kPrecise.RunBudget());
    for (int i = 0; i < 7; i++) watch.Add(0.5);
    const bool tooSlowEarly = watch.TooSlow();
    watch.Add(0.5);

    CHECK(!tooSlowEarly);  // too few windows to tell
    CHECK(watch.TooSlow());
    watch.Clear();
    CHECK(!watch.TooSlow());
}

TEST_CASE(Sing_PaceWatchForgivesAFewSlowWindows) {
    PaceWatch watch(kStandard.RunBudget());
    for (int i = 0; i < 20; i++) watch.Add(i % 3 == 0 ? 0.9 : 0.1);  // a busy moment now and then

    CHECK(!watch.TooSlow());
    watch.Add(std::nan(""));  // a time that cannot be read is counted against the card
    for (int i = 0; i < 4; i++) watch.Add(0.6);
    CHECK(watch.TooSlow());
}

TEST_CASE(Sing_StudioStreamFadesTheSongComingInWhenItWasNotPlaying) {
    constexpr int kRate = 44100;
    HalfModel model;
    VocalSplitter splitter(model);
    StudioStream stream(kRate);
    const std::vector<float> song = Song(kRate, 5.0);
    const size_t back = 3 * kRate;  // where the song is when the stream plays again, a second after it stopped
    const size_t fade = kRate / 100;
    Play(stream, splitter, song, 0, 2 * kRate, kRate, 1.0);

    stream.Flush(StudioStream::Fade::Live);
    const std::vector<float> out = Play(stream, splitter, song, back, back + kRate / 10, kRate, 1.0);

    double worst = 0;
    for (size_t j = 0; j < fade; j++) {
        const double gain = 1.0 - static_cast<double>(j + 1) / static_cast<double>(fade);
        worst = std::max(worst, std::fabs(out[j * 2] - song[(back + j) * 2] * gain));
    }
    CHECK(worst < 1e-6);
    CHECK(Rms(out, 0, fade, kRate / 10) == 0.0);
}
