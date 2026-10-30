#pragma once

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

#include "gain_ramp.hpp"
#include "loudness_meter.hpp"

namespace atmos {

// One spatial audio object's mono buffer for the current update pass. `key` identifies the object
// across passes, such as its address.
struct ObjectBuffer {
    const void* key = nullptr;
    float* samples = nullptr;  // `frames` mono float32 samples: read, then gained in place
};

// What a pass measured, before its gain: the power of all objects summed frame by frame, and the peak.
struct PassLevels {
    std::vector<double> framePower;
    float peak = 0;
};

// Metering and gain of Dolby Atmos spatial objects, one update pass at a time. Each object is one
// channel of weight 1 in BS.1770 terms, so the summed power is the loudness of the whole mix.
class ObjectPass {
public:
    static constexpr size_t kMaxObjects = 256;

    explicit ObjectPass(double sampleRate);

    // Meters every object, then applies the gain eased towards `targetLinear`, then `stage` when one is given
    // (the kit's equalizer, keyed by object), then the knee. A buffer is read before it is gained, so the
    // measurement never sees the gain.
    const PassLevels& Process(std::span<const ObjectBuffer> objects, size_t frames, double targetLinear,
                              SampleStage* stage = nullptr);

private:
    double m_sampleRate;
    GainRamp m_ramp;
    std::vector<float> m_curve;
    std::unordered_map<const void*, KWeighting> m_filters;  // per object: the filter state carries over
    PassLevels m_levels;
};

}  // namespace atmos
