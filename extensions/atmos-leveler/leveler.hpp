#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/audio/sample_stage.hpp"
#include "loudness_meter.hpp"

namespace atmos {

// Highest gain the settings may ask for, in dB: the leveler's own cap, apart from the elements' audio.maxGainDb.
constexpr double kGainLimitDb = 15.0;

// Tuning of the Atmos leveler. Unmeasured: every default is the value of the Windhawk reference mod, and no listen
// test on a real Dolby Atmos track has confirmed them yet (docs/development/testing.md).
struct LevelerConfig {
    bool autoLevel = true;
    double fallbackGainDb = 5.0;          // used until the level is known, or when auto-levelling is off
    double maxGainDb = kGainLimitDb;      // never boost an Atmos track by more than this
    double peakCeiling = 0.95;            // linear: keeps measured peaks under about -0.5 dBFS
    double slewDbPerPoll = 0.15;          // largest change per Poll while converging: 0.5 dB/s at one Poll per 300 ms
    double settleSeconds = 8.0;           // skipped at the start of each track (crossfade)
    double minSecondsToLevel = 10.0;      // measured audio needed before the gain follows the track
    double minSecondsToStore = 30.0;      // measured audio needed before a track is remembered
    size_t stereoHistory = 30;            // stereo tracks kept for the target
    size_t minStereoTracks = 2;           // stereo tracks needed before the target is trusted
};

// The config with fallbackGainDb and maxGainDb brought into 0..kGainLimitDb. A value that is not a number
// takes its default back.
LevelerConfig ClampGains(LevelerConfig config);

double DbToLinear(double db);
double LinearToDb(double linear);

// Recent stereo tracks, already levelled by Sound Check. Their median is the level
// Atmos tracks are matched to.
class TargetHistory {
public:
    explicit TargetHistory(size_t capacity);

    void Remember(double lufs);

    // Empty until `minTracks` stereo tracks have been remembered. For an even count, returns the upper median.
    std::optional<double> Target(size_t minTracks) const;

    size_t Size() const;
    size_t Capacity() const;

    // Oldest first, as they were remembered.
    const std::vector<double>& Values() const;

private:
    size_t m_capacity;
    std::vector<double> m_values;  // oldest first
};

struct StoredLevel {
    double lufs = 0;
    double seconds = 0;  // audible seconds the measurement is based on
};

// Measured loudness of Atmos tracks, by stable variant id. A longer measurement replaces a shorter one.
class VariantStore {
public:
    std::optional<StoredLevel> Find(const std::string& variantId) const;

    // Returns false when an existing measurement is at least as long, so nothing changed.
    bool Put(const std::string& variantId, StoredLevel level);

    const std::map<std::string, StoredLevel>& Levels() const;

private:
    std::map<std::string, StoredLevel> m_levels;
};

// Where the decoded audio reaches the leveler. Each path has its own meter at its own sample rate, so a
// stereo stream at 96 kHz and Atmos objects at 48 kHz are both measured.
enum class AudioPath {
    Stream,   // IAudioRenderClient: interleaved stereo, after the player volume. Measures stereo tracks.
    Objects,  // spatial audio objects: mono buffers, before the slider volume. Measures Atmos tracks.
};

// A track whose loudness the leveler has measured, for the log.
struct FinishedTrack {
    bool atmos = false;
    double lufs = 0;
    double seconds = 0;
    AudioPath path = AudioPath::Stream;  // the meter the loudness comes from
    bool stored = false;                 // it changed the VariantStore or the TargetHistory: worth saving
};

// What the gain policy knows about the track that is playing.
struct TrackLevel {
    std::optional<double> storedLufs;    // known before the track is heard, from the VariantStore
    std::optional<double> measuredLufs;  // from the meter, once minSecondsToLevel has been measured
    bool audible = false;                // audio has passed the settle period
    double peak = 0;                     // linear peak, without the player volume
    double userVolume = 1;               // player volume already in the buffers the gain goes into: the
                                         // slider for the stereo stream, 1 for spatial objects (before it)
};

// The Atmos gain (dB) the track should have now. Pure: the caller keeps `currentDb` and calls
// this about every 300 ms (slewDbPerPoll). `target` is the stereo level from TargetHistory, if known.
double NextGainDb(const LevelerConfig& config, std::optional<double> target, const TrackLevel& track,
                  double currentDb);

// Follows one playing item at a time: meters the decoded samples and remembers the level of
// each track it heard for long enough. Not thread-safe: the caller serializes calls.
//
// A track is measured on the meter of its kind: the stream for stereo, the objects for Atmos. When that
// meter has nothing and the other one has, the other one is used (an Atmos track a device without spatial
// audio gets as a stereo stream, say).
class AtmosLeveler {
public:
    explicit AtmosLeveler(const LevelerConfig& config, double streamRate = ampify::audio::kStartSampleRate,
                          double objectRate = ampify::audio::kStartSampleRate);

    // Ends the previous track, storing it if it was measured long enough, then starts this one.
    // An Atmos track with an empty variant id is measured but never stored.
    void StartTrack(const std::string& variantId, bool atmos);

    // Ends the current track, as StartTrack does, without starting another: what plays next is not known yet.
    void EndTrack();

    // Decoded stereo samples from the render stream, with the player volume applied, as Apple Music hands
    // them to the audio device. Measured as if the slider were at full.
    void Feed(const float* interleaved, size_t frames, float volume);

    // Spatial audio objects, from ObjectPass: per-frame power of all objects and their peak, both before
    // the gain. The objects are taken before the slider volume, so nothing is divided out.
    void FeedPower(const std::vector<double>& framePower, float peak);

    // Rate of each path. A change rebuilds that meter's filters only: the track goes on, and what was
    // measured is kept.
    void SetStreamRate(double sampleRate);
    void SetObjectRate(double sampleRate);
    double StreamRate() const;
    double ObjectRate() const;

    // The Atmos gain (dB) for the current track. For a stereo track the value passed in comes back
    // unchanged: check IsAtmos() and apply 0 dB to stereo tracks.
    double Poll(double currentDb);

    bool IsAtmos() const;

    // Loudness of the current track so far. Empty until enough audio has been measured.
    std::optional<double> MeasuredLufs() const;

    // The last track that ended with a measured loudness. Returned once, then forgotten.
    std::optional<FinishedTrack> TakeFinished();

    // Puts back what an earlier run saved. The history keeps its capacity: only the newest values stay.
    void Restore(const VariantStore& store, const std::vector<double>& stereo);

    const TargetHistory& StereoHistory() const;
    const VariantStore& Store() const;

private:
    struct Path {
        LoudnessMeter meter;
        double settleLeft = 0;  // seconds still to skip at the start of the track
        bool audible = false;   // audio has passed the settle period

        Path(double sampleRate, int channels) : meter(sampleRate, channels) {}
        void Start(double settleSeconds);
        // How many of `frames` frames fall in the settle period. Counts them as skipped.
        size_t Settle(size_t frames);
    };

    void FinishTrack();
    // The path the current track is measured on.
    const Path& Measuring() const;
    AudioPath MeasuringPath() const;

    LevelerConfig m_config;
    TargetHistory m_stereo;
    VariantStore m_store;

    Path m_stream;
    Path m_objects;
    std::string m_variantId;
    bool m_active = false;
    bool m_atmos = false;
    std::optional<StoredLevel> m_stored;
    float m_volume = 1.0f;  // last player volume seen with the stream
    std::optional<FinishedTrack> m_finished;
};

}  // namespace atmos
