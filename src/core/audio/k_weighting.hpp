#pragma once

// ITU-R BS.1770 building blocks shared by every loudness measure: the K-weighting of one channel and the 100 ms chunks
// of K-weighted power. The live loudness (live_loudness.hpp) and the Atmos leveler's integrated meter both stand on it.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ampify::audio {

// A 997 Hz full-scale sine in one channel is -3.01 LUFS.
constexpr double kLoudnessOffset = -0.691;

// Mean K-weighted power to LUFS.
inline double PowerToLufs(double meanSquare) {
    return kLoudnessOffset + 10.0 * std::log10(std::max(meanSquare, 1e-12));
}

// BS.1770 K-weighting of one channel: a high shelf modelling the head, then the RLB high-pass.
// The coefficients are derived for the actual sample rate, as libebur128 does.
class KWeighting {
public:
    explicit KWeighting(double sampleRate);

    double Process(double x);
    void Reset();

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        double Process(double x);
    };

    Biquad m_shelf;
    Biquad m_highPass;
};

// K-weighted power of a stream, summed over its channels and cut into 100 ms chunks whatever the rate. Each finished
// chunk's mean power goes to the caller's sink. Not thread-safe.
class ChunkedPower {
public:
    ChunkedPower(double sampleRate, int channels);

    // Interleaved samples, each scaled by `gain` first. Returns the largest absolute scaled sample.
    template <typename Sink>
    float Weigh(const float* interleaved, size_t frames, float gain, Sink&& chunk);

    // Power already K-weighted and summed per frame (spatial objects), times `scale`.
    template <typename Sink>
    void Add(const double* framePower, size_t frames, double scale, Sink&& chunk);

    // Re-derives the weighting and the chunk length for another rate. The chunk being filled is dropped.
    void SetSampleRate(double sampleRate);
    double SampleRate() const { return m_sampleRate; }

    // Filter states and the chunk being filled.
    void Reset();

private:
    template <typename Sink>
    void AddFramePower(double power, Sink& chunk);

    double m_sampleRate;
    size_t m_chunkFrames;
    std::vector<KWeighting> m_filters;
    double m_chunkPower = 0;  // sum over the channels and frames of the chunk being filled
    size_t m_framesInChunk = 0;
};

template <typename Sink>
float ChunkedPower::Weigh(const float* interleaved, size_t frames, float gain, Sink&& chunk) {
    const size_t channels = m_filters.size();
    float peak = 0;
    for (size_t frame = 0; frame < frames; frame++) {
        const float* sample = interleaved + frame * channels;
        double power = 0;
        for (size_t channel = 0; channel < channels; channel++) {
            double x = static_cast<double>(sample[channel]) * gain;
            peak = std::max(peak, static_cast<float>(std::fabs(x)));
            double y = m_filters[channel].Process(x);
            power += y * y;
        }
        AddFramePower(power, chunk);
    }
    return peak;
}

template <typename Sink>
void ChunkedPower::Add(const double* framePower, size_t frames, double scale, Sink&& chunk) {
    for (size_t frame = 0; frame < frames; frame++) AddFramePower(framePower[frame] * scale, chunk);
}

template <typename Sink>
void ChunkedPower::AddFramePower(double power, Sink& chunk) {
    m_chunkPower += power;
    if (++m_framesInChunk < m_chunkFrames) return;
    chunk(m_chunkPower / static_cast<double>(m_chunkFrames));
    m_chunkPower = 0;
    m_framesInChunk = 0;
}

}  // namespace ampify::audio
