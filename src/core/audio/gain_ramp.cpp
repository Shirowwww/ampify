#include "gain_ramp.hpp"

#include <algorithm>
#include <cmath>

namespace ampify::audio {

namespace {

constexpr float kKnee = 0.94f;

// About 0.1 s time constant: 95 % of the way to the target after 0.3 s. Unmeasured: taken from the Windhawk reference
// mod, no listen test has confirmed it yet.
constexpr double kRatePerSecond = 10.0;

}  // namespace

GainRamp::GainRamp(double sampleRate) : m_sampleRate(sampleRate) {}

void GainRamp::SetSampleRate(double sampleRate) {
    m_sampleRate = sampleRate;
}

bool GainRamp::Curve(size_t frames, double target, std::vector<float>& curve) {
    curve.resize(frames);
    const double step = std::min(1.0, kRatePerSecond / m_sampleRate);

    bool unity = true;
    for (size_t frame = 0; frame < frames; frame++) {
        m_current += (target - m_current) * step;
        if (std::fabs(target - m_current) < 1e-4) m_current = target;

        curve[frame] = static_cast<float>(m_current);
        if (curve[frame] != 1.0f) unity = false;
    }
    return unity;
}

double GainRamp::Current() const {
    return m_current;
}

float GainRamp::Limit(float sample) {
    float magnitude = std::fabs(sample);
    if (magnitude <= kKnee) return sample;

    float knee = kKnee + (1.0f - kKnee) * std::tanh((magnitude - kKnee) / (1.0f - kKnee));
    return std::copysign(knee, sample);
}

void ApplyCurve(float* samples, size_t frames, int channels, const std::vector<float>& curve, bool unity,
                SampleStage* stage, const void* key) {
    const bool staged = stage && stage->Active();
    if (unity && !staged) return;
    const size_t width = static_cast<size_t>(channels);
    if (!staged) {
        for (size_t frame = 0; frame < frames; frame++) {
            for (size_t channel = 0; channel < width; channel++) {
                float& sample = samples[frame * width + channel];
                sample = GainRamp::Limit(sample * curve[frame]);
            }
        }
        return;
    }
    if (!unity) {
        for (size_t frame = 0; frame < frames; frame++) {
            for (size_t channel = 0; channel < width; channel++) samples[frame * width + channel] *= curve[frame];
        }
    }
    stage->Run(key, samples, frames, channels);
    for (size_t i = 0; i < frames * width; i++) samples[i] = GainRamp::Limit(samples[i]);
}

}  // namespace ampify::audio
