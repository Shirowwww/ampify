#include "loudness_meter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace atmos {

namespace {

constexpr double kLoudnessOffset = -0.691;
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kRelativeGateLu = -10.0;
constexpr size_t kChunksPerBlock = 4;

double PowerToLufs(double meanSquare) {
    return kLoudnessOffset + 10.0 * std::log10(std::max(meanSquare, 1e-12));
}

}  // namespace

double KWeighting::Biquad::Process(double x) {
    double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

KWeighting::KWeighting(double sampleRate) {
    // Constants from ITU-R BS.1770-4, bilinear transform as in libebur128.
    const double k1 = std::tan(std::numbers::pi * 1681.974450955533 / sampleRate);
    const double q1 = 0.7071752369554196;
    const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    double a0 = 1.0 + k1 / q1 + k1 * k1;
    m_shelf.b0 = (vh + vb * k1 / q1 + k1 * k1) / a0;
    m_shelf.b1 = 2.0 * (k1 * k1 - vh) / a0;
    m_shelf.b2 = (vh - vb * k1 / q1 + k1 * k1) / a0;
    m_shelf.a1 = 2.0 * (k1 * k1 - 1.0) / a0;
    m_shelf.a2 = (1.0 - k1 / q1 + k1 * k1) / a0;

    const double k2 = std::tan(std::numbers::pi * 38.13547087602444 / sampleRate);
    const double q2 = 0.5003270373238773;
    a0 = 1.0 + k2 / q2 + k2 * k2;
    m_highPass.b0 = 1.0;
    m_highPass.b1 = -2.0;
    m_highPass.b2 = 1.0;
    m_highPass.a1 = 2.0 * (k2 * k2 - 1.0) / a0;
    m_highPass.a2 = (1.0 - k2 / q2 + k2 * k2) / a0;
}

double KWeighting::Process(double x) {
    return m_highPass.Process(m_shelf.Process(x));
}

void KWeighting::Reset() {
    m_shelf.z1 = m_shelf.z2 = 0;
    m_highPass.z1 = m_highPass.z2 = 0;
}

namespace {

size_t ChunkFrames(double sampleRate) {
    return std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate / 10.0)));
}

}  // namespace

LoudnessMeter::LoudnessMeter(double sampleRate, int channels)
    : m_sampleRate(sampleRate),
      m_chunkFrames(ChunkFrames(sampleRate)),
      m_filters(static_cast<size_t>(channels), KWeighting(sampleRate)) {}

void LoudnessMeter::Process(const float* interleaved, size_t frames, float gain) {
    const size_t channels = m_filters.size();
    for (size_t frame = 0; frame < frames; frame++) {
        const float* sample = interleaved + frame * channels;
        double power = 0;
        for (size_t channel = 0; channel < channels; channel++) {
            double x = static_cast<double>(sample[channel]) * gain;
            m_peak = std::max(m_peak, static_cast<float>(std::fabs(x)));

            double y = m_filters[channel].Process(x);
            power += y * y;
        }
        AddFramePower(power);
    }
    m_seconds += static_cast<double>(frames) / m_sampleRate;
}

void LoudnessMeter::ProcessPower(const double* framePower, size_t frames, double scale, float peak) {
    for (size_t frame = 0; frame < frames; frame++) AddFramePower(framePower[frame] * scale);
    m_seconds += static_cast<double>(frames) / m_sampleRate;
    m_peak = std::max(m_peak, peak);
}

void LoudnessMeter::AddFramePower(double power) {
    m_chunkPower += power;
    if (++m_framesInChunk == m_chunkFrames) {
        m_chunkPowers.push_back(m_chunkPower / static_cast<double>(m_chunkFrames));
        m_chunkPower = 0;
        m_framesInChunk = 0;
    }
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
    if (sampleRate == m_sampleRate) return;

    m_sampleRate = sampleRate;
    m_chunkFrames = ChunkFrames(sampleRate);
    for (KWeighting& filter : m_filters) filter = KWeighting(sampleRate);
    m_chunkPower = 0;
    m_framesInChunk = 0;
}

double LoudnessMeter::SampleRate() const {
    return m_sampleRate;
}

void LoudnessMeter::Reset() {
    for (KWeighting& filter : m_filters) filter.Reset();
    m_chunkPower = 0;
    m_framesInChunk = 0;
    m_chunkPowers.clear();
    m_seconds = 0;
    m_peak = 0;
}

}  // namespace atmos
