#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "loudness_meter.hpp"

namespace atmos {

// Tuning of the Atmos leveler. Defaults are the values of the Windhawk reference mod.
struct LevelerConfig {
    bool autoLevel = true;
    double fallbackGainDb = 5.0;      // used until the level is known, or when auto-levelling is off
    double maxGainDb = 15.0;          // never boost an Atmos track by more than this
    double peakCeiling = 0.95;        // linear: keeps measured peaks under about -0.5 dBFS
    double slewDbPerPoll = 0.15;      // largest change per Poll while converging on a track
    double settleSeconds = 8.0;       // skipped at the start of each track (crossfade)
    double minSecondsToLevel = 10.0;  // measured audio needed before the gain follows the track
    double minSecondsToStore = 30.0;  // measured audio needed before a track is remembered
    size_t stereoHistory = 30;        // stereo tracks kept for the target
    size_t minStereoTracks = 2;       // stereo tracks needed before the target is trusted
};

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

private:
    std::map<std::string, StoredLevel> m_levels;
};

// A track whose loudness the leveler has measured, for the journal.
struct FinishedTrack {
    bool atmos = false;
    double lufs = 0;
    double seconds = 0;
};

// What the gain policy knows about the track that is playing.
struct TrackLevel {
    std::optional<double> storedLufs;    // known before the track is heard, from the VariantStore
    std::optional<double> measuredLufs;  // from the meter, once minSecondsToLevel has been measured
    bool audible = false;                // audio has passed the settle period
    double peak = 0;                     // linear peak, relative to the player volume
    double userVolume = 1;               // linear player volume (the slider)
};

// The Atmos gain (dB) the track should have now. Pure: the caller keeps `currentDb` and calls
// this about every 300 ms. `target` is the stereo level from TargetHistory, if known.
double NextGainDb(const LevelerConfig& config, std::optional<double> target, const TrackLevel& track,
                  double currentDb);

// Follows one playing item at a time: meters the decoded samples and remembers the level of
// each track it heard for long enough. Not thread-safe: the caller serializes calls.
class AtmosLeveler {
public:
    AtmosLeveler(const LevelerConfig& config, double sampleRate, int channels);

    // Ends the previous track, storing it if it was measured long enough, then starts this one.
    void StartTrack(const std::string& variantId, bool atmos);

    // Decoded samples, with the player volume applied, as Apple Music hands them to the audio device.
    void Feed(const float* interleaved, size_t frames, float volume);

    // Spatial audio objects, from ObjectPass: per-frame power of all objects and their peak, both
    // before the gain. Same settle period and volume handling as Feed.
    void FeedPower(const std::vector<double>& framePower, float peak, float volume);

    // Rebuilds the meter when the stream runs at another sample rate. A track being measured ends first.
    void SetSampleRate(double sampleRate);

    // The Atmos gain (dB) for the current track. For a stereo track the value passed in comes back
    // unchanged: check IsAtmos() and apply 0 dB to stereo tracks.
    double Poll(double currentDb);

    bool IsAtmos() const;

    // Loudness of the current track so far. Empty until enough audio has been measured.
    std::optional<double> MeasuredLufs() const;

    // The last track that ended with a measured loudness. Returned once, then forgotten.
    std::optional<FinishedTrack> TakeFinished();

    const TargetHistory& StereoHistory() const;
    const VariantStore& Store() const;

private:
    void FinishTrack();

    LevelerConfig m_config;
    double m_sampleRate;
    int m_channels;
    TargetHistory m_stereo;
    VariantStore m_store;

    LoudnessMeter m_meter;
    std::string m_variantId;
    bool m_active = false;
    bool m_atmos = false;
    std::optional<StoredLevel> m_stored;
    size_t m_settleFramesLeft = 0;
    bool m_audible = false;
    float m_volume = 1.0f;
    std::optional<FinishedTrack> m_finished;
};

}  // namespace atmos
