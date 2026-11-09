// Tests of the audio pieces behind Ampify.audio and Ampify.color, on synthetic signals: the spectrum, the tempo, the
// equalizer, the live loudness, Ampify's steps on the sample path, the engine, and colour extraction.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numbers>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../src/core/audio/analysis.hpp"
#include "../src/core/audio/audio_engine.hpp"
#include "../src/core/audio/audio_share.hpp"
#include "../src/core/audio/bar_meter.hpp"
#include "../src/core/audio/bar_shapes.hpp"
#include "../src/core/audio/equalizer.hpp"
#include "../src/core/audio/gain_ramp.hpp"
#include "../src/core/audio/live_loudness.hpp"
#include "../src/core/audio/object_pass.hpp"
#include "../src/core/audio/sample_path.hpp"
#include "../src/core/color_extract.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

using ampify::audio::AudioAnalyzer;
using ampify::audio::EqProcessor;
using ampify::audio::EqSettings;

constexpr double kPi = std::numbers::pi;

using ampify::test::Clicks;
using ampify::test::Sine;

void FeedInBlocks(AudioAnalyzer& analyzer, const std::vector<float>& samples, int channels, double rate,
                  size_t block = 480) {
    size_t frames = samples.size() / static_cast<size_t>(channels);
    for (size_t start = 0; start < frames; start += block) {
        size_t count = std::min(block, frames - start);
        analyzer.Feed(samples.data() + start * static_cast<size_t>(channels), count, channels, rate);
    }
}

size_t BandOf(double frequency) {
    std::vector<double> edges = ampify::audio::LogBandEdges(AudioAnalyzer::kBands, 40.0, 16000.0);
    for (size_t i = 0; i + 1 < edges.size(); i++) {
        if (frequency >= edges[i] && frequency < edges[i + 1]) return i;
    }
    return edges.size();
}

size_t Loudest(const std::vector<float>& bands) {
    return static_cast<size_t>(std::max_element(bands.begin(), bands.end()) - bands.begin());
}

double RmsDb(const std::vector<float>& samples, size_t from) {
    double sum = 0;
    for (size_t i = from; i < samples.size(); i++) sum += static_cast<double>(samples[i]) * samples[i];
    return 10 * std::log10(sum / static_cast<double>(samples.size() - from));
}

// Gain of the equalizer at `frequency`, measured on a sine once the filter has settled, in dB.
double MeasuredGainDb(EqSettings& settings, double frequency, double rate, int channels) {
    EqProcessor processor(settings);
    size_t frames = static_cast<size_t>(rate);  // 1 s: the easing takes 0.1 s, the filter rings out well before
    std::vector<float> input = Sine(frequency, 0.25, rate, frames, channels);
    std::vector<float> output = input;
    for (size_t start = 0; start < frames; start += 512) {
        size_t count = std::min<size_t>(512, frames - start);
        processor.Process(output.data() + start * static_cast<size_t>(channels), count, channels, rate);
    }
    size_t from = input.size() / 2;
    return RmsDb(output, from) - RmsDb(input, from);
}

}  // namespace

TEST_CASE(Analysis_LevelsOfASine) {
    std::vector<float> sine = Sine(1000, 1.0, 48000, 48000, 1);
    ampify::audio::Levels levels = ampify::audio::MeasureLevels(sine.data(), sine.size());
    CHECK_NEAR(levels.rms, std::sqrt(0.5), 0.001);
    CHECK_NEAR(levels.peak, 1.0, 0.001);
}

TEST_CASE(Analysis_FftPutsASineInItsBin) {
    ampify::audio::Fft fft(1024);
    std::vector<std::complex<float>> data(1024);
    for (size_t i = 0; i < 1024; i++) data[i] = {static_cast<float>(std::cos(2 * kPi * 37 * static_cast<double>(i) / 1024)), 0};
    fft.Forward(data);
    CHECK_NEAR(std::abs(data[37]), 512.0, 0.01);
    CHECK(std::abs(data[36]) < 0.01 && std::abs(data[100]) < 0.01);
}

TEST_CASE(Analysis_SineFallsInTheRightBand) {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (double frequency : {100.0, 1000.0, 4300.0}) {
            AudioAnalyzer analyzer;
            FeedInBlocks(analyzer, Sine(frequency, 0.5, rate, static_cast<size_t>(rate * 0.5)), 2, rate);
            CHECK(analyzer.FrameCount() >= 9);  // about 20 frames a second
            const auto& bands = analyzer.Latest().bandsDb;
            CHECK(bands.size() == AudioAnalyzer::kBands);
            CHECK(Loudest(bands) == BandOf(frequency));
            CHECK_NEAR(bands[BandOf(frequency)], 20 * std::log10(0.5), 1.0);  // a sine of 0.5 reads -6 dBFS
            CHECK_NEAR(analyzer.Latest().rms, 0.5 / std::sqrt(2.0), 0.01);
            CHECK_NEAR(analyzer.Latest().peak, 0.5, 0.01);
        }
    }
}

TEST_CASE(Analysis_ScaleUndoesThePlayerVolume) {
    AudioAnalyzer analyzer;
    std::vector<float> quiet = Sine(1000, 0.25, 48000, 24000);
    analyzer.Feed(quiet.data(), 24000, 2, 48000, 2.0f);  // a slider at 0.5, divided out
    CHECK_NEAR(analyzer.Latest().peak, 0.5, 0.01);
}

TEST_CASE(Analysis_ClickTrainAt120BpmGives120) {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        AudioAnalyzer analyzer;
        FeedInBlocks(analyzer, Clicks(120, rate, 10), 2, rate);
        ampify::audio::TempoEstimate tempo = analyzer.Tempo();
        CHECK_NEAR(tempo.bpm, 120.0, 1.5);
        CHECK(tempo.confidence > 0.3);
    }
}

TEST_CASE(Analysis_OtherTempos) {
    for (double bpm : {90.0, 100.0, 140.0, 160.0}) {
        AudioAnalyzer analyzer;
        FeedInBlocks(analyzer, Clicks(bpm, 48000, 10), 2, 48000);
        CHECK_NEAR(analyzer.Tempo().bpm, bpm, bpm * 0.02);
    }
}

TEST_CASE(Analysis_TempoUnknownOnSilenceAndShortInput) {
    AudioAnalyzer analyzer;
    std::vector<float> silence(48000 * 2 * 5, 0.0f);
    FeedInBlocks(analyzer, silence, 2, 48000);
    CHECK(analyzer.Tempo().bpm == 0);

    AudioAnalyzer shortInput;
    FeedInBlocks(shortInput, Clicks(120, 48000, 2), 2, 48000);
    CHECK(shortInput.Tempo().bpm == 0);  // under 3 s of envelope
}

TEST_CASE(Equalizer_DesignsHitTheirGain) {
    using ampify::audio::BiquadCoefficients;
    using ampify::audio::FilterKind;
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        auto peak = BiquadCoefficients::Design(FilterKind::Peaking, 1000, 1.41, 6, rate);
        CHECK_NEAR(peak.MagnitudeDb(1000, rate), 6.0, 0.01);
        CHECK_NEAR(peak.MagnitudeDb(20, rate), 0.0, 0.05);
        auto low = BiquadCoefficients::Design(FilterKind::LowShelf, 200, 1, -6, rate);
        CHECK_NEAR(low.MagnitudeDb(10, rate), -6.0, 0.1);
        CHECK_NEAR(low.MagnitudeDb(10000, rate), 0.0, 0.1);
        auto high = BiquadCoefficients::Design(FilterKind::HighShelf, 4000, 1, 6, rate);
        CHECK_NEAR(high.MagnitudeDb(rate * 0.49, rate), 6.0, 0.3);
        CHECK_NEAR(high.MagnitudeDb(100, rate), 0.0, 0.1);
    }
}

TEST_CASE(Equalizer_PlusSixAtCentreAndNothingFarAway) {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (int channels : {1, 2}) {
            EqSettings settings;
            CHECK(settings.SetGainDb(5, 6.0));  // the 1 kHz band
            CHECK_NEAR(MeasuredGainDb(settings, 1000, rate, channels), 6.0, 0.2);
            CHECK_NEAR(MeasuredGainDb(settings, 60, rate, channels), 0.0, 0.2);
            CHECK_NEAR(MeasuredGainDb(settings, 12000, rate, channels), 0.0, 0.2);
        }
    }
}

TEST_CASE(Equalizer_FlatIsBitExact) {
    std::mt19937 random(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<float> input(4800 * 2);
    for (float& sample : input) sample = noise(random);

    EqSettings settings;
    EqProcessor processor(settings);
    std::vector<float> output = input;
    CHECK(!processor.Process(output.data(), 4800, 2, 48000));
    CHECK(std::memcmp(output.data(), input.data(), input.size() * sizeof(float)) == 0);

    // A band raised then put back: once it has eased back and rung out, the samples are untouched again.
    settings.SetGainDb(3, 6);
    for (int i = 0; i < 10; i++) processor.Process(output.data(), 4800, 2, 48000);
    settings.SetGainDb(3, 0);
    for (int i = 0; i < 20 && !processor.Idle(); i++) {
        std::copy(input.begin(), input.end(), output.begin());
        processor.Process(output.data(), 4800, 2, 48000);
    }
    CHECK(processor.Idle());
    std::copy(input.begin(), input.end(), output.begin());
    CHECK(!processor.Process(output.data(), 4800, 2, 48000));
    CHECK(std::memcmp(output.data(), input.data(), input.size() * sizeof(float)) == 0);
}

TEST_CASE(Equalizer_ChangesAreEased) {
    EqSettings settings;
    EqProcessor processor(settings);
    settings.SetGainDb(5, 12);
    std::vector<float> block = Sine(1000, 0.1, 48000, EqProcessor::kBlock);
    processor.Process(block.data(), EqProcessor::kBlock, 2, 48000);
    CHECK_NEAR(processor.CurrentDb(5), EqProcessor::kDbPerSecond * EqProcessor::kBlock / 48000.0, 1e-9);
    std::vector<float> more = Sine(1000, 0.1, 48000, 4800);
    processor.Process(more.data(), 4800, 2, 48000);  // 0.1 s: 12 dB reached
    CHECK_NEAR(processor.CurrentDb(5), 12.0, 1e-9);
}

TEST_CASE(Equalizer_StableWithEveryBandAtTheTop) {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        EqSettings settings;
        for (size_t band = 0; band < settings.Bands().size(); band++) settings.SetGainDb(band, 12);
        EqProcessor processor(settings);
        std::mt19937 random(3);
        std::uniform_real_distribution<float> noise(-0.1f, 0.1f);
        std::vector<float> samples(static_cast<size_t>(rate) * 2);
        for (float& sample : samples) sample = noise(random);
        processor.Process(samples.data(), samples.size() / 2, 2, rate);
        bool finite = std::all_of(samples.begin(), samples.end(), [](float s) { return std::isfinite(s) && std::fabs(s) < 10; });
        CHECK(finite);
        // Silence after: the filters ring out instead of growing.
        std::vector<float> silence(static_cast<size_t>(rate) * 2, 0.0f);
        processor.Process(silence.data(), silence.size() / 2, 2, rate);
        CHECK(std::fabs(silence.back()) < 1e-6);
    }
}

TEST_CASE(Equalizer_SettingsClampAndPresets) {
    EqSettings settings;
    CHECK(settings.Bands().size() == 10);
    CHECK(settings.SetGainDb(0, 40));
    CHECK_NEAR(settings.GainDb(0), ampify::audio::kEqMaxGainDb, 1e-9);
    CHECK(!settings.SetGainDb(10, 1));
    CHECK(!settings.SetGainDb(1, std::nan("")));
    settings.Flatten();
    CHECK(settings.IsFlat());
    CHECK(ampify::audio::PresetGains("rock").has_value());
    CHECK(ampify::audio::PresetGains("rock")->size() == 10);
    CHECK(!ampify::audio::PresetGains("unknown").has_value());
    for (const std::string& name : ampify::audio::PresetNames()) CHECK(ampify::audio::PresetGains(name).has_value());
}

TEST_CASE(LiveLoudness_SineAtMinusTwentyLufs) {
    // A 997 Hz sine of amplitude 0.1 on both channels is -20 LUFS.
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        ampify::audio::LiveLoudness meter(rate, 2);
        std::vector<float> sine = Sine(997, 0.1, rate, static_cast<size_t>(rate * 0.3));
        meter.Process(sine.data(), sine.size() / 2);
        CHECK(!meter.MomentaryLufs().has_value());  // under 400 ms
        std::vector<float> rest = Sine(997, 0.1, rate, static_cast<size_t>(rate * 3), 2, sine.size() / 2);
        meter.Process(rest.data(), rest.size() / 2);
        CHECK(meter.MomentaryLufs().has_value() && meter.ShortTermLufs().has_value());
        CHECK_NEAR(meter.MomentaryLufs().value_or(0), -20.0, 0.2);
        CHECK_NEAR(meter.ShortTermLufs().value_or(0), -20.0, 0.2);
    }
}

TEST_CASE(LiveLoudness_MomentaryFollowsAChange) {
    ampify::audio::LiveLoudness meter(48000, 2);
    std::vector<float> loud = Sine(997, 0.1, 48000, 48000 * 3);
    meter.Process(loud.data(), loud.size() / 2);
    std::vector<float> quiet = Sine(997, 0.01, 48000, 48000);
    meter.Process(quiet.data(), quiet.size() / 2);
    CHECK_NEAR(meter.MomentaryLufs().value_or(0), -40.0, 0.3);
    CHECK(meter.ShortTermLufs().value_or(0) > -30.0);  // 2 s of the loud part still in the 3 s window
}

TEST_CASE(SamplePath_UnityGainAndFlatEqLeaveTheStreamUntouched) {
    ampify::audio::AudioEngine engine;
    ampify::audio::SamplePath kit(engine);
    std::vector<float> input = Sine(440, 0.99, 48000, 4800);  // above the knee: the knee must not run either
    std::vector<float> samples = input;
    std::vector<float> curve(4800, 1.0f);
    ampify::audio::ApplyCurve(samples.data(), 4800, 2, curve, true, kit.StreamStage(48000), &kit);
    CHECK(std::memcmp(samples.data(), input.data(), input.size() * sizeof(float)) == 0);
}

TEST_CASE(SamplePath_EqualizerRunsAfterTheGainAndBeforeTheKnee) {
    ampify::audio::AudioEngine engine;
    ampify::audio::SamplePath kit(engine);
    engine.Eq().SetGainDb(5, 6.0206);  // x2 at 1 kHz
    const double rate = 48000;
    const size_t frames = 48000;
    std::vector<float> curve(frames, 2.0f);  // the leveler's gain, x2

    // 0.1 x 2 (gain) x 2 (equalizer) = 0.4: under the knee, so the product shows.
    std::vector<float> quiet = Sine(1000, 0.1, rate, frames);
    ampify::audio::ApplyCurve(quiet.data(), frames, 2, curve, false, kit.StreamStage(48000), &kit);
    float peak = 0;
    for (size_t i = frames; i < quiet.size(); i++) peak = std::max(peak, std::fabs(quiet[i]));
    CHECK_NEAR(peak, 0.4, 0.01);

    // 0.3 x 2 x 2 = 1.2: the knee comes last and keeps it under full scale.
    std::vector<float> loud = Sine(1000, 0.3, rate, frames);
    ampify::audio::ApplyCurve(loud.data(), frames, 2, curve, false, kit.StreamStage(48000), &kit);
    float loudPeak = 0;
    for (float sample : loud) loudPeak = std::max(loudPeak, std::fabs(sample));
    CHECK(loudPeak < 1.0f && loudPeak > 0.94f);
}

TEST_CASE(SamplePath_EqualizerOnSpatialObjects) {
    ampify::audio::AudioEngine engine;
    ampify::audio::SamplePath kit(engine);
    engine.Eq().SetGainDb(5, 6.0);
    ampify::audio::ObjectPass pass(48000);
    std::vector<float> a = Sine(1000, 0.1, 48000, 48000, 1);
    std::vector<float> b = Sine(1000, 0.1, 48000, 48000, 1);
    std::vector<float> reference = a;
    for (size_t start = 0; start < 48000; start += 480) {
        ampify::audio::ObjectBuffer objects[] = {{&a, a.data() + start}, {&b, b.data() + start}};
        const ampify::audio::PassLevels& levels = pass.Process(objects, 480, 1.0, kit.ObjectStage(48000));
        kit.AfterObjects(objects, 480, 48000, levels);
    }
    CHECK_NEAR(RmsDb(a, 24000) - RmsDb(reference, 24000), 6.0, 0.2);
    CHECK_NEAR(RmsDb(b, 24000) - RmsDb(reference, 24000), 6.0, 0.2);
    CHECK(engine.Loudness().momentary.has_value());  // the objects' power, published as they play
}

TEST_CASE(SamplePath_LoudnessAndAnalysisIgnoreTheSlider) {
    ampify::audio::AudioEngine engine;
    ampify::audio::SamplePath kit(engine);
    engine.SubscribeFrames("demo", [](const ampify::audio::AnalysisFrame&) {});
    const float volume = 0.5f;
    std::vector<float> curve;
    for (size_t start = 0; start < 48000 * 4; start += 480) {
        // 0.1 at full slider: the stream carries 0.05 after the player volume.
        std::vector<float> block = Sine(997, 0.1 * volume, 48000, 480, 2, start);
        kit.BeforeStream(block.data(), 480, 2, 48000, volume);
        curve.assign(480, 1.0f);
        ampify::audio::ApplyCurve(block.data(), 480, 2, curve, true, kit.StreamStage(48000), &kit);
        kit.AfterStream(block.data(), 480, 2, 48000, volume);
    }
    CHECK_NEAR(engine.Loudness().momentary.value_or(0), -20.0, 0.2);
    CHECK_NEAR(engine.Loudness().shortTerm.value_or(0), -20.0, 0.2);
    ampify::AudioAnalysisResult result = engine.Analyze("demo");
    CHECK(result.frame.has_value());
    if (result.frame) CHECK_NEAR(result.frame->peak, 0.1, 0.005);
}

TEST_CASE(AudioEngine_DeliversEachFrameOnceAndStopsOnRelease) {
    ampify::audio::AudioEngine engine;
    std::vector<float> sine = Sine(1000, 0.5, 48000, 4800);
    engine.FeedAnalysis(sine.data(), 4800, 2, 48000);
    CHECK(!engine.AnalysisWanted());  // nobody listens: nothing analysed

    int calls = 0;
    engine.SubscribeFrames("a", [&](const ampify::audio::AnalysisFrame& frame) {
        calls++;
        CHECK(frame.bandsDb.size() == AudioAnalyzer::kBands);
    });
    CHECK(engine.AnalysisWanted());
    engine.DeliverPending();
    CHECK(calls == 0);
    engine.FeedAnalysis(sine.data(), 4800, 2, 48000);
    engine.DeliverPending();
    engine.DeliverPending();
    CHECK(calls == 1);

    engine.Release("a");
    CHECK(!engine.AnalysisWanted());
    engine.FeedAnalysis(sine.data(), 4800, 2, 48000);
    engine.DeliverPending();
    CHECK(calls == 1);
}

TEST_CASE(AudioEngine_ReleaseFlattensOnlyTheLastEqualizerOwner) {
    ampify::audio::AudioEngine engine;
    CHECK(engine.SetEqBand("a", 2, 4));
    CHECK(!engine.SetEqBand("a", 99, 4));
    engine.Release("b");
    CHECK_NEAR(engine.EqBands()[2].gainDb, 4.0, 1e-6);
    engine.Release("a");
    CHECK(engine.Eq().IsFlat());
}

TEST_CASE(AudioEngine_VolumeAndLoudnessAreReadOnlyReadings) {
    ampify::audio::AudioEngine engine;
    CHECK(!engine.PlayerVolume().has_value());
    CHECK(!engine.Loudness().momentary.has_value());
    engine.PublishPlayerVolume(0.42);
    CHECK_NEAR(engine.PlayerVolume().value_or(0), 0.42, 1e-9);
    engine.PublishLoudness({-12.5, std::nullopt});
    CHECK_NEAR(engine.Loudness().momentary.value_or(0), -12.5, 1e-9);
    CHECK(!engine.Loudness().shortTerm.has_value());
    CHECK(engine.Analyze("reader").bandFrequencies.size() == AudioAnalyzer::kBands);  // before any audio too
}

TEST_CASE(AudioEngine_DeliveryThreadSendsFrames) {
    ampify::audio::AudioEngine engine;
    std::atomic<int> calls{0};
    engine.SubscribeFrames("a", [&](const ampify::audio::AnalysisFrame&) { calls++; });
    engine.StartDeliveryThread(std::chrono::milliseconds(5));
    std::vector<float> sine = Sine(1000, 0.5, 48000, 4800);
    for (int attempt = 0; attempt < 200 && calls.load() == 0; attempt++) {
        engine.FeedAnalysis(sine.data(), 4800, 2, 48000);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(calls.load() > 0);
    engine.Release("a");
}

namespace {

std::vector<uint8_t> TwoColours(size_t width, size_t height, ampify::Rgb first, ampify::Rgb second, size_t firstColumns) {
    std::vector<uint8_t> rgba(width * height * 4);
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            const ampify::Rgb& c = x < firstColumns ? first : second;
            uint8_t* p = &rgba[(y * width + x) * 4];
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = 255;
        }
    }
    return rgba;
}

}  // namespace

TEST_CASE(ColorExtract_TwoColourImageGivesBothColours) {
    const ampify::Rgb red{200, 40, 40};
    const ampify::Rgb blue{40, 70, 200};
    std::vector<uint8_t> image = TwoColours(64, 32, red, blue, 45);  // 70 % red
    auto colors = ampify::ExtractColors(image.data(), image.size(), 64, 32);
    CHECK(colors.has_value());
    if (!colors) return;
    CHECK(colors->palette.size() == 2);
    CHECK(colors->palette[0].color == red);
    CHECK(colors->palette[1].color == blue);
    CHECK(colors->prominent == red);
    CHECK(colors->vibrant == red || colors->vibrant == blue);
    CHECK(colors->vibrantNonAlarming == blue);  // the red is a warning red
    CHECK(colors->prominent.Hex() == "#c82828");
}

TEST_CASE(ColorExtract_EveryKeyIsFilledAndTransparencyIgnored) {
    std::vector<uint8_t> image(16 * 16 * 4, 0);
    for (size_t i = 0; i < 16 * 16; i++) {
        image[i * 4 + 0] = image[i * 4 + 1] = image[i * 4 + 2] = 128;
        image[i * 4 + 3] = i < 128 ? 255 : 0;  // half transparent: ignored
    }
    auto colors = ampify::ExtractColors(image.data(), image.size(), 16, 16);
    CHECK(colors.has_value());
    if (!colors) return;
    CHECK(colors->palette.size() == 1);
    CHECK(colors->prominent == (ampify::Rgb{128, 128, 128}));
    CHECK(!(colors->darkVibrant == colors->lightVibrant));  // made from the grey, at their own lightness

    std::vector<uint8_t> clear(4 * 4 * 4, 0);
    CHECK(!ampify::ExtractColors(clear.data(), clear.size(), 4, 4).has_value());
    CHECK(!ampify::ExtractColors(image.data(), image.size() - 1, 16, 16).has_value());
}

TEST_CASE(ColorExtract_LargeImageIsSampled) {
    std::vector<uint8_t> image = TwoColours(1000, 1000, {10, 200, 30}, {250, 250, 250}, 500);
    auto colors = ampify::ExtractColors(image.data(), image.size(), 1000, 1000);
    CHECK(colors.has_value() && colors->palette.size() == 2);
}

TEST_CASE(AudioShare_AFrameWrittenIsReadWhileFresh) {
    ampify::audio::SharedAudio block{};
    CHECK(ampify::audio::ClaimShared(block));
    CHECK(ampify::audio::ClaimShared(block));  // the other process finds it Ampify's, and uses it too
    ampify::audio::AnalysisFrame frame;
    frame.bandsDb = {-10.0f, -20.0f, -30.0f};
    frame.rms = 0.25f;
    ampify::audio::WriteSharedFrame(block, frame, {120.0, 0.8}, {60.0, 1000.0, 8000.0}, 1000);

    ampify::audio::SharedAudioReading reading;
    CHECK(ampify::audio::ReadShared(block, 1100, reading));
    CHECK(reading.frame.has_value());
    CHECK(reading.frames == 1);
    CHECK(reading.frame && reading.frame->bandsDb.size() == 3);
    CHECK(reading.frame && reading.frame->bandsDb[1] == -20.0f);
    CHECK_NEAR(reading.tempo.bpm, 120.0, 1e-9);
    CHECK(!reading.volume.has_value());  // never seen
    REQUIRE(reading.bandFrequencies.size() == 3);
    CHECK(reading.bandFrequencies[1] == 1000.0);

    // A frame older than 300 ms means the music stopped: no frame, the count stays.
    CHECK(ampify::audio::ReadShared(block, 1400, reading));
    CHECK(!reading.frame.has_value());
    CHECK(reading.frames == 1);
    CHECK(reading.bandFrequencies.size() == 3);  // the engine's band layout stays known
}

TEST_CASE(AudioShare_TheStatusCarriesTheVolumeAndTheLoudness) {
    ampify::audio::SharedAudio block{};
    ampify::audio::ClaimShared(block);
    ampify::audio::WriteSharedStatus(block, 0.62, {-14.0, -15.5}, 5000);
    ampify::audio::SharedAudioReading reading;
    CHECK(ampify::audio::ReadShared(block, 5200, reading));
    CHECK(reading.volume && *reading.volume == 0.62);
    CHECK(reading.loudness.momentary && *reading.loudness.momentary == -14.0);
    CHECK(ampify::audio::ReadShared(block, 7000, reading));
    CHECK(!reading.loudness.momentary.has_value());  // stale after 1.5 s
    CHECK(reading.volume && *reading.volume == 0.62);  // the volume stays known
}

TEST_CASE(AudioShare_TheInterfaceDemandLastsTwoSeconds) {
    ampify::audio::SharedAudio block{};
    ampify::audio::ClaimShared(block);
    CHECK(!ampify::audio::SharedWanted(block, 10));
    ampify::audio::DemandShared(block, 1000);
    CHECK(ampify::audio::SharedWanted(block, 2900));
    CHECK(!ampify::audio::SharedWanted(block, 3100));
}

TEST_CASE(AudioShare_TheVolumeAsksForTheHooksAlone) {
    ampify::audio::SharedAudio block{};
    ampify::audio::ClaimShared(block);
    ampify::audio::DemandSound(block, 1000);
    CHECK(ampify::audio::SoundWanted(block, 2900));
    CHECK(!ampify::audio::SharedWanted(block, 2900));  // no analysis for a volume
    CHECK(!ampify::audio::SoundWanted(block, 3100));
    ampify::audio::DemandShared(block, 5000);
    CHECK(ampify::audio::SoundWanted(block, 6000));  // the analysis needs the hooks too
}

TEST_CASE(AudioShare_ABlockOfAnotherLayoutIsNotRead) {
    ampify::audio::SharedAudio block{};
    block.magic = 0x12345678;
    CHECK(!ampify::audio::ClaimShared(block));
    ampify::audio::SharedAudioReading reading;
    CHECK(!ampify::audio::ReadShared(block, 0, reading));
}

TEST_CASE(BarMeter_LoudBandsRiseAtOnceAndFallSlowly) {
    ampify::audio::BarMeter meter(4);
    std::vector<float> bands(16, -90.0f);
    bands[0] = bands[1] = -12.0f;  // a kick in the lowest bands
    meter.Update(&bands, 0.033);
    CHECK(meter.Levels()[0] > 0.8f);
    CHECK(meter.Levels()[3] < 0.05f);

    std::vector<float> quiet(16, -90.0f);
    meter.Update(&quiet, 0.1);
    CHECK(meter.Levels()[0] > 0.5f);  // still falling
    meter.Update(nullptr, 0.5);       // paused: every bar goes down
    CHECK(meter.Levels()[0] < 0.01f);
}

TEST_CASE(BarShapes_TheMirrorPutsTheLowNotesInTheMiddle) {
    using ampify::audio::MirrorSource;
    CHECK(ampify::audio::MirrorBands(10) == 5 && ampify::audio::MirrorBands(9) == 5);
    CHECK(MirrorSource(4, 10) == 0 && MirrorSource(5, 10) == 0);  // the two middle bars: the lowest band
    CHECK(MirrorSource(0, 10) == 4 && MirrorSource(9, 10) == 4);  // both ends: the highest
    CHECK(MirrorSource(4, 9) == 0 && MirrorSource(0, 9) == 4 && MirrorSource(8, 9) == 4);
    for (size_t i = 0; i < 32; i++) CHECK(MirrorSource(i, 32) < ampify::audio::MirrorBands(32));
}

TEST_CASE(BarShapes_ARingBarStandsOnTheRingAndPointsOutwards) {
    const size_t count = 8;
    ampify::audio::Ring ring = ampify::audio::LayRing(100, 60, count);
    CHECK(ring.centreX == 50 && ring.centreY == 30);
    CHECK(std::abs(ring.radius + ring.length - 30) < 1e-4f);  // the tips of full bars touch the shorter side
    CHECK(ring.thickness >= 1.5f && ring.thickness <= 6.0f);
    const auto apply = [](const ampify::audio::Affine& m, float x, float y) {
        return std::pair{x * m.m11 + y * m.m21 + m.dx, x * m.m12 + y * m.m22 + m.dy};
    };
    for (size_t i = 0; i < count; i++) {
        float level = 0.5f;
        ampify::audio::Affine m = ampify::audio::RingBar(ring, i, count, level);
        auto [footX, footY] = apply(m, ring.thickness / 2, ring.length);
        auto [tipX, tipY] = apply(m, ring.thickness / 2, 0);
        float foot = std::hypot(footX - ring.centreX, footY - ring.centreY);
        float tip = std::hypot(tipX - ring.centreX, tipY - ring.centreY);
        CHECK(std::abs(foot - ring.radius) < 1e-3f);
        CHECK(std::abs(tip - (ring.radius + ring.length * level)) < 1e-3f);
    }
    // Bar 0 points straight up, bar 2 of 8 to the right (clockwise).
    auto [upX, upY] = apply(ampify::audio::RingBar(ring, 0, count, 1), ring.thickness / 2, 0);
    CHECK(std::abs(upX - 50) < 1e-3f && std::abs(upY) < 1e-3f);
    auto [rightX, rightY] = apply(ampify::audio::RingBar(ring, 2, count, 1), ring.thickness / 2, 0);
    CHECK(std::abs(rightX - 80) < 1e-3f && std::abs(rightY - 30) < 1e-3f);
}

TEST_CASE(BarShapes_TheLineRunsFromEdgeToEdgeInsideTheDecor) {
    std::vector<ampify::audio::Point> points;
    std::vector<float> levels{0.0f, 1.0f, 0.0f, 1.0f};
    ampify::audio::TraceLine(levels, 200, 40, 2, points);
    CHECK(points.size() > levels.size() * 3);  // a curve, not a zigzag
    CHECK(points.front().x == 0 && points.back().x == 200);
    for (size_t i = 0; i < points.size(); i++) {
        CHECK(points[i].y >= 2 && points[i].y <= 38);  // the curve's overshoot stays inside, past the stroke
        if (i) CHECK(points[i].x >= points[i - 1].x);
    }
    // A full bar reaches the top at its middle, an empty one the bottom.
    auto at = [&](float x) {
        return std::min_element(points.begin(), points.end(), [&](auto a, auto b) {
                   return std::abs(a.x - x) < std::abs(b.x - x);
               })->y;
    };
    CHECK(std::abs(at(75) - 2) < 1e-3f);   // bar 1, at (1 + 0.5) / 4 of the width
    CHECK(std::abs(at(25) - 38) < 1e-3f);  // bar 0

    std::vector<ampify::audio::Point> reused = points;
    ampify::audio::TraceLine(levels, 0, 40, 2, reused);
    CHECK(reused.empty());  // nothing laid out yet
}

TEST_CASE(BarMeter_AQuietSongStillFillsTheBars) {
    ampify::audio::BarMeter meter(2);
    std::vector<float> bands(16, -48.0f);
    for (int i = 0; i < 200; i++) meter.Update(&bands, 0.05);  // ten seconds of a quiet passage
    CHECK(meter.Levels()[0] > 0.8f);
}
