#include <cmath>
#include <numbers>
#include <vector>

#include "../extensions/atmos-leveler/leveler.hpp"
#include "../extensions/atmos-leveler/loudness_meter.hpp"
#include "test.hpp"

namespace {

// Interleaved sine, the same on every channel.
std::vector<float> Sine(double sampleRate, int channels, double seconds, double frequency, double amplitude) {
    size_t frames = static_cast<size_t>(sampleRate * seconds);
    std::vector<float> samples(frames * channels);
    for (size_t frame = 0; frame < frames; frame++) {
        double value = amplitude * std::sin(2.0 * std::numbers::pi * frequency * frame / sampleRate);
        for (int channel = 0; channel < channels; channel++) {
            samples[frame * channels + channel] = static_cast<float>(value);
        }
    }
    return samples;
}

double MeasureLufs(const std::vector<float>& samples, double sampleRate, int channels) {
    atmos::LoudnessMeter meter(sampleRate, channels);
    meter.Process(samples.data(), samples.size() / channels);
    return meter.IntegratedLufs().value_or(-200.0);
}

// A 1 kHz tone, scaled so that its integrated loudness is exactly `lufs`. Scaling by a factor is
// a fixed dB shift, so this does not depend on the absolute calibration of the meter.
std::vector<float> ToneAtLufs(double sampleRate, int channels, double seconds, double lufs) {
    std::vector<float> samples = Sine(sampleRate, channels, seconds, 1000.0, 1.0);
    double shiftDb = lufs - MeasureLufs(samples, sampleRate, channels);
    float factor = static_cast<float>(atmos::DbToLinear(shiftDb));
    for (float& sample : samples) sample *= factor;
    return samples;
}

}  // namespace

TEST_CASE(Meter_SilenceHasNoLoudness) {
    std::vector<float> silence(48000 * 2 * 5, 0.0f);
    atmos::LoudnessMeter meter(48000, 2);
    meter.Process(silence.data(), silence.size() / 2);
    CHECK(!meter.IntegratedLufs().has_value());
}

TEST_CASE(Meter_CalibrationOneKiloHertzMonoAtMinus23) {
    // A mono 1 kHz sine with an RMS of -23 dBFS is -23 LUFS (EBU Tech 3341 reference tone).
    double amplitude = std::sqrt(2.0) * std::pow(10.0, -23.0 / 20.0);
    std::vector<float> tone = Sine(48000, 1, 10.0, 1000.0, amplitude);
    CHECK_NEAR(MeasureLufs(tone, 48000, 1), -23.0, 0.1);
}

TEST_CASE(Meter_DoublingTheAmplitudeAddsSixDecibels) {
    double amplitude = 0.1;
    std::vector<float> quiet = Sine(48000, 2, 10.0, 1000.0, amplitude);
    std::vector<float> loud = Sine(48000, 2, 10.0, 1000.0, amplitude * 2.0);
    CHECK_NEAR(MeasureLufs(loud, 48000, 2) - MeasureLufs(quiet, 48000, 2), 6.0206, 0.01);
}

TEST_CASE(Meter_WeightingFollowsTheEarShape) {
    // Deep bass is filtered out by the RLB high-pass, and the head-related shelf adds about 4 dB
    // at high frequencies on top of the 1 kHz reference.
    double amplitude = 0.1;
    double at1k = MeasureLufs(Sine(48000, 1, 10.0, 1000.0, amplitude), 48000, 1);
    double at10 = MeasureLufs(Sine(48000, 1, 10.0, 10.0, amplitude), 48000, 1);
    double at10k = MeasureLufs(Sine(48000, 1, 10.0, 10000.0, amplitude), 48000, 1);

    CHECK(at10 - at1k < -15.0);
    CHECK(at10k - at1k > 2.5);
    CHECK(at10k - at1k < 4.5);
}

TEST_CASE(Meter_SameLoudnessAtAnySampleRate) {
    // Same tone, same amplitude, two rates: the K-weighting must be re-derived for each rate.
    double at48 = MeasureLufs(Sine(48000, 2, 10.0, 1000.0, 0.1), 48000, 2);
    double at96 = MeasureLufs(Sine(96000, 2, 10.0, 1000.0, 0.1), 96000, 2);
    CHECK_NEAR(at96, at48, 0.05);
}

TEST_CASE(Meter_GatingIgnoresQuietPassages) {
    // Half loud, half far below the relative gate: only the loud half counts.
    std::vector<float> loud = Sine(48000, 1, 10.0, 1000.0, std::sqrt(2.0) * std::pow(10.0, -23.0 / 20.0));
    std::vector<float> quiet = Sine(48000, 1, 10.0, 1000.0, std::sqrt(2.0) * std::pow(10.0, -50.0 / 20.0));
    std::vector<float> both = loud;
    both.insert(both.end(), quiet.begin(), quiet.end());
    CHECK_NEAR(MeasureLufs(both, 48000, 1), -23.0, 0.1);
}

TEST_CASE(Meter_PeakIsTakenAfterGain) {
    std::vector<float> samples(480, 0.5f);
    atmos::LoudnessMeter meter(48000, 1);
    meter.Process(samples.data(), samples.size(), 2.0f);
    CHECK_NEAR(meter.Peak(), 1.0, 1e-6);
}

TEST_CASE(TargetHistory_UsesTheMedianOfRecentStereoTracks) {
    atmos::TargetHistory history(3);
    CHECK(!history.Target(2).has_value());

    history.Remember(-14.0);
    history.Remember(-18.0);
    CHECK(!history.Target(3).has_value());
    CHECK_NEAR(*history.Target(2), -14.0, 1e-12);  // even count: upper median

    history.Remember(-16.0);
    CHECK_NEAR(*history.Target(3), -16.0, 1e-12);

    history.Remember(-20.0);  // capacity 3: the oldest (-14) drops out
    CHECK(history.Size() == 3);
    CHECK_NEAR(*history.Target(3), -18.0, 1e-12);
}

TEST_CASE(VariantStore_KeepsTheLongerMeasurement) {
    atmos::VariantStore store;
    CHECK(store.Put("variant", {-20.0, 10.0}));
    CHECK(!store.Put("variant", {-19.0, 5.0}));
    CHECK_NEAR(store.Find("variant")->lufs, -20.0, 1e-12);
    CHECK(store.Put("variant", {-21.0, 30.0}));
    CHECK_NEAR(store.Find("variant")->lufs, -21.0, 1e-12);
    CHECK(!store.Find("unknown").has_value());
}

TEST_CASE(Gain_FallsBackWithoutATarget) {
    atmos::LevelerConfig config;
    atmos::TrackLevel track;
    track.measuredLufs = -30.0;
    track.audible = true;

    CHECK_NEAR(atmos::NextGainDb(config, std::nullopt, track, 0.0), 5.0, 1e-12);

    config.autoLevel = false;
    CHECK_NEAR(atmos::NextGainDb(config, -16.0, track, 0.0), 5.0, 1e-12);
}

TEST_CASE(Gain_BoostsToTheTargetAndNoFurther) {
    atmos::LevelerConfig config;
    atmos::TrackLevel quiet;
    quiet.storedLufs = -20.0;
    CHECK_NEAR(atmos::NextGainDb(config, -16.0, quiet, 0.0), 4.0, 1e-12);

    atmos::TrackLevel verySoft;
    verySoft.storedLufs = -40.0;
    CHECK_NEAR(atmos::NextGainDb(config, -16.0, verySoft, 0.0), 15.0, 1e-12);  // clamped to maxGainDb

    atmos::TrackLevel loud;
    loud.storedLufs = -12.0;
    CHECK_NEAR(atmos::NextGainDb(config, -16.0, loud, 0.0), 0.0, 1e-12);  // never attenuates
}

TEST_CASE(Gain_RespectsThePeakCeiling) {
    atmos::LevelerConfig config;
    atmos::TrackLevel track;
    track.storedLufs = -20.0;
    track.peak = 0.9;
    track.userVolume = 1.0;

    // 0.95 / 0.9 is about 0.47 dB: the peak would otherwise clip after the 4 dB boost.
    CHECK_NEAR(atmos::NextGainDb(config, -16.0, track, 0.0), 20.0 * std::log10(0.95 / 0.9), 1e-9);
}

TEST_CASE(Gain_EasesInOnceTheTrackIsBeingHeard) {
    atmos::LevelerConfig config;
    atmos::TrackLevel track;
    track.measuredLufs = -20.0;
    track.audible = true;
    track.peak = 0.1;
    track.userVolume = 1.0;

    // The target is 4 dB above, but one poll moves the gain by at most the slew step.
    CHECK_NEAR(atmos::NextGainDb(config, -16.0, track, 0.0), config.slewDbPerPoll, 1e-12);
}

TEST_CASE(Gain_KeepsCurrentGainWhileNothingIsKnown) {
    atmos::LevelerConfig config;
    atmos::TrackLevel unknown;
    CHECK_NEAR(atmos::NextGainDb(config, -16.0, unknown, 3.25), 3.25, 1e-12);
}

TEST_CASE(Leveler_LevelsAnAtmosTrackToTheStereoTarget) {
    constexpr double kRate = 48000;
    atmos::LevelerConfig config;
    atmos::AtmosLeveler leveler(config, kRate, 2);

    // Two stereo tracks at -16 LUFS set the target.
    for (const char* id : {"stereo-1", "stereo-2"}) {
        leveler.StartTrack(id, false);
        std::vector<float> tone = ToneAtLufs(kRate, 2, 40.0, -16.0);
        leveler.Feed(tone.data(), tone.size() / 2, 1.0f);
    }
    leveler.StartTrack("atmos-a", true);
    CHECK(leveler.StereoHistory().Size() == 2);
    CHECK_NEAR(*leveler.StereoHistory().Target(2), -16.0, 0.05);

    // An Atmos track at -20 LUFS should end up 4 dB up, reached gradually.
    std::vector<float> atmosTone = ToneAtLufs(kRate, 2, 40.0, -20.0);
    const size_t chunkFrames = static_cast<size_t>(kRate * 0.3);
    double gainDb = 0.0;
    for (size_t frame = 0; frame + chunkFrames <= atmosTone.size() / 2; frame += chunkFrames) {
        leveler.Feed(atmosTone.data() + frame * 2, chunkFrames, 1.0f);
        gainDb = leveler.Poll(gainDb);
    }
    CHECK(leveler.IsAtmos());
    CHECK_NEAR(gainDb, 4.0, 0.2);

    // Replaying it: the stored level applies straight away, before any audio is heard.
    leveler.StartTrack("atmos-b", false);
    leveler.StartTrack("atmos-a", true);
    CHECK_NEAR(leveler.Poll(0.0), 4.0, 0.05);
}

TEST_CASE(Leveler_LoudnessDoesNotDependOnTheSliderVolume) {
    constexpr double kRate = 48000;
    atmos::LevelerConfig config;
    atmos::AtmosLeveler leveler(config, kRate, 2);

    // Apple Music applies the slider to the samples. At half volume they are half as loud, but
    // the leveler measures them as if the slider were at full, so the stored level is the same.
    std::vector<float> tone = ToneAtLufs(kRate, 2, 40.0, -20.0);
    for (float& sample : tone) sample *= 0.5f;

    leveler.StartTrack("volume-check", true);
    leveler.Feed(tone.data(), tone.size() / 2, 0.5f);
    leveler.StartTrack("next", false);  // ends the track, which stores its level

    std::optional<atmos::StoredLevel> stored = leveler.Store().Find("volume-check");
    CHECK(stored.has_value());
    if (stored) CHECK_NEAR(stored->lufs, -20.0, 0.05);
}

TEST_CASE(Leveler_StereoTracksAreNotBoosted) {
    atmos::LevelerConfig config;
    atmos::AtmosLeveler leveler(config, 48000, 2);
    leveler.StartTrack("stereo", false);
    CHECK(!leveler.IsAtmos());
    CHECK_NEAR(leveler.Poll(2.0), 2.0, 1e-12);
}
