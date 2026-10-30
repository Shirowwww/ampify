#include "leveler.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace atmos {

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

double NextGainDb(const LevelerConfig& config, std::optional<double> target, const TrackLevel& track,
                  double currentDb) {
    if (!config.autoLevel || !target) return config.fallbackGainDb;

    // Nothing is known about this track yet: keep the gain it already has.
    if (!track.storedLufs && !track.measuredLufs) return currentDb;

    double lufs = track.storedLufs ? *track.storedLufs : *track.measuredLufs;
    double desired = std::clamp(*target - lufs, 0.0, config.maxGainDb);

    // The gain must not push the output peak past the ceiling. The peak is relative to the
    // player volume, so multiply back by it to get the level the listener hears.
    if (track.peak > 0 && track.userVolume > 0) {
        double ceilingDb = LinearToDb(config.peakCeiling / (track.peak * track.userVolume));
        desired = std::min(desired, ceilingDb);
    }
    desired = std::max(desired, 0.0);

    // A stored track jumps straight to its gain before the music starts. Later changes ease in.
    if (track.storedLufs && !track.audible) return desired;
    return std::clamp(desired, currentDb - config.slewDbPerPoll, currentDb + config.slewDbPerPoll);
}

AtmosLeveler::AtmosLeveler(const LevelerConfig& config, double sampleRate, int channels)
    : m_config(config),
      m_sampleRate(sampleRate),
      m_channels(channels),
      m_stereo(config.stereoHistory),
      m_meter(sampleRate, channels) {}

void AtmosLeveler::StartTrack(const std::string& variantId, bool atmos) {
    FinishTrack();

    m_variantId = variantId;
    m_atmos = atmos;
    m_active = true;
    m_stored = atmos ? m_store.Find(variantId) : std::nullopt;
    m_settleFramesLeft = static_cast<size_t>(m_config.settleSeconds * m_sampleRate);
    m_audible = false;
    m_meter.Reset();
}

void AtmosLeveler::Feed(const float* interleaved, size_t frames, float volume) {
    if (!m_active || volume <= 0.0f) return;

    m_volume = volume;

    // Skip the settle period at the start of the track.
    size_t settle = std::min(frames, m_settleFramesLeft);
    m_settleFramesLeft -= settle;
    interleaved += settle * static_cast<size_t>(m_channels);
    frames -= settle;
    if (frames == 0) return;

    // Measure as if the slider were at full: the loudness must not depend on the volume.
    m_audible = true;
    m_meter.Process(interleaved, frames, 1.0f / volume);
}

void AtmosLeveler::FeedPower(const std::vector<double>& framePower, float peak, float volume) {
    if (!m_active || volume <= 0.0f) return;

    m_volume = volume;

    size_t settle = std::min(framePower.size(), m_settleFramesLeft);
    m_settleFramesLeft -= settle;
    size_t frames = framePower.size() - settle;
    if (frames == 0) return;

    // Power scales with the square of the gain, the peak with the gain itself.
    m_audible = true;
    double scale = 1.0 / (static_cast<double>(volume) * volume);
    m_meter.ProcessPower(framePower.data() + settle, frames, scale, peak / volume);
}

void AtmosLeveler::SetSampleRate(double sampleRate) {
    if (sampleRate == m_sampleRate) return;

    FinishTrack();
    m_sampleRate = sampleRate;
    m_meter = LoudnessMeter(sampleRate, m_channels);
}

std::optional<double> AtmosLeveler::MeasuredLufs() const {
    return m_meter.IntegratedLufs();
}

std::optional<FinishedTrack> AtmosLeveler::TakeFinished() {
    std::optional<FinishedTrack> finished = m_finished;
    m_finished.reset();
    return finished;
}

double AtmosLeveler::Poll(double currentDb) {
    if (!m_active || !m_atmos) return currentDb;

    TrackLevel track;
    if (m_stored) track.storedLufs = m_stored->lufs;
    if (m_meter.Seconds() >= m_config.minSecondsToLevel) track.measuredLufs = m_meter.IntegratedLufs();
    track.audible = m_audible;
    track.peak = m_meter.Peak();
    track.userVolume = m_volume;

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

    double seconds = m_meter.Seconds();
    std::optional<double> lufs = m_meter.IntegratedLufs();
    if (lufs) m_finished = FinishedTrack{m_atmos, *lufs, seconds};
    if (lufs && seconds >= m_config.minSecondsToStore) {
        if (m_atmos) {
            m_store.Put(m_variantId, StoredLevel{*lufs, seconds});
        } else {
            m_stereo.Remember(*lufs);
        }
    }
    m_active = false;
}

}  // namespace atmos
