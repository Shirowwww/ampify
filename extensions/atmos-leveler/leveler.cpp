#include "leveler.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace atmos {

LevelerConfig ClampGains(LevelerConfig config) {
    const LevelerConfig defaults;
    auto clamp = [](double value, double fallback) {
        return std::isfinite(value) ? std::clamp(value, 0.0, kGainLimitDb) : fallback;
    };
    config.fallbackGainDb = clamp(config.fallbackGainDb, defaults.fallbackGainDb);
    config.maxGainDb = clamp(config.maxGainDb, defaults.maxGainDb);
    return config;
}

double DbToLinear(double db) {
    return std::pow(10.0, db / 20.0);
}

double LinearToDb(double linear) {
    return 20.0 * std::log10(linear);
}

TargetHistory::TargetHistory(size_t capacity) : m_capacity(capacity) {}

void TargetHistory::Remember(double lufs) {
    m_values.push_back(lufs);
    if (m_values.size() > m_capacity) m_values.erase(m_values.begin());
}

std::optional<double> TargetHistory::Target(size_t minTracks) const {
    if (m_values.size() < minTracks || m_values.empty()) return std::nullopt;

    std::vector<double> sorted = m_values;
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2];
}

size_t TargetHistory::Size() const {
    return m_values.size();
}

size_t TargetHistory::Capacity() const {
    return m_capacity;
}

const std::vector<double>& TargetHistory::Values() const {
    return m_values;
}

std::optional<StoredLevel> VariantStore::Find(const std::string& variantId) const {
    auto it = m_levels.find(variantId);
    if (it == m_levels.end()) return std::nullopt;
    return it->second;
}

bool VariantStore::Put(const std::string& variantId, StoredLevel level) {
    auto it = m_levels.find(variantId);
    if (it != m_levels.end() && it->second.seconds >= level.seconds) return false;

    m_levels[variantId] = level;
    return true;
}

const std::map<std::string, StoredLevel>& VariantStore::Levels() const {
    return m_levels;
}

double NextGainDb(const LevelerConfig& config, std::optional<double> target, const TrackLevel& track,
                  double currentDb) {
    if (!config.autoLevel || !target) return config.fallbackGainDb;

    // Nothing is known about this track yet: keep the gain it already has.
    if (!track.storedLufs && !track.measuredLufs) return currentDb;

    double lufs = track.storedLufs ? *track.storedLufs : *track.measuredLufs;
    double desired = std::clamp(*target - lufs, 0.0, config.maxGainDb);

    // The gain must not push the peak of the buffers it goes into past the ceiling. The peak is
    // without the player volume: multiply back by the volume those buffers already carry (the
    // slider for the stereo stream, 1 for spatial objects, which come before it).
    if (track.peak > 0 && track.userVolume > 0) {
        double ceilingDb = LinearToDb(config.peakCeiling / (track.peak * track.userVolume));
        desired = std::min(desired, ceilingDb);
    }
    desired = std::max(desired, 0.0);

    // A stored track jumps straight to its gain before the music starts. Later changes ease in.
    if (track.storedLufs && !track.audible) return desired;
    return std::clamp(desired, currentDb - config.slewDbPerPoll, currentDb + config.slewDbPerPoll);
}

void AtmosLeveler::Path::Start(double settleSeconds) {
    meter.Reset();
    settleLeft = settleSeconds;
    audible = false;
}

size_t AtmosLeveler::Path::Settle(size_t frames) {
    double rate = meter.SampleRate();
    size_t settle = std::min(frames, static_cast<size_t>(std::llround(settleLeft * rate)));
    settleLeft = std::max(0.0, settleLeft - static_cast<double>(settle) / rate);
    if (settle < frames) audible = true;
    return settle;
}

AtmosLeveler::AtmosLeveler(const LevelerConfig& config, double streamRate, double objectRate)
    : m_config(config), m_stereo(config.stereoHistory), m_stream(streamRate, 2), m_objects(objectRate, 1) {}

void AtmosLeveler::StartTrack(const std::string& variantId, bool atmos) {
    FinishTrack();

    m_variantId = variantId;
    m_atmos = atmos;
    m_active = true;
    m_stored = atmos ? m_store.Find(variantId) : std::nullopt;
    m_stream.Start(m_config.settleSeconds);
    m_objects.Start(m_config.settleSeconds);
}

void AtmosLeveler::EndTrack() {
    FinishTrack();
}

void AtmosLeveler::Feed(const float* interleaved, size_t frames, float volume) {
    if (!m_active || volume <= ampify::audio::kLowestPlayerVolume) return;

    m_volume = volume;

    // Skip the settle period at the start of the track.
    size_t settle = m_stream.Settle(frames);
    interleaved += settle * 2;
    frames -= settle;
    if (frames == 0) return;

    // Measure as if the slider were at full: the loudness must not depend on the volume.
    m_stream.meter.Process(interleaved, frames, 1.0f / volume);
}

void AtmosLeveler::FeedPower(const std::vector<double>& framePower, float peak) {
    if (!m_active) return;

    size_t settle = m_objects.Settle(framePower.size());
    size_t frames = framePower.size() - settle;
    if (frames == 0) return;

    // Before the slider: the buffers are measured as they are.
    m_objects.meter.ProcessPower(framePower.data() + settle, frames, 1.0, peak);
}

void AtmosLeveler::SetStreamRate(double sampleRate) {
    m_stream.meter.SetSampleRate(sampleRate);
}

void AtmosLeveler::SetObjectRate(double sampleRate) {
    m_objects.meter.SetSampleRate(sampleRate);
}

double AtmosLeveler::StreamRate() const {
    return m_stream.meter.SampleRate();
}

double AtmosLeveler::ObjectRate() const {
    return m_objects.meter.SampleRate();
}

AudioPath AtmosLeveler::MeasuringPath() const {
    AudioPath own = m_atmos ? AudioPath::Objects : AudioPath::Stream;
    AudioPath other = m_atmos ? AudioPath::Stream : AudioPath::Objects;
    const Path& ownPath = own == AudioPath::Objects ? m_objects : m_stream;
    const Path& otherPath = other == AudioPath::Objects ? m_objects : m_stream;
    if (!ownPath.meter.IntegratedLufs() && otherPath.meter.IntegratedLufs()) return other;
    return own;
}

const AtmosLeveler::Path& AtmosLeveler::Measuring() const {
    return MeasuringPath() == AudioPath::Objects ? m_objects : m_stream;
}

std::optional<double> AtmosLeveler::MeasuredLufs() const {
    return Measuring().meter.IntegratedLufs();
}

std::optional<FinishedTrack> AtmosLeveler::TakeFinished() {
    std::optional<FinishedTrack> finished = m_finished;
    m_finished.reset();
    return finished;
}

void AtmosLeveler::Restore(const VariantStore& store, const std::vector<double>& stereo) {
    m_store = store;
    m_stereo = TargetHistory(m_config.stereoHistory);
    for (double lufs : stereo) m_stereo.Remember(lufs);
}

double AtmosLeveler::Poll(double currentDb) {
    if (!m_active || !m_atmos) return currentDb;

    AudioPath path = MeasuringPath();
    const Path& measuring = Measuring();
    TrackLevel track;
    if (m_stored) track.storedLufs = m_stored->lufs;
    if (measuring.meter.Seconds() >= m_config.minSecondsToLevel) track.measuredLufs = measuring.meter.IntegratedLufs();
    track.audible = measuring.audible;
    track.peak = measuring.meter.Peak();
    track.userVolume = path == AudioPath::Objects ? 1.0 : m_volume;

    return NextGainDb(m_config, m_stereo.Target(m_config.minStereoTracks), track, currentDb);
}

bool AtmosLeveler::IsAtmos() const {
    return m_active && m_atmos;
}

const TargetHistory& AtmosLeveler::StereoHistory() const {
    return m_stereo;
}

const VariantStore& AtmosLeveler::Store() const {
    return m_store;
}

void AtmosLeveler::FinishTrack() {
    if (!m_active) return;

    AudioPath path = MeasuringPath();
    const Path& measuring = Measuring();
    double seconds = measuring.meter.Seconds();
    std::optional<double> lufs = measuring.meter.IntegratedLufs();
    bool stored = false;
    if (lufs && seconds >= m_config.minSecondsToStore) {
        if (!m_atmos) {
            m_stereo.Remember(*lufs);
            stored = true;
        } else if (!m_variantId.empty()) {
            stored = m_store.Put(m_variantId, StoredLevel{*lufs, seconds});
        }
    }
    if (lufs) m_finished = FinishedTrack{m_atmos, *lufs, seconds, path, stored};
    m_active = false;
}

}  // namespace atmos
