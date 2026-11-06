#pragma once

// Bar heights for a theme's `content: bars(n)` (src/win/audio_bars.*), from the bands of the analysis. Portable: the
// tests drive it with plain band levels.

#include <cstddef>
#include <vector>

namespace ampify::audio {

// The heights of `count` bars, 0 to 1. The level follows the loudest band of the last seconds, so a quiet song moves
// the bars as much as a loud one. A bar rises at once and falls back over about a third of a second; with no fresh
// frame (paused, stopped), every bar falls to 0.
class BarMeter {
public:
    static constexpr size_t kMaxBars = 32;

    explicit BarMeter(size_t count);

    void Update(const std::vector<float>* bandsDb, double elapsedSeconds);
    const std::vector<float>& Levels() const { return m_levels; }

private:
    std::vector<float> m_levels;
    float m_reference = -30.0f;  // dB of the loudest band lately
};

}  // namespace ampify::audio
