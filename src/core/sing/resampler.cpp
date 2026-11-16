#include "resampler.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace ampify::sing {

namespace {

constexpr double kStopbandDb = 90;    // designed with room over the 80 dB promised
constexpr double kCutoff = 0.45;      // of the lower rate: 0.9 of its Nyquist
constexpr double kTransition = 0.1;   // of the lower rate, centred on the cutoff

double BesselI0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 64 && term > 1e-12 * sum; k++) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
    }
    return sum;
}

double Sinc(double x) { return x == 0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x); }

}  // namespace

Resampler::Resampler(int inRate, int outRate) {
    if (inRate > 0 && outRate > 0 && inRate != outRate) {
        const std::int64_t unit = std::gcd(inRate, outRate);
        m_up = outRate / unit;
        m_down = inRate / unit;
        const double low = std::min(inRate, outRate);
        const double cutoff = kCutoff * low / inRate;   // cycles per input sample
        const double width = kTransition * low / inRate;
        const double length = (kStopbandDb - 7.95) / (2.285 * 2 * std::numbers::pi * width);  // Kaiser's estimate
        m_half = static_cast<int>(std::ceil(length / 2));
        m_latency = static_cast<int>((m_half + m_down - 1) / m_down * m_down);
        const double beta = 0.1102 * (kStopbandDb - 8.7);
        const size_t span = 2 * static_cast<size_t>(m_half);
        m_taps.resize(static_cast<size_t>(m_up) * span);
        for (std::int64_t phase = 0; phase < m_up; phase++) {
            float* taps = m_taps.data() + phase * span;
            double sum = 0;
            for (size_t i = 0; i < span; i++) {
                const double t = static_cast<double>(i) - (m_half - 1) - static_cast<double>(phase) / m_up;
                const double edge = t / m_half;
                const double window = BesselI0(beta * std::sqrt(std::max(0.0, 1 - edge * edge))) / BesselI0(beta);
                const double tap = 2 * cutoff * Sinc(2 * cutoff * t) * window;
                taps[i] = static_cast<float>(tap);
                sum += tap;
            }
            // Each phase passes DC at exactly 1, so the phases do not ripple against one another.
            for (size_t i = 0; i < span; i++) taps[i] = static_cast<float>(taps[i] / sum);
        }
    }
    m_size = static_cast<size_t>(m_latency + m_half);
    m_left.assign(2 * m_size, 0.0f);
    m_right.assign(2 * m_size, 0.0f);
}

int Resampler::OutputLatency() const { return static_cast<int>(m_latency * m_up / m_down); }

size_t Resampler::MaxOutput(size_t frames) const {
    return static_cast<size_t>((static_cast<std::int64_t>(frames) * m_up + m_down - 1) / m_down + 1);
}

void Resampler::Reset() {
    std::fill(m_left.begin(), m_left.end(), 0.0f);
    std::fill(m_right.begin(), m_right.end(), 0.0f);
    m_at = 0;
    m_count = m_next = m_phase = 0;
}

void Resampler::Push(float left, float right) {
    m_left[m_at] = m_left[m_at + m_size] = left;
    m_right[m_at] = m_right[m_at + m_size] = right;
    m_at = (m_at + 1) % m_size;
}

// The output due now sits m_phase / L past input sample m_count - Latency(); its kernel spans the oldest 2H samples
// held, which start at m_at once the newest is written.
void Resampler::Emit(float* out) const {
    const size_t span = 2 * static_cast<size_t>(m_half);
    const float* taps = m_taps.data() + m_phase * static_cast<std::int64_t>(span);
    const float* left = m_left.data() + m_at;
    const float* right = m_right.data() + m_at;
    float sumLeft = 0, sumRight = 0;
    for (size_t i = 0; i < span; i++) {
        sumLeft += taps[i] * left[i];
        sumRight += taps[i] * right[i];
    }
    out[0] = sumLeft;
    out[1] = sumRight;
}

size_t Resampler::Process(const float* in, size_t frames, float* out) {
    if (m_up == m_down) {
        std::copy(in, in + frames * 2, out);
        m_count += static_cast<std::int64_t>(frames);
        return frames;
    }
    size_t written = 0;
    for (size_t i = 0; i < frames; i++) {
        Push(in[i * 2], in[i * 2 + 1]);
        while (m_next == m_count) {
            Emit(out + written * 2);
            written++;
            m_phase += m_down;
            m_next += m_phase / m_up;
            m_phase %= m_up;
        }
        m_count++;
    }
    return written;
}

}  // namespace ampify::sing
