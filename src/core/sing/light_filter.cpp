#include "light_filter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ampify::sing {

namespace {

constexpr double kLowHz = 150;
constexpr double kHighHz = 12000;
constexpr double kVoiceSeconds = 0.05;
constexpr double kButterworthQ = std::numbers::sqrt2 / 2;  // a 2nd-order Butterworth section

struct Coefficients {
    double b0, b1, b2, a1, a2;
};

// RBJ cookbook low-pass or high-pass, normalised (a0 = 1).
Coefficients Pass(bool high, double frequency, int rate) {
    const double w = 2 * std::numbers::pi * frequency / rate;
    const double cosW = std::cos(w);
    const double alpha = std::sin(w) / (2 * kButterworthQ);
    const double a0 = 1 + alpha;
    const double edge = high ? (1 + cosW) / 2 : (1 - cosW) / 2;
    const double middle = high ? -(1 + cosW) : 1 - cosW;
    return {edge / a0, middle / a0, edge / a0, -2 * cosW / a0, (1 - alpha) / a0};
}

}  // namespace

void LightFilter::Reset() {
    m_state = {};
    m_voice = -1;
}

void LightFilter::Design(int rate) {
    const Coefficients low = Pass(false, kLowHz, rate);
    const Coefficients high = Pass(true, kHighHz, rate);
    m_sections[0] = {low.b0, low.b1, low.b2, low.a1, low.a2};
    m_sections[1] = {high.b0, high.b1, high.b2, high.a1, high.a2};
    m_state = {};
    m_rate = rate;
    m_voiceStep = 1.0 / (kVoiceSeconds * rate);
}

// The channel minus its low and high parts (transposed direct form II, in double for the 150 Hz poles at 192 kHz).
double LightFilter::Band(size_t channel, double x) {
    double parts = 0;
    for (size_t k = 0; k < kSections; k++) {
        const Biquad& c = m_sections[k];
        State& s = m_state[channel][k];
        const double out = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * out + s.z2;
        s.z2 = c.b2 * x - c.a2 * out;
        parts += out;
    }
    return x - parts;
}

void LightFilter::Process(float* interleaved, size_t frames, int rate, double voice) {
    if (interleaved == nullptr || rate <= 2 * kHighHz) return;
    if (rate != m_rate) Design(rate);
    const double target = std::isfinite(voice) ? std::clamp(voice, 0.0, 1.0) : 1.0;
    if (m_voice < 0) m_voice = target;
    for (size_t i = 0; i < frames; i++) {
        m_voice = target > m_voice ? std::min(target, m_voice + m_voiceStep) : std::max(target, m_voice - m_voiceStep);
        const double left = interleaved[i * 2];
        const double right = interleaved[i * 2 + 1];
        const double centre = (Band(0, left) + Band(1, right)) / 2;
        const double take = 1 - m_voice;
        interleaved[i * 2] = static_cast<float>(left - take * centre);
        interleaved[i * 2 + 1] = static_cast<float>(right - take * centre);
    }
}

}  // namespace ampify::sing
