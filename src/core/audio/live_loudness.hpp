#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "k_weighting.hpp"

namespace ampify::audio {

// Momentary (400 ms) and short-term (3 s) loudness of a live stream, in LUFS (ITU-R BS.1770, EBU R128 windows), over
// sliding windows, without gating, and with a fixed amount of memory, so it can run for hours. Feeds
// Ampify.audio.loudness(). Not thread-safe.
class LiveLoudness {
public:
    LiveLoudness(double sampleRate, int channels);

    // Interleaved samples. `gain` scales them first (1 / player volume).
    void Process(const float* interleaved, size_t frames, float gain = 1.0f);

    // Spatial objects: K-weighted power summed over the objects, per frame (ObjectPass), times `scale`.
    void ProcessPower(const double* framePower, size_t frames, double scale = 1.0);

    std::optional<double> MomentaryLufs() const;  // empty until 400 ms were measured
    std::optional<double> ShortTermLufs() const;  // empty until 3 s were measured

    // Re-derives the weighting for another rate. The windows start again.
    void SetSampleRate(double sampleRate);
    double SampleRate() const { return m_power.SampleRate(); }

    void Reset();

private:
    static constexpr size_t kShortTermChunks = 30;
    static constexpr size_t kMomentaryChunks = 4;

    void AddChunk(double meanPower);
    std::optional<double> Window(size_t chunks) const;

    ChunkedPower m_power;
    std::array<double, kShortTermChunks> m_chunks{};  // mean power of the last chunks, a ring
    size_t m_next = 0;
    size_t m_count = 0;
};

}  // namespace ampify::audio
