#include "object_pass.hpp"

#include <algorithm>
#include <cmath>

namespace ampify::audio {

namespace {

// Apple Music opens a new spatial stream at each seek, and the Dolby Atmos renderer plays its first 0.8 s 10 to 15 dB
// louder than the same passage played through (measured on Dolby Atmos for Headphones, 2026-11-09). Held down, then
// brought up over the rest of that time, a seek or an A-B loop return sounds even.
constexpr double kFadeInSeconds = 0.8;
constexpr double kFadeInHoldSeconds = 0.4;
constexpr double kFadeInFloorDb = -12.0;

}  // namespace

ObjectPass::ObjectPass(double sampleRate, bool fadeIn)
    : m_sampleRate(sampleRate), m_fadeIn(fadeIn), m_ramp(sampleRate) {}

bool ObjectPass::FadeIn(size_t frames) {
    const auto hold = static_cast<size_t>(kFadeInHoldSeconds * m_sampleRate);
    const auto total = static_cast<size_t>(kFadeInSeconds * m_sampleRate);
    if (frames == 0 || m_elapsed >= total) return false;
    for (size_t frame = 0; frame < frames && m_elapsed + frame < total; frame++) {
        const size_t at = m_elapsed + frame;
        const double rise = at < hold ? 0.0 : static_cast<double>(at - hold) / static_cast<double>(total - hold);
        m_curve[frame] *= static_cast<float>(std::pow(10.0, kFadeInFloorDb * (1.0 - rise) / 20.0));
    }
    return true;
}

const PassLevels& ObjectPass::Process(std::span<const ObjectBuffer> objects, size_t frames, double targetLinear,
                                      SampleStage* stage) {
    m_levels.framePower.assign(frames, 0.0);
    m_levels.peak = 0;
    bool unity = m_ramp.Curve(frames, targetLinear, m_curve);
    // With the leveler on, a new stream fades in from its first frame: an Atmos song after a stereo one opens its
    // stream before the leveler knows it is Atmos and sends its gain, and waiting for that gain let the renderer's
    // loud start through. Otherwise only a gained stream fades in: Ampify leaves the samples as Apple made them.
    if ((m_fadeIn || !unity) && FadeIn(frames)) unity = false;
    m_elapsed += frames;

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
        ApplyCurve(object.samples, frames, 1, m_curve, unity, stage, object.key);
    }
    return m_levels;
}

}  // namespace ampify::audio
