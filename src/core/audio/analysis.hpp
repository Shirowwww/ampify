#pragma once

// Analysis of the decoded audio for Ampify.audio: levels, a spectrum in log-spaced bands, and a tempo estimate.
// Pure C++, no Windows API: the kernel feeds it the samples its own hooks already see (never Apple's audio tap,
// which Apple skips for Dolby Atmos). Everything stays in memory: only small frames of numbers leave this file.

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ampify::audio {

// RMS and peak (linear, 1 = full scale) of `count` samples.
struct Levels {
    float rms = 0;
    float peak = 0;
};
Levels MeasureLevels(const float* samples, size_t count);

// The periodic Hann window of `size` points.
std::vector<float> HannWindow(size_t size);

// Radix-2 complex FFT of a fixed power-of-two size, in place, forward only.
class Fft {
public:
    explicit Fft(size_t size);  // size is rounded up to a power of two, at least 2

    size_t Size() const { return m_size; }
    void Forward(std::vector<std::complex<float>>& data) const;  // data.size() must be Size()

private:
    size_t m_size;
    std::vector<size_t> m_reversed;
    std::vector<std::complex<float>> m_twiddles;
};

// Edges of `bands` log-spaced bands between lowHz and highHz: bands + 1 values, lowest first.
std::vector<double> LogBandEdges(size_t bands, double lowHz, double highHz);

// Level of each band of a block of mono samples, in dBFS: a full-scale sine reads about 0 dB in its band.
// The block is Hann-windowed; the power of the bins of a band is summed, so a band holds the energy of its range.
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer(size_t fftSize, size_t bands, double lowHz, double highHz);

    size_t FftSize() const { return m_fft.Size(); }
    size_t Bands() const { return m_bandCount; }

    // `block` holds FftSize() samples, oldest first. `bandsDb` gets Bands() values, floored at -120 dB.
    void Analyze(const float* block, double sampleRate, std::vector<float>& bandsDb);

    // Centre frequency of each band (geometric mean of its edges), in Hz.
    std::vector<double> Centres() const;

private:
    Fft m_fft;
    size_t m_bandCount;
    std::vector<double> m_edges;
    std::vector<float> m_window;
    std::vector<std::complex<float>> m_work;
};

struct TempoEstimate {
    double bpm = 0;         // 0 when not known yet
    double confidence = 0;  // 0..1: how far the best peak stands above the average of the range
};

// Tempo from an onset-strength envelope sampled at a fixed rate: autocorrelation over the last seconds, weighted
// towards the usual range of music (centred on 120 BPM), with sub-lag precision.
class TempoEstimator {
public:
    static constexpr double kEnvelopeRate = 100.0;  // onset values per second
    static constexpr double kMinBpm = 60.0;
    static constexpr double kMaxBpm = 200.0;

    explicit TempoEstimator(double seconds = 8.0);

    void Push(float onset);
    void Reset();

    // Seconds of envelope held, at most the window.
    double Seconds() const;

    // Needs at least 3 s of envelope.
    TempoEstimate Current() const;

private:
    size_t m_capacity;
    std::vector<float> m_ring;
    size_t m_next = 0;
    size_t m_count = 0;
};

// What the analysis publishes about every 50 ms of audio.
struct AnalysisFrame {
    std::vector<float> bandsDb;  // see SpectrumAnalyzer
    float rms = 0;               // over the last 50 ms, linear
    float peak = 0;
    double seconds = 0;  // audio time of the frame since the analysis started
};

// The whole analysis of one stream of samples: feed it interleaved blocks as they reach the output, read frames.
// Not thread-safe: the caller serialises calls.
class AudioAnalyzer {
public:
    static constexpr size_t kBands = 16;
    static constexpr double kFrameSeconds = 0.05;  // about 20 frames a second

    AudioAnalyzer();

    // `scale` multiplies the samples first (1 / player volume for a stream after the volume). The channels are
    // averaged to mono. A new sample rate restarts the analysis.
    void Feed(const float* interleaved, size_t frames, int channels, double sampleRate, float scale = 1.0f);

    // How many frames were produced since the start. The latest one is Latest().
    uint64_t FrameCount() const { return m_frameCount; }
    const AnalysisFrame& Latest() const { return m_latest; }

    TempoEstimate Tempo() const { return m_tempo.Current(); }
    double SampleRate() const { return m_rate; }
    std::vector<double> BandCentres() const;

    void Reset();

private:
    void Restart(double sampleRate);
    void PushSample(float sample);
    void Onset();
    void Frame();

    double m_rate = 0;
    size_t m_hop = 0;          // samples per onset value
    size_t m_frameLength = 0;  // samples per frame
    std::vector<float> m_ring;  // the last samples, at least the spectrum's FFT size
    size_t m_ringNext = 0;
    size_t m_sinceHop = 0;
    size_t m_sinceFrame = 0;
    double m_sumSquares = 0;  // over the frame being filled
    float m_framePeak = 0;
    uint64_t m_samples = 0;

    SpectrumAnalyzer m_spectrum;
    Fft m_onsetFft;
    std::vector<float> m_onsetWindow;
    std::vector<std::complex<float>> m_onsetWork;
    std::vector<float> m_previousMagnitudes;
    std::vector<float> m_block;
    TempoEstimator m_tempo;

    AnalysisFrame m_latest;
    uint64_t m_frameCount = 0;
};

}  // namespace ampify::audio
