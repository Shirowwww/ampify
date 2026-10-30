#include "live_loudness.hpp"

#include <algorithm>
#include <cmath>

namespace atmos {

namespace {

constexpr double kLoudnessOffset = -0.691;  // as LoudnessMeter: a 997 Hz full-scale sine in one channel is -3.01 LUFS

size_t ChunkFrames(double sampleRate) {
    return std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate / 10.0)));
}

}  // namespace

LiveLoudness::LiveLoudness(double sampleRate, int channels)
    : m_sampleRate(sampleRate),
      m_chunkFrames(ChunkFrames(sampleRate)),
      m_filters(static_cast<size_t>(std::max(1, channels)), KWeighting(sampleRate)) {}

void LiveLoudness::Process(const float* interleaved, size_t frames, float gain) {
    if (!interleaved) return;
    const size_t channels = m_filters.size();
    for (size_t frame = 0; frame < frames; frame++) {
        const float* sample = interleaved + frame * channels;
        double power = 0;
        for (size_t channel = 0; channel < channels; channel++) {
            double weighted = m_filters[channel].Process(static_cast<double>(sample[channel]) * gain);
            power += weighted * weighted;
        }
        AddFramePower(power);
    }
}

void LiveLoudness::ProcessPower(const double* framePower, size_t frames, double scale) {
    if (!framePower) return;
    for (size_t frame = 0; frame < frames; frame++) AddFramePower(framePower[frame] * scale);
}

void LiveLoudness::AddFramePower(double power) {
    m_chunkPower += power;
    if (++m_framesInChunk < m_chunkFrames) return;
    m_chunks[m_next] = m_chunkPower / static_cast<double>(m_chunkFrames);
    m_next = (m_next + 1) % kShortTermChunks;
    m_count = std::min(m_count + 1, kShortTermChunks);
    m_chunkPower = 0;
    m_framesInChunk = 0;
}

std::optional<double> LiveLoudness::Window(size_t chunks) const {
    if (m_count < chunks) return std::nullopt;
    double sum = 0;
    for (size_t i = 1; i <= chunks; i++) sum += m_chunks[(m_next + kShortTermChunks - i) % kShortTermChunks];
    return kLoudnessOffset + 10.0 * std::log10(std::max(sum / static_cast<double>(chunks), 1e-12));
}

std::optional<double> LiveLoudness::MomentaryLufs() const { return Window(kMomentaryChunks); }

std::optional<double> LiveLoudness::ShortTermLufs() const { return Window(kShortTermChunks); }

void LiveLoudness::SetSampleRate(double sampleRate) {
    if (sampleRate == m_sampleRate) return;
    m_sampleRate = sampleRate;
    m_chunkFrames = ChunkFrames(sampleRate);
    for (KWeighting& filter : m_filters) filter = KWeighting(sampleRate);
    Reset();
}

void LiveLoudness::Reset() {
    for (KWeighting& filter : m_filters) filter.Reset();
    m_chunkPower = 0;
    m_framesInChunk = 0;
    m_chunks.fill(0);
    m_next = 0;
    m_count = 0;
}

}  // namespace atmos
