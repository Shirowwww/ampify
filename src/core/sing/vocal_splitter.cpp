#include "vocal_splitter.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "real_fft.hpp"

namespace ampify::sing {

namespace {

constexpr double kMinNorm = 1e-8;

}  // namespace

VocalModel::~VocalModel() = default;

// Frames transformed together. The spectrum is stored bin by bin (a row of frames per bin), so one frame alone
// touches every bin's row, kilobytes apart: a block's spectra are kept frame by frame here (in cache) and moved to or
// from the rows a short run at a time.
constexpr size_t kBlockFrames = 16;

struct VocalSplitter::Fft {
    std::unique_ptr<RealFft> real;
    // kBlockFrames spectra of half the FFT size each. Analyse fills them all; Synthesise zeroes the bins past the
    // model's before each inverse.
    std::vector<std::complex<float>> block;
};

VocalSplitter::VocalSplitter(VocalModel& model)
    : m_model(model),
      m_spec(model.spec),
      m_pad(m_spec.fftSize / 2),
      m_half(m_spec.fftSize / 2),
      m_edgeFrames((m_pad + m_spec.fftHop - 1) / m_spec.fftHop),
      m_fft(std::make_unique<Fft>()),
      m_window(m_spec.fftSize),
      m_padded(m_spec.Chunk() + 2 * m_pad),
      m_frame(m_spec.fftSize),
      m_overlap(m_spec.Chunk() + 2 * m_pad),
      m_inverseNorm(m_spec.Chunk()),
      m_vocals(m_spec.SpectrumSize()) {
    m_fft->real = RealFft::Make(m_spec.fftSize);
    m_fft->block.resize(kBlockFrames * m_half);
    const size_t n = m_spec.fftSize;
    for (size_t i = 0; i < n; i++) {
        m_window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / n));  // periodic
    }
    std::vector<double> norm(m_overlap.size(), 0.0);
    for (size_t frame = 0; frame < m_spec.frames; frame++) {
        for (size_t i = 0; i < n; i++) norm[frame * m_spec.fftHop + i] += double(m_window[i]) * m_window[i];
    }
    for (size_t i = 0; i < m_spec.Chunk(); i++) {
        m_inverseNorm[i] = static_cast<float>(1.0 / std::max(norm[m_pad + i], kMinNorm));
    }
}

VocalSplitter::~VocalSplitter() = default;

bool VocalSplitter::Vocals(const float* left, const float* right, float* outLeft, float* outRight) {
    return Vocals(left, right, m_own, kNewWindow, 0, outLeft, outRight);
}

bool VocalSplitter::Vocals(const float* left, const float* right, Spectrum& last, size_t shift, size_t from,
                           float* outLeft, float* outRight) {
    const size_t plane = m_spec.Plane();
    if (last.values.size() != m_spec.SpectrumSize()) {
        last.values.assign(m_spec.SpectrumSize(), 0.0f);
        last.analysed = false;
    }
    AnalyseFrames(left, right, last.values.data(), last.analysed ? shift : kNewWindow);
    last.analysed = true;
    for (size_t channel = 0; channel < 4; channel++) {
        float* values = last.values.data() + channel * plane;
        std::fill(values, values + m_spec.clearedLowBins * m_spec.frames, 0.0f);
    }
    if (!m_model.Run(last.values.data(), m_vocals.data())) return false;
    from = std::min(from, m_spec.Chunk());
    SynthesiseChannel(m_vocals.data(), m_vocals.data() + plane, outLeft, from);
    SynthesiseChannel(m_vocals.data() + 2 * plane, m_vocals.data() + 3 * plane, outRight, from);
    return true;
}

void VocalSplitter::Analyse(const float* left, const float* right, float* spectrum) {
    AnalyseFrames(left, right, spectrum, kNewWindow);
}

void VocalSplitter::Synthesise(const float* spectrum, float* left, float* right) {
    const size_t plane = m_spec.Plane();
    SynthesiseChannel(spectrum, spectrum + plane, left, 0);
    SynthesiseChannel(spectrum + 2 * plane, spectrum + 3 * plane, right, 0);
}

void VocalSplitter::AnalyseFrames(const float* left, const float* right, float* spectrum, size_t shift) {
    const size_t plane = m_spec.Plane();
    AnalyseChannel(left, spectrum, spectrum + plane, shift);
    AnalyseChannel(right, spectrum + 2 * plane, spectrum + 3 * plane, shift);
}

// The first frame that reads any of the last `shift` hops of the window, or the reflected end.
size_t VocalSplitter::FirstChangedFrame(size_t shift) const {
    const size_t end = m_pad + m_spec.Chunk();  // the reflected end starts there, in the padded window
    const size_t reach = shift * m_spec.fftHop + m_spec.fftSize;
    return reach >= end ? 0 : (end - reach) / m_spec.fftHop + 1;
}

// Moves every row of one plane `shift` frames to the left: frame f of the new window is frame f + shift of the old.
void VocalSplitter::ShiftRows(float* plane, size_t shift) const {
    for (size_t bin = 0; bin < m_spec.bins; bin++) {
        float* row = plane + bin * m_spec.frames;
        std::copy(row + shift, row + m_spec.frames, row);
    }
}

void VocalSplitter::AnalyseChannel(const float* samples, float* real, float* imag, size_t shift) {
    const size_t chunk = m_spec.Chunk();
    // numpy's "reflect": the edge sample itself is not repeated.
    std::copy(samples, samples + chunk, m_padded.begin() + static_cast<std::ptrdiff_t>(m_pad));
    for (size_t k = 1; k <= m_pad; k++) {
        m_padded[m_pad - k] = samples[k];
        m_padded[m_pad + chunk - 1 + k] = samples[chunk - 1 - k];
    }
    const size_t frames = m_spec.frames;
    const bool whole = shift == 0 || shift >= frames;
    if (whole) {
        AnalyseRange(real, imag, 0, frames);
        return;
    }
    // The frames that read the reflected start, then those from the first changed one on: the others are taken over.
    const size_t changed = std::max(FirstChangedFrame(shift), m_edgeFrames);
    ShiftRows(real, shift);
    ShiftRows(imag, shift);
    AnalyseRange(real, imag, 0, m_edgeFrames);
    AnalyseRange(real, imag, changed, frames);
}

// Frames [first, last) of m_padded into the rows, kBlockFrames at a time.
void VocalSplitter::AnalyseRange(float* real, float* imag, size_t first, size_t last) {
    std::complex<float>* block = m_fft->block.data();
    const size_t frames = m_spec.frames;
    const size_t below = std::min(m_spec.bins, m_half);  // bins of the FFT's own layout; m_half itself is Nyquist
    for (size_t start = first; start < last; start += kBlockFrames) {
        const size_t count = std::min(kBlockFrames, last - start);
        for (size_t k = 0; k < count; k++) {
            const float* from = m_padded.data() + (start + k) * m_spec.fftHop;
            for (size_t i = 0; i < m_spec.fftSize; i++) m_frame[i] = from[i] * m_window[i];
            m_fft->real->Forward(m_frame.data(), block + k * m_half);
        }
        // Bin 0 carries the Nyquist bin in its imaginary part.
        for (size_t k = 0; k < count; k++) {
            real[start + k] = block[k * m_half].real();
            imag[start + k] = 0.0f;
        }
        for (size_t bin = 1; bin < below; bin++) {
            float* rowReal = real + bin * frames + start;
            float* rowImag = imag + bin * frames + start;
            for (size_t k = 0; k < count; k++) {
                const std::complex<float> value = block[k * m_half + bin];
                rowReal[k] = value.real();
                rowImag[k] = value.imag();
            }
        }
        if (m_spec.bins > m_half) {
            float* rowReal = real + m_half * frames + start;
            float* rowImag = imag + m_half * frames + start;
            for (size_t k = 0; k < count; k++) {
                rowReal[k] = block[k * m_half].imag();
                rowImag[k] = 0.0f;
            }
        }
    }
}

void VocalSplitter::SynthesiseChannel(const float* real, const float* imag, float* samples, size_t from) {
    const size_t frames = m_spec.frames;
    const size_t n = m_spec.fftSize;
    const size_t below = std::min(m_spec.bins, m_half);
    const bool nyquist = m_spec.bins > m_half;
    // Only the frames that reach sample `from` or later.
    const size_t start = m_pad + from;
    const size_t first = start >= n ? (start - n) / m_spec.fftHop + 1 : 0;
    std::fill(m_overlap.begin() + static_cast<std::ptrdiff_t>(first * m_spec.fftHop), m_overlap.end(), 0.0f);
    std::complex<float>* block = m_fft->block.data();
    const float scale = 1.0f / static_cast<float>(n);  // the inverse is unnormalised, numpy's irfft is not
    for (size_t at = first; at < frames; at += kBlockFrames) {
        const size_t count = std::min(kBlockFrames, frames - at);
        for (size_t k = 0; k < count; k++) {
            // irfft ignores the imaginary parts of DC and Nyquist.
            block[k * m_half] = {real[at + k], nyquist ? real[m_half * frames + at + k] : 0.0f};
            std::fill(block + k * m_half + below, block + (k + 1) * m_half, std::complex<float>{});
        }
        for (size_t bin = 1; bin < below; bin++) {
            const float* rowReal = real + bin * frames + at;
            const float* rowImag = imag + bin * frames + at;
            for (size_t k = 0; k < count; k++) block[k * m_half + bin] = {rowReal[k], rowImag[k]};
        }
        for (size_t k = 0; k < count; k++) {
            m_fft->real->Inverse(block + k * m_half, m_frame.data());
            float* to = m_overlap.data() + (at + k) * m_spec.fftHop;
            for (size_t i = 0; i < n; i++) to[i] += m_frame[i] * m_window[i] * scale;
        }
    }
    for (size_t i = from; i < m_spec.Chunk(); i++) samples[i] = m_overlap[m_pad + i] * m_inverseNorm[i];
}

}  // namespace ampify::sing
