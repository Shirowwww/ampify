#pragma once

#include <cstddef>
#include <vector>

namespace atmos {

// Gain on the decoded samples, eased towards a target so track changes do not click. Holds its
// position between calls, so there is one ramp per audio stream.
class GainRamp {
public:
    explicit GainRamp(double sampleRate);

    void SetSampleRate(double sampleRate);

    // Eases the gain towards `target` over the next `frames` frames and writes the gain of each
    // frame into `curve`. Returns true when every value is exactly 1: the samples can be left alone.
    bool Curve(size_t frames, double target, std::vector<float>& curve);

    double Current() const;

    // Soft knee above -0.5 dBFS: linear up to 0.94, then approaching 1.0 and never above it,
    // so a boost cannot clip.
    static float Limit(float sample);

private:
    double m_sampleRate;
    double m_current = 1.0;
};

// Multiplies each frame of interleaved samples by its gain from `curve`, then applies the knee.
void ApplyCurve(float* samples, size_t frames, int channels, const std::vector<float>& curve);

}  // namespace atmos
