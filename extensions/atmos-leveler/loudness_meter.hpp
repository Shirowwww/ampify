#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace atmos {

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

// Integrated loudness (ITU-R BS.1770-4, in LUFS) of interleaved float audio.
//
// Each channel is K-weighted, then measured in 100 ms chunks. A 400 ms block is four chunks
// (75 % overlap). Blocks are gated at -70 LUFS, then at -10 LU below the average of the blocks
// that passed the first gate.
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

    void Reset();

private:
    void AddFramePower(double power);

    double m_sampleRate;
    size_t m_chunkFrames;

    std::vector<KWeighting> m_filters;
    double m_chunkPower = 0;  // sum over the channels and frames of the chunk being filled
    size_t m_framesInChunk = 0;
    std::vector<double> m_chunkPowers;  // mean power per 100 ms chunk

    size_t m_frames = 0;
    float m_peak = 0;
};

}  // namespace atmos
