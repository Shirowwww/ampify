#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace atmos {

// Integrated loudness (ITU-R BS.1770-4, in LUFS) of interleaved float audio.
//
// Each channel is K-weighted, then measured in 100 ms chunks. A 400 ms block is four chunks
// (75 % overlap). Blocks are gated at -70 LUFS, then at -10 LU below the average of the blocks
// that passed the first gate. The filter is re-derived for the actual sample rate, as libebur128 does.
class LoudnessMeter {
public:
    LoudnessMeter(double sampleRate, int channels);

    // `gain` scales the samples before measuring. The leveler passes 1 / player volume, so the
    // loudness does not depend on where the slider sits. The peak is taken after `gain` too.
    void Process(const float* interleaved, size_t frames, float gain = 1.0f);

    // Empty until at least one 400 ms block passes the absolute gate.
    std::optional<double> IntegratedLufs() const;

    // Seconds of audio processed since the last Reset.
    double Seconds() const;

    // Largest absolute sample seen since the last Reset, after `gain`.
    float Peak() const;

    void Reset();

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        double Process(double x);
    };

    struct ChannelFilter {
        Biquad shelf;
        Biquad highPass;
    };

    double m_sampleRate;
    size_t m_chunkFrames;

    std::vector<ChannelFilter> m_filters;
    std::vector<double> m_chunkSquares;  // per channel, for the chunk being filled
    size_t m_framesInChunk = 0;
    std::vector<double> m_chunkPowers;   // channel-summed mean square, one entry per 100 ms

    size_t m_frames = 0;
    float m_peak = 0;
};

}  // namespace atmos
