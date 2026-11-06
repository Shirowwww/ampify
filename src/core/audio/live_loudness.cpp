#include "live_loudness.hpp"

namespace ampify::audio {

LiveLoudness::LiveLoudness(double sampleRate, int channels) : m_power(sampleRate, channels) {}

void LiveLoudness::Process(const float* interleaved, size_t frames, float gain) {
    if (!interleaved) return;
    m_power.Weigh(interleaved, frames, gain, [this](double meanPower) { AddChunk(meanPower); });
}

void LiveLoudness::ProcessPower(const double* framePower, size_t frames, double scale) {
    if (!framePower) return;
    m_power.Add(framePower, frames, scale, [this](double meanPower) { AddChunk(meanPower); });
}

void LiveLoudness::AddChunk(double meanPower) {
    m_chunks[m_next] = meanPower;
    m_next = (m_next + 1) % kShortTermChunks;
    m_count = std::min(m_count + 1, kShortTermChunks);
}

std::optional<double> LiveLoudness::Window(size_t chunks) const {
    if (m_count < chunks) return std::nullopt;
    double sum = 0;
    for (size_t i = 1; i <= chunks; i++) sum += m_chunks[(m_next + kShortTermChunks - i) % kShortTermChunks];
    return PowerToLufs(sum / static_cast<double>(chunks));
}

std::optional<double> LiveLoudness::MomentaryLufs() const { return Window(kMomentaryChunks); }

std::optional<double> LiveLoudness::ShortTermLufs() const { return Window(kShortTermChunks); }

void LiveLoudness::SetSampleRate(double sampleRate) {
    if (sampleRate == SampleRate()) return;
    m_power.SetSampleRate(sampleRate);
    Reset();
}

void LiveLoudness::Reset() {
    m_power.Reset();
    m_chunks.fill(0);
    m_next = 0;
    m_count = 0;
}

}  // namespace ampify::audio
