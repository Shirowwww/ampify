#include <cmath>
#include <numbers>
#include <vector>

#include "../extensions/atmos-leveler/level_file.hpp"
#include "../extensions/atmos-leveler/leveler.hpp"
#include "../extensions/atmos-leveler/loudness_meter.hpp"
#include "../extensions/atmos-leveler/object_pass.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

using ampify::test::SineSeconds;

double MeasureLufs(const std::vector<float>& samples, double sampleRate, int channels) {
    atmos::LoudnessMeter meter(sampleRate, channels);
    meter.Process(samples.data(), samples.size() / channels);
    return meter.IntegratedLufs().value_or(-200.0);
}

// A 1 kHz tone, scaled so that its integrated loudness is exactly `lufs`. Scaling by a factor is
// a fixed dB shift, so this does not depend on the absolute calibration of the meter.
std::vector<float> ToneAtLufs(double sampleRate, int channels, double seconds, double lufs) {
    std::vector<float> samples = SineSeconds(sampleRate, channels, seconds, 1000.0, 1.0);
    double shiftDb = lufs - MeasureLufs(samples, sampleRate, channels);
    float factor = static_cast<float>(atmos::DbToLinear(shiftDb));
    for (float& sample : samples) sample *= factor;
    return samples;
}

// Atmos as the spatial objects carry it: two mono objects (the channels of `stereo`), metered by an
// ObjectPass at `sampleRate`, then handed to the leveler in passes of 10 ms.
void FeedObjects(atmos::AtmosLeveler& leveler, const std::vector<float>& stereo, double sampleRate) {
    size_t frames = stereo.size() / 2;
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    for (size_t frame = 0; frame < frames; frame++) {
        left[frame] = stereo[frame * 2];
        right[frame] = stereo[frame * 2 + 1];
    }

    atmos::ObjectPass pass(sampleRate);
    const size_t passFrames = static_cast<size_t>(sampleRate / 100);
    for (size_t start = 0; start + passFrames <= frames; start += passFrames) {
        std::vector<atmos::ObjectBuffer> buffers = {{&left, left.data() + start}, {&right, right.data() + start}};
        const atmos::PassLevels& levels = pass.Process(buffers, passFrames, 1.0);
        leveler.FeedPower(levels.framePower, levels.peak);
    }
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
    std::vector<float> tone = SineSeconds(48000, 1, 10.0, 1000.0, amplitude);
    CHECK_NEAR(MeasureLufs(tone, 48000, 1), -23.0, 0.1);
}

TEST_CASE(Meter_DoublingTheAmplitudeAddsSixDecibels) {
    double amplitude = 0.1;
    std::vector<float> quiet = SineSeconds(48000, 2, 10.0, 1000.0, amplitude);
    std::vector<float> loud = SineSeconds(48000, 2, 10.0, 1000.0, amplitude * 2.0);
    CHECK_NEAR(MeasureLufs(loud, 48000, 2) - MeasureLufs(quiet, 48000, 2), 6.0206, 0.01);
}

TEST_CASE(Meter_WeightingFollowsTheEarShape) {
    // Deep bass is filtered out by the RLB high-pass, and the head-related shelf adds about 4 dB
    // at high frequencies on top of the 1 kHz reference.
    double amplitude = 0.1;
    double at1k = MeasureLufs(SineSeconds(48000, 1, 10.0, 1000.0, amplitude), 48000, 1);
    double at10 = MeasureLufs(SineSeconds(48000, 1, 10.0, 10.0, amplitude), 48000, 1);
    double at10k = MeasureLufs(SineSeconds(48000, 1, 10.0, 10000.0, amplitude), 48000, 1);

    CHECK(at10 - at1k < -15.0);
    CHECK(at10k - at1k > 2.5);
    CHECK(at10k - at1k < 4.5);
}

TEST_CASE(Meter_SameLoudnessAtAnySampleRate) {
    // Same tone, same amplitude, two rates: the K-weighting must be re-derived for each rate.
    double at48 = MeasureLufs(SineSeconds(48000, 2, 10.0, 1000.0, 0.1), 48000, 2);
    double at96 = MeasureLufs(SineSeconds(96000, 2, 10.0, 1000.0, 0.1), 96000, 2);
    CHECK_NEAR(at96, at48, 0.05);
}

TEST_CASE(Meter_GatingIgnoresQuietPassages) {
    // Half loud, half far below the relative gate: only the loud half counts.
    std::vector<float> loud = SineSeconds(48000, 1, 10.0, 1000.0, std::sqrt(2.0) * std::pow(10.0, -23.0 / 20.0));
    std::vector<float> quiet = SineSeconds(48000, 1, 10.0, 1000.0, std::sqrt(2.0) * std::pow(10.0, -50.0 / 20.0));
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
    atmos::AtmosLeveler leveler(config, kRate, kRate);

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
    atmos::AtmosLeveler leveler(config, kRate, kRate);

    // The slider sits at half: the stereo stream carries it, so the leveler knows it. The spatial objects come
    // before the slider: they are as loud at any volume, and are measured as they are.
    std::vector<float> tone = ToneAtLufs(kRate, 2, 40.0, -20.0);
    std::vector<float> silence(static_cast<size_t>(kRate) * 2, 0.0f);

    leveler.StartTrack("volume-check", true);
    leveler.Feed(silence.data(), silence.size() / 2, 0.5f);
    FeedObjects(leveler, tone, kRate);
    leveler.StartTrack("next", false);  // ends the track, which stores its level

    std::optional<atmos::StoredLevel> stored = leveler.Store().Find("volume-check");
    CHECK(stored.has_value());
    if (stored) CHECK_NEAR(stored->lufs, -20.0, 0.05);

    // The stereo stream comes after the slider: at half volume its samples are half as loud, and the
    // leveler measures them as if the slider were at full.
    std::vector<float> stereo = ToneAtLufs(kRate, 2, 40.0, -16.0);
    for (float& sample : stereo) sample *= 0.5f;
    leveler.Feed(stereo.data(), stereo.size() / 2, 0.5f);
    leveler.EndTrack();
    CHECK(leveler.StereoHistory().Size() == 1);
    CHECK_NEAR(leveler.StereoHistory().Values().back(), -16.0, 0.05);
}

TEST_CASE(Leveler_AtmosPeakCeilingUsesTheRawObjectPeak) {
    // Objects come before the slider, so the gain goes into buffers without the volume: their own peak
    // decides the ceiling, whatever the slider says.
    constexpr double kRate = 48000;
    atmos::LevelerConfig config;
    config.settleSeconds = 0;
    atmos::AtmosLeveler leveler(config, kRate, kRate);
    for (const char* id : {"stereo-1", "stereo-2"}) {
        leveler.StartTrack(id, false);
        std::vector<float> tone = ToneAtLufs(kRate, 2, 40.0, -10.0);
        leveler.Feed(tone.data(), tone.size() / 2, 1.0f);
    }

    leveler.StartTrack("peaky", true);
    std::vector<float> silence(static_cast<size_t>(kRate) * 2, 0.0f);
    leveler.Feed(silence.data(), silence.size() / 2, 0.25f);  // the slider at a quarter
    // A quiet track (about -25 LUFS, 15 dB under the target) with one peak at 0.9.
    std::vector<float> tone = SineSeconds(kRate, 2, 12.0, 1000.0, 0.05);
    tone[48000 * 2] = 0.9f;
    FeedObjects(leveler, tone, kRate);

    // 0.95 / 0.9 is about 0.47 dB. Counting the slider (0.9 * 0.25) would allow 12.5 dB.
    double gainDb = 0.0;
    for (int poll = 0; poll < 20; poll++) gainDb = leveler.Poll(gainDb);
    CHECK_NEAR(gainDb, 20.0 * std::log10(0.95 / 0.9), 0.01);
}

TEST_CASE(Leveler_StereoAt96kAndAtmosAt48kAreBothMeasured) {
    // A typical output: the stereo stream at 96 kHz, the Atmos objects at 48 kHz. Each path keeps its own rate.
    atmos::LevelerConfig config;
    atmos::AtmosLeveler leveler(config, 96000, 48000);
    CHECK(leveler.StreamRate() == 96000);
    CHECK(leveler.ObjectRate() == 48000);

    for (const char* id : {"stereo-1", "stereo-2"}) {
        leveler.StartTrack(id, false);
        std::vector<float> tone = ToneAtLufs(96000, 2, 40.0, -16.0);
        leveler.Feed(tone.data(), tone.size() / 2, 1.0f);
    }
    leveler.StartTrack("atmos", true);
    CHECK(leveler.StereoHistory().Size() == 2);
    CHECK_NEAR(leveler.StereoHistory().Target(2).value_or(0.0), -16.0, 0.05);

    // While Atmos plays, the stereo stream may go on with silence: it must not hide the objects.
    std::vector<float> silence(96000 * 2 * 4, 0.0f);
    leveler.Feed(silence.data(), silence.size() / 2, 1.0f);
    FeedObjects(leveler, ToneAtLufs(48000, 2, 40.0, -20.0), 48000);
    CHECK(leveler.MeasuredLufs().has_value());
    CHECK_NEAR(leveler.MeasuredLufs().value_or(0.0), -20.0, 0.05);

    double gainDb = 0.0;
    for (int poll = 0; poll < 40; poll++) gainDb = leveler.Poll(gainDb);
    CHECK_NEAR(gainDb, 4.0, 0.1);

    leveler.EndTrack();
    std::optional<atmos::FinishedTrack> finished = leveler.TakeFinished();
    CHECK(finished && finished->path == atmos::AudioPath::Objects);
    CHECK(finished && finished->stored);
    CHECK_NEAR(leveler.Store().Find("atmos").value_or(atmos::StoredLevel{}).lufs, -20.0, 0.05);
}

TEST_CASE(Leveler_DefaultHistoryKeepsThirtyStereoTracks) {
    atmos::LevelerConfig config;
    atmos::AtmosLeveler leveler(config);
    CHECK(leveler.StereoHistory().Capacity() == 30);

    std::vector<double> values;
    for (int i = 0; i < 40; i++) values.push_back(-10.0 - i);
    leveler.Restore(atmos::VariantStore{}, values);
    CHECK(leveler.StereoHistory().Size() == 30);
    CHECK_NEAR(leveler.StereoHistory().Values().front(), -20.0, 1e-12);  // the ten oldest dropped out
}

TEST_CASE(Leveler_EmptyVariantIsNeverStored) {
    atmos::LevelerConfig config;
    config.settleSeconds = 0;
    config.minSecondsToStore = 10.0;
    atmos::AtmosLeveler leveler(config);

    leveler.StartTrack("", true);
    FeedObjects(leveler, ToneAtLufs(48000, 2, 12.0, -20.0), 48000);
    leveler.EndTrack();

    std::optional<atmos::FinishedTrack> finished = leveler.TakeFinished();
    CHECK(finished.has_value());  // measured, for the journal
    CHECK(finished && !finished->stored);
    CHECK(leveler.Store().Levels().empty());
}

TEST_CASE(LevelFile_RoundTrip) {
    atmos::VariantStore store;
    store.Put("abc123", {-19.875, 212.5});
    store.Put("def456", {-21.25, 31.0});
    atmos::TargetHistory history(30);
    history.Remember(-16.0);
    history.Remember(-15.5);
    history.Remember(-16.125);

    atmos::SavedLevels saved = atmos::ParseLevels(atmos::FormatLevels(store, history));
    CHECK(saved.atmos.Levels().size() == 2);
    CHECK_NEAR(saved.atmos.Find("abc123").value_or(atmos::StoredLevel{}).lufs, -19.875, 1e-9);
    CHECK_NEAR(saved.atmos.Find("abc123").value_or(atmos::StoredLevel{}).seconds, 212.5, 1e-9);
    CHECK_NEAR(saved.atmos.Find("def456").value_or(atmos::StoredLevel{}).lufs, -21.25, 1e-9);
    CHECK(saved.stereo == std::vector<double>({-16.0, -15.5, -16.125}));

    // Into a fresh leveler: the stored variant levels straight away, against the saved target (-16, the median).
    atmos::LevelerConfig config;
    atmos::AtmosLeveler leveler(config);
    leveler.Restore(saved.atmos, saved.stereo);
    leveler.StartTrack("abc123", true);
    CHECK_NEAR(leveler.Poll(0.0), -16.0 + 19.875, 1e-9);
}

TEST_CASE(LevelFile_SkipsLinesItCannotRead) {
    atmos::SavedLevels saved = atmos::ParseLevels(
        "# comment\n"
        "atmos good -20.5 40.0\r\n"
        "atmos short -20.5\n"
        "atmos bad x 40\n"
        "stereo -16,0\n"
        "stereo -15.0\n"
        "atmos good -18.0 30.0\n"  // shorter than the first: the first one stays
        "unknown line\n");
    CHECK(saved.atmos.Levels().size() == 1);
    CHECK_NEAR(saved.atmos.Find("good").value_or(atmos::StoredLevel{}).lufs, -20.5, 1e-12);
    CHECK(saved.stereo == std::vector<double>({-15.0}));
    CHECK(atmos::ParseLevels("").atmos.Levels().empty());
}

TEST_CASE(Config_GainsAreClampedToTheirRange) {
    atmos::LevelerConfig config;
    config.fallbackGainDb = 40.0;
    config.maxGainDb = -3.0;
    atmos::LevelerConfig clamped = atmos::ClampGains(config);
    CHECK_NEAR(clamped.fallbackGainDb, 15.0, 1e-12);
    CHECK_NEAR(clamped.maxGainDb, 0.0, 1e-12);

    config.fallbackGainDb = 7.5;
    config.maxGainDb = std::nan("");
    clamped = atmos::ClampGains(config);
    CHECK_NEAR(clamped.fallbackGainDb, 7.5, 1e-12);
    CHECK_NEAR(clamped.maxGainDb, atmos::LevelerConfig{}.maxGainDb, 1e-12);
}

TEST_CASE(Leveler_StereoTracksAreNotBoosted) {
    atmos::LevelerConfig config;
    atmos::AtmosLeveler leveler(config);
    leveler.StartTrack("stereo", false);
    CHECK(!leveler.IsAtmos());
    CHECK_NEAR(leveler.Poll(2.0), 2.0, 1e-12);
}
