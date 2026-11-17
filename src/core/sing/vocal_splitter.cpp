#include "vocal_splitter.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "signalsmith-linear/fft.h"

namespace ampify::sing {

namespace {

constexpr size_t kPad = kFftSize / 2;                     // torch.stft's centre padding
constexpr size_t kPaddedSize = kChunk + 2 * kPad;         // also the inverse's overlap-add length
constexpr size_t kHalfBins = kFftSize / 2;                // complex bins of the real FFT; Nyquist rides bin 0
constexpr double kMinNorm = 1e-8;

}  // namespace

VocalModel::~VocalModel() = default;

// Signalsmith's real FFT: 7680 is 2 x 256 x 15, which it runs as power-of-two FFTs and a final generic radix-15
// pass. The tests check the result against numpy's.
struct VocalSplitter::Fft {
    signalsmith::linear::RealFFT<float> real{kFftSize};
    std::vector<std::complex<float>> bins = std::vector<std::complex<float>>(kHalfBins);
};

VocalSplitter::VocalSplitter(VocalModel& model)
    : m_model(model),
      m_fft(std::make_unique<Fft>()),
      m_window(kFftSize),
      m_padded(kPaddedSize),
      m_frame(kFftSize),
      m_overlap(kPaddedSize),
      m_inverseNorm(kChunk),
      m_vocals(kSpectrumSize) {
    for (size_t i = 0; i < kFftSize; i++) {
        m_window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / kFftSize));  // periodic
    }
    std::vector<double> norm(kPaddedSize, 0.0);
    for (size_t frame = 0; frame < kFrames; frame++) {
        for (size_t i = 0; i < kFftSize; i++) norm[frame * kFftHop + i] += double(m_window[i]) * m_window[i];
    }
    for (size_t i = 0; i < kChunk; i++) m_inverseNorm[i] = static_cast<float>(1.0 / std::max(norm[kPad + i], kMinNorm));
}

VocalSplitter::~VocalSplitter() = default;

bool VocalSplitter::Vocals(const float* left, const float* right, float* outLeft, float* outRight) {
    return Vocals(left, right, m_own, kNewWindow, 0, outLeft, outRight);
}

bool VocalSplitter::Vocals(const float* left, const float* right, Spectrum& last, size_t shift, size_t from,
                           float* outLeft, float* outRight) {
    if (last.values.size() != kSpectrumSize) {
        last.values.assign(kSpectrumSize, 0.0f);
        last.analysed = false;
    }
    AnalyseFrames(left, right, last.values.data(), last.analysed ? shift : kNewWindow);
    last.analysed = true;
    for (size_t channel = 0; channel < 4; channel++) {
        float* plane = last.values.data() + channel * kChannelPlane;
        std::fill(plane, plane + kClearedLowBins * kFrames, 0.0f);
    }
    if (!m_model.Run(last.values.data(), m_vocals.data())) return false;
    from = std::min(from, kChunk);
    SynthesiseChannel(m_vocals.data(), m_vocals.data() + kChannelPlane, outLeft, from);
    SynthesiseChannel(m_vocals.data() + 2 * kChannelPlane, m_vocals.data() + 3 * kChannelPlane, outRight, from);
    return true;
}

void VocalSplitter::Analyse(const float* left, const float* right, float* spectrum) {
    AnalyseFrames(left, right, spectrum, kNewWindow);
}

void VocalSplitter::Synthesise(const float* spectrum, float* left, float* right) {
    SynthesiseChannel(spectrum, spectrum + kChannelPlane, left, 0);
    SynthesiseChannel(spectrum + 2 * kChannelPlane, spectrum + 3 * kChannelPlane, right, 0);
}

void VocalSplitter::AnalyseFrames(const float* left, const float* right, float* spectrum, size_t shift) {
    AnalyseChannel(left, spectrum, spectrum + kChannelPlane, shift);
    AnalyseChannel(right, spectrum + 2 * kChannelPlane, spectrum + 3 * kChannelPlane, shift);
}

namespace {

constexpr size_t kLeftEdgeFrames = (kPad + kFftHop - 1) / kFftHop;  // frames that read the reflected start

// The first frame that reads any of the last `shift` hops of the window, or the reflected end.
size_t FirstChangedFrame(size_t shift) {
    const size_t end = kPad + kChunk;  // the reflected end starts there, in the padded window
    const size_t reach = shift * kFftHop + kFftSize;
    return reach >= end ? 0 : (end - reach) / kFftHop + 1;
}

// Moves every row of one plane `shift` frames to the left: frame f of the new window is frame f + shift of the old.
void ShiftRows(float* plane, size_t shift) {
    for (size_t bin = 0; bin < kBins; bin++) {
        float* row = plane + bin * kFrames;
        std::copy(row + shift, row + kFrames, row);
    }
}

}  // namespace

void VocalSplitter::AnalyseChannel(const float* samples, float* real, float* imag, size_t shift) {
    // numpy's "reflect": the edge sample itself is not repeated.
    std::copy(samples, samples + kChunk, m_padded.begin() + kPad);
    for (size_t k = 1; k <= kPad; k++) {
        m_padded[kPad - k] = samples[k];
        m_padded[kPad + kChunk - 1 + k] = samples[kChunk - 1 - k];
    }
    const bool whole = shift == 0 || shift >= kFrames;
    const size_t changed = whole ? 0 : std::max(FirstChangedFrame(shift), kLeftEdgeFrames);
    if (!whole) {
        ShiftRows(real, shift);
        ShiftRows(imag, shift);
    }
    std::complex<float>* bins = m_fft->bins.data();
    for (size_t frame = 0; frame < kFrames; frame++) {
        if (frame == kLeftEdgeFrames && frame < changed) frame = changed;  // taken over from the last window
        const float* from = m_padded.data() + frame * kFftHop;
        for (size_t i = 0; i < kFftSize; i++) m_frame[i] = from[i] * m_window[i];
        m_fft->real.fft(m_frame.data(), bins);
        real[frame] = bins[0].real();  // bin 0 carries the Nyquist bin in its imaginary part: not ours
        imag[frame] = 0.0f;
        for (size_t bin = 1; bin < kBins; bin++) {
            real[bin * kFrames + frame] = bins[bin].real();
            imag[bin * kFrames + frame] = bins[bin].imag();
        }
    }
}

void VocalSplitter::SynthesiseChannel(const float* real, const float* imag, float* samples, size_t from) {
    // Only the frames that reach sample `from` or later.
    const size_t start = kPad + from;
    const size_t first = start >= kFftSize ? (start - kFftSize) / kFftHop + 1 : 0;
    std::fill(m_overlap.begin() + static_cast<std::ptrdiff_t>(first * kFftHop), m_overlap.end(), 0.0f);
    std::complex<float>* bins = m_fft->bins.data();
    std::fill(bins + kBins, bins + kHalfBins, std::complex<float>{});
    const float scale = 1.0f / static_cast<float>(kFftSize);  // the inverse is unnormalised, numpy's irfft is not
    for (size_t frame = first; frame < kFrames; frame++) {
        bins[0] = {real[frame], 0.0f};  // irfft ignores the DC bin's imaginary part; Nyquist is zero
        for (size_t bin = 1; bin < kBins; bin++) bins[bin] = {real[bin * kFrames + frame], imag[bin * kFrames + frame]};
        m_fft->real.ifft(bins, m_frame.data());
        float* to = m_overlap.data() + frame * kFftHop;
        for (size_t i = 0; i < kFftSize; i++) to[i] += m_frame[i] * m_window[i] * scale;
    }
    for (size_t i = from; i < kChunk; i++) samples[i] = m_overlap[kPad + i] * m_inverseNorm[i];
}

}  // namespace ampify::sing
