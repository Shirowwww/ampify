#include "k_weighting.hpp"

#include <numbers>

namespace ampify::audio {

namespace {

size_t ChunkFrames(double sampleRate) {
    return std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate / 10.0)));
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

ChunkedPower::ChunkedPower(double sampleRate, int channels)
    : m_sampleRate(sampleRate),
      m_chunkFrames(ChunkFrames(sampleRate)),
      m_filters(static_cast<size_t>(std::max(1, channels)), KWeighting(sampleRate)) {}

void ChunkedPower::SetSampleRate(double sampleRate) {
    m_sampleRate = sampleRate;
    m_chunkFrames = ChunkFrames(sampleRate);
    for (KWeighting& filter : m_filters) filter = KWeighting(sampleRate);
    m_chunkPower = 0;
    m_framesInChunk = 0;
}

void ChunkedPower::Reset() {
    for (KWeighting& filter : m_filters) filter.Reset();
    m_chunkPower = 0;
    m_framesInChunk = 0;
}

}  // namespace ampify::audio
