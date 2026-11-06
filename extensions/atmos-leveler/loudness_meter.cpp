#include "loudness_meter.hpp"

#include <algorithm>

namespace atmos {

namespace {

using ampify::audio::PowerToLufs;

constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kRelativeGateLu = -10.0;
constexpr size_t kChunksPerBlock = 4;

}  // namespace

LoudnessMeter::LoudnessMeter(double sampleRate, int channels) : m_power(sampleRate, channels) {}

void LoudnessMeter::Process(const float* interleaved, size_t frames, float gain) {
    float peak = m_power.Weigh(interleaved, frames, gain, [this](double power) { m_chunkPowers.push_back(power); });
    m_peak = std::max(m_peak, peak);
    m_seconds += static_cast<double>(frames) / m_power.SampleRate();
}

void LoudnessMeter::ProcessPower(const double* framePower, size_t frames, double scale, float peak) {
    m_power.Add(framePower, frames, scale, [this](double power) { m_chunkPowers.push_back(power); });
    m_seconds += static_cast<double>(frames) / m_power.SampleRate();
    m_peak = std::max(m_peak, peak);
}

std::optional<double> LoudnessMeter::IntegratedLufs() const {
    if (m_chunkPowers.size() < kChunksPerBlock) return std::nullopt;

    // One block per chunk from the fourth chunk on: 400 ms long, 100 ms apart.
    std::vector<double> blocks;
    blocks.reserve(m_chunkPowers.size() - kChunksPerBlock + 1);
    for (size_t end = kChunksPerBlock; end <= m_chunkPowers.size(); end++) {
        double sum = 0;
        for (size_t i = end - kChunksPerBlock; i < end; i++) sum += m_chunkPowers[i];
        blocks.push_back(sum / kChunksPerBlock);
    }

    double absoluteSum = 0;
    size_t absoluteCount = 0;
    for (double block : blocks) {
        if (PowerToLufs(block) > kAbsoluteGateLufs) {
            absoluteSum += block;
            absoluteCount++;
        }
    }
    if (absoluteCount == 0) return std::nullopt;

    double relativeGate = PowerToLufs(absoluteSum / absoluteCount) + kRelativeGateLu;
    double sum = 0;
    size_t count = 0;
    for (double block : blocks) {
        if (PowerToLufs(block) > kAbsoluteGateLufs && PowerToLufs(block) > relativeGate) {
            sum += block;
            count++;
        }
    }
    if (count == 0) return std::nullopt;
    return PowerToLufs(sum / count);
}

double LoudnessMeter::Seconds() const {
    return m_seconds;
}

float LoudnessMeter::Peak() const {
    return m_peak;
}

void LoudnessMeter::SetSampleRate(double sampleRate) {
    if (sampleRate == m_power.SampleRate()) return;
    m_power.SetSampleRate(sampleRate);
}

double LoudnessMeter::SampleRate() const {
    return m_power.SampleRate();
}

void LoudnessMeter::Reset() {
    m_power.Reset();
    m_chunkPowers.clear();
    m_seconds = 0;
    m_peak = 0;
}

}  // namespace atmos
