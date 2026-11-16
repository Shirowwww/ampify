#pragma once

// A streaming rational resampler for stereo: polyphase Kaiser-windowed sinc, at least 80 dB of stopband, cut off at
// 0.9 of the lower Nyquist (flat to 0.8 of it). Made for the rates Apple Music plays at (44.1, 48, 88.2, 96, 176.4,
// 192 kHz), between any two of them.
//
// Its latency is a whole number of samples on both sides (a multiple of 1 / gcd(in, out) seconds), so a round trip
// through two of them is late by an exact number of samples, which a caller can take away to the sample.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ampify::sing {

class Resampler {
public:
    Resampler(int inRate, int outRate);

    // Interleaved stereo. `out` has room for MaxOutput(frames) frames; returns the frames written. After N input
    // frames in all, exactly ceil(N * outRate / inRate) have come out, the first Latency() input frames late.
    size_t Process(const float* in, size_t frames, float* out);
    size_t MaxOutput(size_t frames) const;
    void Reset();  // back to a fresh stream: silence before it

    int Latency() const { return m_latency; }  // input samples
    int OutputLatency() const;                 // the same delay in output samples, exact

private:
    void Push(float left, float right);
    void Emit(float* out) const;

    std::int64_t m_up = 1;    // L: output samples per gcd unit
    std::int64_t m_down = 1;  // M: input samples per gcd unit
    int m_half = 0;           // H: the kernel spans 2H input samples
    int m_latency = 0;
    std::vector<float> m_taps;  // m_up phases of 2H taps
    // The last m_size input samples per channel, written twice so they always read as one run.
    std::vector<float> m_left;
    std::vector<float> m_right;
    size_t m_size = 0;
    size_t m_at = 0;           // where the next input sample goes
    std::int64_t m_count = 0;  // input samples so far
    std::int64_t m_next = 0;   // the input sample after which the next output is due
    std::int64_t m_phase = 0;  // of the next output, in 1/L of an input sample
};

}  // namespace ampify::sing
