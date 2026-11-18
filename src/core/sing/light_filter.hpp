#pragma once

// Sing's light mode: karaoke with no model, worked out on the processor. Each channel is split into frequencies
// (a short-time Fourier transform) and every frequency is turned down by how centred it is: the same sound on both
// sides at the same level (the singer, mixed in the middle) goes, a sound on one side or different on each stays.
// Below 133 Hz nothing changes (all of it is taken only from 247 Hz), so the bass and the kick stay, nor above 18 kHz
// (from 12 kHz less and less).
// The sides keep their own sound and phase, so the music does not turn hollow as it did when the older filter left
// each channel minus the middle (the side signal, out of phase between the speakers).
//
// Measured on 46 MUSDB18 clips with their true vocals (17/11/2026), against that older filter: the voice left goes
// from -9.4 dB to -15.8 dB, and the music's signal to distortion from 1.0 dB to 6.3 dB (medians).
//
// The analysis window is long (1024 samples at 44.1 or 48 kHz) for frequencies fine enough to tell the voice's
// harmonics from the rest, and the synthesis window short (Mauler and Martin's low-delay STFT, 2007), so the sound is
// only 2 hops late: 256 samples, 5.8 ms at 44.1 kHz, the same at every rate. A mono song (no side at all) cannot be
// told apart this way and would lose all but its bass: the filter then stands aside.

#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <vector>

namespace ampify::sing {

class RealFft;

class LightFilter {
public:
    LightFilter();
    ~LightFilter();
    LightFilter(const LightFilter&) = delete;
    LightFilter& operator=(const LightFilter&) = delete;

    // In place, interleaved stereo. `voice` 0..1: how much of the centre stays; `music` 0..1: how much of the rest
    // does (below 1, the singer stands out). The output lags the input by DelayFrames(rate); the first block after
    // Reset (or a rate change) crossfades from the input as it is.
    void Process(float* interleaved, size_t frames, int rate, double voice, double music = 1.0);
    // Forgets the song: the next block starts again from the input as it is.
    void Reset();

    static size_t DelayFrames(int rate);

private:
    void Design(int rate);
    void RunFrame();
    void Synthesize();

    int m_rate = 0;
    size_t m_size = 0;  // analysis window
    size_t m_hop = 0;
    std::unique_ptr<RealFft> m_fft;
    std::vector<float> m_analysis, m_synthesis, m_weight;  // windows, removal by frequency
    std::array<std::vector<float>, 2> m_input;               // the last m_size samples of each channel, a ring
    size_t m_inputAt = 0;                                     // the oldest sample in the rings
    std::vector<float> m_frame;
    std::array<std::vector<std::complex<float>>, 2> m_spectrum;
    std::vector<float> m_left, m_right;                       // smoothed powers by frequency
    std::vector<std::complex<float>> m_cross;
    std::array<std::vector<float>, 2> m_overlap;              // 2 hops of output being added up
    std::array<std::vector<float>, 2> m_ready;                // the hop being played
    size_t m_readyAt = 0;
    size_t m_sinceReset = 0;                                  // frames since Reset, for the crossfade in
    double m_voice = -1;                                      // smoothed by frame; below 0 until the first block
    double m_voiceTarget = 1;
    double m_music = -1;                                      // the same for the music
    double m_musicTarget = 1;
    double m_sideShare = -1;  // smoothed side to middle power, the mono check; below 0 until a frame has sound
};

}  // namespace ampify::sing
