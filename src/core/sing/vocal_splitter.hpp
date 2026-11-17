#pragma once

// One window of a vocal model: 44.1 kHz stereo in, its vocals out, with the STFT of the model's spec (torch.stft with
// center=True, reflect padding, periodic Hann window, no normalisation) and its inverse (overlap-add divided by the
// summed squared window). Every buffer is made once, here: a call allocates nothing.

#include <memory>
#include <vector>

#include "vocal_model.hpp"

namespace ampify::sing {

class VocalSplitter {
public:
    explicit VocalSplitter(VocalModel& model);
    const ModelSpec& Spec() const { return m_spec; }
    ~VocalSplitter();
    VocalSplitter(const VocalSplitter&) = delete;
    VocalSplitter& operator=(const VocalSplitter&) = delete;

    // spec.Chunk() samples per channel in and out. False when the model failed; the outputs are then left as they were.
    bool Vocals(const float* left, const float* right, float* outLeft, float* outRight);

    // One stream's last window, kept between its windows (spec.SpectrumSize() floats, made at the first use).
    struct Spectrum {
        std::vector<float> values;
        bool analysed = false;
    };

    // The same for a window `shift` STFT hops later than `last`, with its vocals wanted for samples [from, chunk)
    // only: the frames both windows share are taken over, the rest computed, which gives the same spectrum as a whole
    // analysis. kNewWindow, or a `last` not analysed yet, computes it all.
    static constexpr size_t kNewWindow = static_cast<size_t>(-1);
    bool Vocals(const float* left, const float* right, Spectrum& last, size_t shift, size_t from, float* outLeft,
                float* outRight);

    // The two halves of Vocals, apart: spec.Chunk() samples per channel <-> spec.SpectrumSize() floats.
    void Analyse(const float* left, const float* right, float* spectrum);
    void Synthesise(const float* spectrum, float* left, float* right);

private:
    struct Fft;
    size_t FirstChangedFrame(size_t shift) const;
    void ShiftRows(float* plane, size_t shift) const;

    void AnalyseFrames(const float* left, const float* right, float* spectrum, size_t shift);
    void AnalyseChannel(const float* samples, float* real, float* imag, size_t shift);
    void AnalyseRange(float* real, float* imag, size_t first, size_t last);
    void SynthesiseChannel(const float* real, const float* imag, float* samples, size_t from);

    VocalModel& m_model;
    const ModelSpec& m_spec;
    size_t m_pad;         // torch.stft's centre padding, n_fft / 2
    size_t m_half;        // complex bins of the real FFT; the Nyquist bin rides bin 0
    size_t m_edgeFrames;  // frames that read the reflected start
    std::unique_ptr<Fft> m_fft;
    std::vector<float> m_window;
    std::vector<float> m_padded;       // one channel with its reflected edges
    std::vector<float> m_frame;
    std::vector<float> m_overlap;      // the inverse's overlap-add, padding included
    std::vector<float> m_inverseNorm;  // 1 / summed squared window, over the kept samples
    Spectrum m_own;  // for the plain Vocals
    std::vector<float> m_vocals;
};

}  // namespace ampify::sing
