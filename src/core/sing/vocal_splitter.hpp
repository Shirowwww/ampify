#pragma once

// One window of the vocal model: 44.1 kHz stereo in, its vocals out, with the STFT UVR uses (torch.stft with
// center=True, reflect padding, periodic Hann window, no normalisation) and its inverse (overlap-add divided by the
// summed squared window). Every buffer is made once, here: a call allocates nothing.

#include <memory>
#include <vector>

#include "vocal_model.hpp"

namespace ampify::sing {

class VocalSplitter {
public:
    explicit VocalSplitter(VocalModel& model);
    ~VocalSplitter();
    VocalSplitter(const VocalSplitter&) = delete;
    VocalSplitter& operator=(const VocalSplitter&) = delete;

    // kChunk samples per channel in and out. False when the model failed; the outputs are then left as they were.
    bool Vocals(const float* left, const float* right, float* outLeft, float* outRight);

    // The two halves of Vocals, apart: kChunk samples per channel <-> kSpectrumSize floats.
    void Analyse(const float* left, const float* right, float* spectrum);
    void Synthesise(const float* spectrum, float* left, float* right);

private:
    struct Fft;

    void AnalyseChannel(const float* samples, float* real, float* imag);
    void SynthesiseChannel(const float* real, const float* imag, float* samples);

    VocalModel& m_model;
    std::unique_ptr<Fft> m_fft;
    std::vector<float> m_window;
    std::vector<float> m_padded;       // one channel with its reflected edges
    std::vector<float> m_frame;
    std::vector<float> m_overlap;      // the inverse's overlap-add, padding included
    std::vector<float> m_inverseNorm;  // 1 / summed squared window, over the kept samples
    std::vector<float> m_spectrum;
    std::vector<float> m_vocals;
};

}  // namespace ampify::sing
