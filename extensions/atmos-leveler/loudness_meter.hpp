#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "core/audio/k_weighting.hpp"

namespace atmos {

// Integrated loudness (ITU-R BS.1770-4, in LUFS) of interleaved float audio.
//
// Each channel is K-weighted, then measured in 100 ms chunks (ampify::audio::ChunkedPower). A 400 ms block is four
// chunks (75 % overlap). Blocks are gated at -70 LUFS, then at -10 LU below the average of the blocks that passed the
// first gate.
class LoudnessMeter {
public:
    LoudnessMeter(double sampleRate, int channels);

    // `gain` scales the samples before measuring. The leveler passes 1 / player volume, so the
    // loudness does not depend on where the slider sits. The peak is taken after `gain` too.
    void Process(const float* interleaved, size_t frames, float gain = 1.0f);

    // Spatial audio objects: the caller K-weighted each object and summed the squares per frame
    // (each object is a channel of weight 1). `scale` multiplies those sums, `peak` is the largest
    // absolute sample after the same gain. Do not mix with Process on one meter.
    void ProcessPower(const double* framePower, size_t frames, double scale, float peak);

    // Empty until at least one 400 ms block passes the absolute gate.
    std::optional<double> IntegratedLufs() const;

    // Seconds of audio processed since the last Reset.
    double Seconds() const;

    // Largest absolute sample seen since the last Reset, after `gain`.
    float Peak() const;

    // Re-derives the weighting and the chunk length for a stream at another rate. What was measured is kept:
    // a chunk is 100 ms whatever the rate, so the blocks before and after the change gate together. Only the
    // chunk being filled is dropped.
    void SetSampleRate(double sampleRate);

    double SampleRate() const;

    void Reset();

private:
    ampify::audio::ChunkedPower m_power;
    std::vector<double> m_chunkPowers;  // mean power per 100 ms chunk

    double m_seconds = 0;  // frames / rate, summed: stays right across a rate change
    float m_peak = 0;
};

}  // namespace atmos
