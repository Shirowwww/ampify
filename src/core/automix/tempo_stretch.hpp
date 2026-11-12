#pragma once

// Plays stereo sound faster or slower without changing its pitch: Signalsmith Stretch (vendor/signalsmith, MIT)
// behind a small face, so that its templates are compiled once. One thread at a time; nothing is allocated after the
// constructor.

#include <cstddef>
#include <memory>

namespace ampify::automix {

class TempoStretch {
public:
    explicit TempoStretch(int sampleRate);
    ~TempoStretch();
    TempoStretch(const TempoStretch&) = delete;
    TempoStretch& operator=(const TempoStretch&) = delete;

    // How far the sound that comes out is behind the sound that goes in, in frames, at normal speed.
    int Latency() const;

    void Reset();

    // Takes inFrames of interleaved stereo and gives outFrames: the speed is inFrames / outFrames, and it is the
    // caller who makes the counts add up to the speed wanted over time.
    void Process(const float* in, size_t inFrames, float* out, size_t outFrames);

private:
    struct Engine;
    std::unique_ptr<Engine> m_engine;
};

}  // namespace ampify::automix
