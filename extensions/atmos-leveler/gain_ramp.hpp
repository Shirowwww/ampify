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

// A step on the samples after the gain and before the knee: Ampify's equalizer (kit_audio.hpp). `key` names the
// stream or the spatial object the samples belong to, so the stage keeps one filter state for each.
class SampleStage {
public:
    virtual ~SampleStage() = default;

    // False while the stage would leave the samples unchanged. It is then not run at all.
    virtual bool Active() = 0;
    virtual void Run(const void* key, float* interleaved, size_t frames, int channels) = 0;
};

// The gain from `curve` (skipped when `unity`), then `stage` when it is active, then the knee. With a unity curve
// and no active stage the samples are left exactly as they were, as when ApplyCurve is not called.
void ApplyCurve(float* samples, size_t frames, int channels, const std::vector<float>& curve, bool unity,
                SampleStage* stage, const void* key);

}  // namespace atmos
