#include "bar_meter.hpp"

#include <algorithm>
#include <cstddef>

namespace ampify::audio {

namespace {

constexpr float kRange = 42.0f;        // dB from the reference down to an empty bar
constexpr float kReferenceFloor = -60.0f;
constexpr float kReferenceFall = 4.0f;  // dB per second: a song that gets quieter fills the bars again within seconds
constexpr float kBarFall = 3.0f;       // of the full height per second

}  // namespace

BarMeter::BarMeter(size_t count) : m_levels(std::clamp<size_t>(count, 1, kMaxBars), 0.0f) {}

void BarMeter::Update(const std::vector<float>* bandsDb, double elapsedSeconds) {
    const float dt = static_cast<float>(std::clamp(elapsedSeconds, 0.0, 0.5));
    const size_t bars = m_levels.size();
    if (!bandsDb || bandsDb->empty()) {
        for (float& level : m_levels) level = std::max(0.0f, level - kBarFall * dt);
        return;
    }
    const size_t bands = bandsDb->size();
    const float loudest = *std::max_element(bandsDb->begin(), bandsDb->end());
    m_reference = std::max({loudest, m_reference - kReferenceFall * dt, kReferenceFloor});
    for (size_t i = 0; i < bars; i++) {
        // Each bar takes the loudest of its bands; with more bars than bands, neighbours share one.
        size_t first = i * bands / bars;
        size_t last = std::max(first + 1, (i + 1) * bands / bars);
        float db = *std::max_element(bandsDb->begin() + static_cast<std::ptrdiff_t>(first),
                                     bandsDb->begin() + static_cast<std::ptrdiff_t>(std::min(last, bands)));
        float target = std::clamp((db - (m_reference - kRange)) / kRange, 0.0f, 1.0f);
        target = target * target;  // the quiet end stays low, so the beats stand out
        m_levels[i] = std::max(target, m_levels[i] - kBarFall * dt);
    }
}

}  // namespace ampify::audio
