#include <cmath>
#include <numbers>
#include <span>
#include <vector>

#include "../extensions/atmos-leveler/format_detection.hpp"
#include "../extensions/atmos-leveler/gain_ramp.hpp"
#include "../extensions/atmos-leveler/leveler.hpp"
#include "../extensions/atmos-leveler/loudness_meter.hpp"
#include "../extensions/atmos-leveler/object_pass.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

using ampify::test::SineSeconds;
using ampify::test::Stereo;

// Two object buffers that carry the left and right halves of a stereo signal.
struct TwoObjects {
    std::vector<float> left;
    std::vector<float> right;

    std::vector<atmos::ObjectBuffer> Buffers() {
        return {{&left, left.data()}, {&right, right.data()}};
    }
};

}  // namespace

TEST_CASE(GainRamp_ConvergesOnTheTarget) {
    atmos::GainRamp ramp(48000);
    std::vector<float> curve;
    ramp.Curve(48000 * 2, 2.0, curve);
    CHECK_NEAR(curve.back(), 2.0, 1e-3);
}

TEST_CASE(GainRamp_EasesInsteadOfJumping) {
    atmos::GainRamp ramp(48000);
    std::vector<float> curve;
    CHECK(!ramp.Curve(4800, 2.0, curve));
    CHECK(curve.front() > 1.0f && curve.front() < 1.01f);
    for (size_t frame = 1; frame < curve.size(); frame++) CHECK(curve[frame] >= curve[frame - 1]);
    CHECK(curve.back() < 2.0f);
}

TEST_CASE(GainRamp_UnityLeavesTheSamplesAlone) {
    atmos::GainRamp ramp(48000);
    std::vector<float> curve;
    CHECK(ramp.Curve(1000, 1.0, curve));
}

TEST_CASE(GainRamp_KneeKeepsTheOutputUnderFullScale) {
    CHECK_NEAR(atmos::GainRamp::Limit(0.5f), 0.5, 1e-7);
    CHECK_NEAR(atmos::GainRamp::Limit(-0.5f), -0.5, 1e-7);
    CHECK_NEAR(atmos::GainRamp::Limit(0.94f), 0.94, 1e-6);  // continuous at the knee
    CHECK(atmos::GainRamp::Limit(10.0f) <= 1.0f);
    CHECK(atmos::GainRamp::Limit(10.0f) > 0.99f);
    CHECK(atmos::GainRamp::Limit(-10.0f) >= -1.0f);
    CHECK(atmos::GainRamp::Limit(-10.0f) < -0.99f);
}

TEST_CASE(ApplyCurve_GainsEachFrameOfInterleavedSamples) {
    std::vector<float> samples = {0.1f, -0.1f, 0.2f, -0.2f};  // two stereo frames
    std::vector<float> curve = {2.0f, 0.5f};
    atmos::ApplyCurve(samples.data(), 2, 2, curve);
    CHECK_NEAR(samples[0], 0.2, 1e-6);
    CHECK_NEAR(samples[1], -0.2, 1e-6);
    CHECK_NEAR(samples[2], 0.1, 1e-6);
    CHECK_NEAR(samples[3], -0.1, 1e-6);
}

TEST_CASE(ObjectPass_MeasuresLikeAStereoMeter) {
    // Objects carry L and R of the same signal: BS.1770 sums the channel powers in both cases.
    std::vector<float> mono = SineSeconds(48000, 1, 10.0, 1000.0, 0.1);
    std::vector<float> stereo = Stereo(mono);

    atmos::LoudnessMeter reference(48000, 2);
    reference.Process(stereo.data(), mono.size());

    TwoObjects objects{mono, mono};
    atmos::ObjectPass pass(48000);
    const atmos::PassLevels& levels = pass.Process(objects.Buffers(), mono.size(), 1.0);

    atmos::LoudnessMeter fromObjects(48000, 1);
    fromObjects.ProcessPower(levels.framePower.data(), levels.framePower.size(), 1.0, levels.peak);
    CHECK_NEAR(fromObjects.IntegratedLufs().value_or(-200.0), reference.IntegratedLufs().value_or(-200.0), 0.01);
}

TEST_CASE(ObjectPass_MeasuresBeforeTheGain) {
    std::vector<float> mono = SineSeconds(48000, 1, 2.0, 1000.0, 0.1);

    TwoObjects unity{mono, mono};
    atmos::ObjectPass unityPass(48000);
    std::vector<double> expected = unityPass.Process(unity.Buffers(), mono.size(), 1.0).framePower;

    TwoObjects boosted{mono, mono};
    atmos::ObjectPass boostedPass(48000);
    const atmos::PassLevels& levels = boostedPass.Process(boosted.Buffers(), mono.size(), 2.0);

    // The meter saw the samples as they were, so the boost does not show in the measurement.
    for (size_t frame = 0; frame < expected.size(); frame++) CHECK_NEAR(levels.framePower[frame], expected[frame], 1e-12);
}

TEST_CASE(ObjectPass_AppliesTheGainToTheBuffers) {
    std::vector<float> mono = SineSeconds(48000, 1, 2.0, 1000.0, 0.1);
    TwoObjects objects{mono, mono};
    atmos::ObjectPass pass(48000);
    pass.Process(objects.Buffers(), mono.size(), 2.0);

    // After two seconds the ramp has converged: the tail is doubled, and 0.2 is far below the knee.
    size_t last = mono.size() - 1;
    CHECK_NEAR(objects.left[last], 2.0 * mono[last], 1e-3);
}

TEST_CASE(ObjectPass_UnityLeavesTheBuffersBitExact) {
    std::vector<float> mono = SineSeconds(48000, 1, 0.5, 1000.0, 0.99);  // loud enough to reach the knee if it were applied
    std::vector<float> copy = mono;
    TwoObjects objects{mono, mono};
    atmos::ObjectPass pass(48000);
    pass.Process(objects.Buffers(), mono.size(), 1.0);
    CHECK(objects.left == copy);
}

TEST_CASE(ObjectPass_FilterStateCarriesAcrossPasses) {
    // The same signal in 100 passes of 10 ms gives the same per-frame power as one pass.
    std::vector<float> signal = SineSeconds(48000, 1, 1.0, 1000.0, 0.1);

    atmos::ObjectPass whole(48000);
    std::vector<float> wholeCopy = signal;
    atmos::ObjectBuffer wholeBuffer{&signal, wholeCopy.data()};
    std::vector<double> expected = whole.Process(std::span(&wholeBuffer, 1), wholeCopy.size(), 1.0).framePower;

    atmos::ObjectPass chunked(48000);
    std::vector<double> actual;
    for (size_t start = 0; start < signal.size(); start += 480) {
        std::vector<float> chunk(signal.begin() + start, signal.begin() + start + 480);
        atmos::ObjectBuffer buffer{&signal, chunk.data()};
        const atmos::PassLevels& levels = chunked.Process(std::span(&buffer, 1), chunk.size(), 1.0);
        actual.insert(actual.end(), levels.framePower.begin(), levels.framePower.end());
    }

    CHECK(actual.size() == expected.size());
    for (size_t frame = 0; frame < actual.size(); frame++) CHECK_NEAR(actual[frame], expected[frame], 1e-9);
}

TEST_CASE(Format_TellsAtmosFromStereo) {
    CHECK(atmos::ClassifyFormatDescription("mediaType:'soun' mediaSubType:'qec3'") == atmos::TrackKind::Atmos);
    CHECK(atmos::ClassifyFormatDescription("mediaType:'soun' mediaSubType:'qc+3' channels:16") == atmos::TrackKind::Atmos);
    CHECK(atmos::ClassifyFormatDescription("mediaType:'soun' mediaSubType:'qaac'") == atmos::TrackKind::Stereo);
    CHECK(atmos::ClassifyFormatDescription("mediaType:'soun' mediaSubType:'qlac'") == atmos::TrackKind::Stereo);
    CHECK(atmos::ClassifyFormatDescription("mediaType:'vide' mediaSubType:'avc1'") == atmos::TrackKind::Unknown);
    CHECK(atmos::ClassifyFormatDescription("") == atmos::TrackKind::Unknown);
}

TEST_CASE(Format_AnAtmosTrackMakesTheItemAtmos) {
    std::vector<std::string> mixed = {"mediaType:'vide'", "mediaType:'soun' mediaSubType:'qaac'",
                                      "mediaType:'soun' mediaSubType:'ec-3'"};
    CHECK(atmos::ClassifyDescriptions(mixed) == atmos::TrackKind::Atmos);
    CHECK(atmos::ClassifyDescriptions({"mediaType:'vide'"}) == atmos::TrackKind::Unknown);
}

TEST_CASE(Format_ReadsTheVariantIdBetweenTheQuotes) {
    CHECK(atmos::VariantIdFromDescription("<AVCFVariantID: contents = \"abc123\", kind = 2>") == "abc123");
    CHECK(atmos::VariantIdFromDescription("no variant here").empty());
    CHECK(atmos::VariantIdFromDescription("contents = \"unterminated").empty());
}

TEST_CASE(Leveler_PowerPathMeasuresLikeTheInterleavedPath) {
    // The stereo stream comes after the slider (0.5 here), the objects before it. Both paths must measure
    // the track as if the slider were at full.
    atmos::LevelerConfig config;
    config.settleSeconds = 0;
    const float volume = 0.5f;

    std::vector<float> mono = SineSeconds(48000, 1, 12.0, 1000.0, 0.1);
    std::vector<float> afterSlider = SineSeconds(48000, 1, 12.0, 1000.0, 0.1 * volume);
    std::vector<float> stereo = Stereo(afterSlider);

    atmos::AtmosLeveler interleaved(config);
    interleaved.StartTrack("variant", false);
    interleaved.Feed(stereo.data(), afterSlider.size(), volume);

    TwoObjects objects{mono, mono};
    atmos::ObjectPass pass(48000);
    const atmos::PassLevels& levels = pass.Process(objects.Buffers(), mono.size(), 1.0);

    atmos::AtmosLeveler fromObjects(config);
    fromObjects.StartTrack("variant", true);
    fromObjects.FeedPower(levels.framePower, levels.peak);

    CHECK(interleaved.MeasuredLufs().has_value());
    CHECK_NEAR(fromObjects.MeasuredLufs().value_or(-200.0), interleaved.MeasuredLufs().value_or(-200.0), 0.01);
}

TEST_CASE(Leveler_ReportsTheFinishedTrackOnce) {
    atmos::LevelerConfig config;
    config.settleSeconds = 0;
    std::vector<float> stereo = Stereo(SineSeconds(48000, 1, 12.0, 1000.0, 0.1));

    atmos::AtmosLeveler leveler(config);
    CHECK(!leveler.TakeFinished().has_value());
    leveler.StartTrack("first", true);
    leveler.Feed(stereo.data(), stereo.size() / 2, 1.0f);
    leveler.StartTrack("second", false);

    std::optional<atmos::FinishedTrack> finished = leveler.TakeFinished();
    CHECK(finished.has_value());
    CHECK(finished && finished->atmos);
    CHECK(finished && std::abs(finished->seconds - 12.0) < 0.01);
    CHECK(!leveler.TakeFinished().has_value());
}

TEST_CASE(Leveler_SampleRateChangeKeepsTheMeasurement) {
    // A stream reopened at another rate goes on measuring the same track: 6 s at 48 kHz, then 6 s at
    // 96 kHz, measure like 12 s of one stream.
    atmos::LevelerConfig config;
    config.settleSeconds = 0;
    config.minSecondsToStore = 10.0;
    std::vector<float> at48 = Stereo(SineSeconds(48000, 1, 6.0, 1000.0, 0.1));
    std::vector<float> at96 = Stereo(SineSeconds(96000, 1, 6.0, 1000.0, 0.1));

    atmos::AtmosLeveler leveler(config, 48000, 48000);
    leveler.StartTrack("stereo", false);
    leveler.Feed(at48.data(), at48.size() / 2, 1.0f);
    leveler.SetStreamRate(96000);
    leveler.Feed(at96.data(), at96.size() / 2, 1.0f);
    CHECK(leveler.StreamRate() == 96000);

    std::optional<double> measured = leveler.MeasuredLufs();
    atmos::LoudnessMeter reference(48000, 2);
    std::vector<float> whole = Stereo(SineSeconds(48000, 1, 12.0, 1000.0, 0.1));
    reference.Process(whole.data(), whole.size() / 2);
    CHECK(measured.has_value());
    CHECK_NEAR(measured.value_or(-200.0), reference.IntegratedLufs().value_or(0.0), 0.05);

    leveler.EndTrack();
    std::optional<atmos::FinishedTrack> finished = leveler.TakeFinished();
    CHECK(finished && finished->stored);
    CHECK(finished && std::abs(finished->seconds - 12.0) < 0.01);
    CHECK(leveler.StereoHistory().Size() == 1);
}
