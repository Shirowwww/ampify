#include "object_pass.hpp"

#include <algorithm>
#include <cmath>

namespace atmos {

ObjectPass::ObjectPass(double sampleRate) : m_sampleRate(sampleRate), m_ramp(sampleRate) {}

const PassLevels& ObjectPass::Process(std::span<const ObjectBuffer> objects, size_t frames, double targetLinear) {
    m_levels.framePower.assign(frames, 0.0);
    m_levels.peak = 0;
    const bool unity = m_ramp.Curve(frames, targetLinear, m_curve);

    // Objects come and go. Past the limit the filter states are dropped rather than the table grown
    // on the audio thread; the transient at the next few hops is the price.
    if (m_filters.size() > kMaxObjects) m_filters.clear();

    for (const ObjectBuffer& object : objects) {
        KWeighting& weighting = m_filters.try_emplace(object.key, m_sampleRate).first->second;
        for (size_t frame = 0; frame < frames; frame++) {
            float sample = object.samples[frame];
            m_levels.peak = std::max(m_levels.peak, std::fabs(sample));

            double weighted = weighting.Process(sample);
            m_levels.framePower[frame] += weighted * weighted;
        }
        if (!unity) ApplyCurve(object.samples, frames, 1, m_curve);
    }
    return m_levels;
}

}  // namespace atmos
