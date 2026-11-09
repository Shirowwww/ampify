#include "analysis.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ampify::audio {

namespace {

constexpr float kFloorDb = -120.0f;
constexpr size_t kSpectrumFftSize = 2048;          // up to 48 kHz: about 43 ms
constexpr size_t kHighRateSpectrumFftSize = 4096;  // above 60 kHz, for the same 43 ms
constexpr size_t kRingSize = kHighRateSpectrumFftSize;  // holds the largest block the spectrum reads
constexpr size_t kOnsetFftSize = 1024;  // about 21 ms at 48 kHz: enough to see a beat arrive
static_assert(kRingSize >= kOnsetFftSize, "a block longer than the ring would read wrapped data");
constexpr double kLowHz = 40.0;
constexpr double kHighHz = 16000.0;

size_t NextPowerOfTwo(size_t value) {
    size_t size = 2;
    while (size < value) size <<= 1;
    return size;
}

}  // namespace

Levels MeasureLevels(const float* samples, size_t count) {
    Levels levels;
    if (!samples || count == 0) return levels;
    double sum = 0;
    for (size_t i = 0; i < count; i++) {
        float x = samples[i];
        sum += static_cast<double>(x) * x;
        levels.peak = std::max(levels.peak, std::fabs(x));
    }
    levels.rms = static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
    return levels;
}

std::vector<float> HannWindow(size_t size) {
    std::vector<float> window(size);
    for (size_t i = 0; i < size; i++) {
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                                             static_cast<double>(size)));
    }
    return window;
}

Fft::Fft(size_t size) : m_size(NextPowerOfTwo(size)), m_reversed(m_size), m_twiddles(m_size / 2) {
    size_t bits = 0;
    while ((size_t{1} << bits) < m_size) bits++;
    for (size_t i = 0; i < m_size; i++) {
        size_t reversed = 0;
        for (size_t b = 0; b < bits; b++) {
            if (i & (size_t{1} << b)) reversed |= size_t{1} << (bits - 1 - b);
        }
        m_reversed[i] = reversed;
    }
    for (size_t k = 0; k < m_size / 2; k++) {
        double angle = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(m_size);
        m_twiddles[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
    }
}

void Fft::Forward(std::vector<std::complex<float>>& data) const {
    if (data.size() != m_size) return;
    for (size_t i = 0; i < m_size; i++) {
        if (i < m_reversed[i]) std::swap(data[i], data[m_reversed[i]]);
    }
    for (size_t length = 2; length <= m_size; length <<= 1) {
        size_t half = length / 2;
        size_t stride = m_size / length;
        for (size_t start = 0; start < m_size; start += length) {
            for (size_t k = 0; k < half; k++) {
                std::complex<float> odd = data[start + k + half] * m_twiddles[k * stride];
                std::complex<float> even = data[start + k];
                data[start + k] = even + odd;
                data[start + k + half] = even - odd;
            }
        }
    }
}

std::vector<double> LogBandEdges(size_t bands, double lowHz, double highHz) {
    bands = std::max<size_t>(1, bands);
    std::vector<double> edges(bands + 1);
    const double ratio = std::log(highHz / lowHz);
    for (size_t i = 0; i <= bands; i++) {
        edges[i] = lowHz * std::exp(ratio * static_cast<double>(i) / static_cast<double>(bands));
    }
    return edges;
}

SpectrumAnalyzer::SpectrumAnalyzer(size_t fftSize, size_t bands, double lowHz, double highHz)
    : m_fft(fftSize),
      m_bandCount(std::max<size_t>(1, bands)),
      m_edges(LogBandEdges(m_bandCount, lowHz, highHz)),
      m_window(HannWindow(m_fft.Size())),
      m_work(m_fft.Size()) {}

std::vector<double> SpectrumAnalyzer::Centres() const {
    std::vector<double> centres(m_bandCount);
    for (size_t i = 0; i < m_bandCount; i++) centres[i] = std::sqrt(m_edges[i] * m_edges[i + 1]);
    return centres;
}

void SpectrumAnalyzer::Analyze(const float* block, double sampleRate, std::vector<float>& bandsDb) {
    const size_t size = m_fft.Size();
    for (size_t i = 0; i < size; i++) m_work[i] = {block[i] * m_window[i], 0.0f};
    m_fft.Forward(m_work);

    // A sine of amplitude A gives 3 A^2 N^2 / 32 summed over its Hann main lobe: that sum reads 0 dB at A = 1.
    const double reference = 3.0 * static_cast<double>(size) * static_cast<double>(size) / 32.0;
    const double binHz = sampleRate / static_cast<double>(size);
    const size_t lastBin = size / 2;

    bandsDb.assign(m_bandCount, kFloorDb);
    for (size_t band = 0; band < m_bandCount; band++) {
        double low = m_edges[band];
        double high = std::min(m_edges[band + 1], sampleRate / 2.0);
        if (low >= high) continue;
        size_t first = static_cast<size_t>(std::ceil(low / binHz));
        size_t last = static_cast<size_t>(std::ceil(high / binHz));  // exclusive
        first = std::clamp<size_t>(first, 1, lastBin);
        last = std::clamp<size_t>(last, 1, lastBin + 1);
        if (first >= last) {
            // A band narrower than a bin takes the nearest bin, so the low bands are never empty.
            first = std::clamp<size_t>(static_cast<size_t>(std::lround(std::sqrt(low * high) / binHz)), 1, lastBin);
            last = first + 1;
        }
        double power = 0;
        for (size_t bin = first; bin < last; bin++) power += std::norm(m_work[bin]);
        double db = 10.0 * std::log10(std::max(power / reference, 1e-12));
        bandsDb[band] = std::max(kFloorDb, static_cast<float>(db));
    }
}

TempoEstimator::TempoEstimator(double seconds)
    : m_capacity(static_cast<size_t>(std::max(4.0, seconds) * kEnvelopeRate)), m_ring(m_capacity, 0.0f) {}

void TempoEstimator::Push(float onset) {
    m_ring[m_next] = onset;
    m_next = (m_next + 1) % m_capacity;
    m_count = std::min(m_count + 1, m_capacity);
}

void TempoEstimator::Reset() {
    std::fill(m_ring.begin(), m_ring.end(), 0.0f);
    m_next = 0;
    m_count = 0;
}

double TempoEstimator::Seconds() const { return static_cast<double>(m_count) / kEnvelopeRate; }

TempoEstimate TempoEstimator::Current() const {
    TempoEstimate estimate;
    if (Seconds() < 3.0) return estimate;

    // The envelope, oldest first, without its mean.
    std::vector<double> envelope(m_count);
    size_t start = (m_next + m_capacity - m_count) % m_capacity;
    double mean = 0;
    for (size_t i = 0; i < m_count; i++) {
        envelope[i] = m_ring[(start + i) % m_capacity];
        mean += envelope[i];
    }
    mean /= static_cast<double>(m_count);
    for (double& value : envelope) value -= mean;

    // A Gaussian of 20 ms: a beat whose period falls between two lags (160 BPM is 37.5 values) still peaks at
    // both, instead of losing to its exact half tempo.
    constexpr int kRadius = 6;
    constexpr double kSigma = 2.0;
    std::vector<double> smoothed(m_count, 0.0);
    for (size_t i = 0; i < m_count; i++) {
        double sum = 0, weights = 0;
        for (int k = -kRadius; k <= kRadius; k++) {
            long j = static_cast<long>(i) + k;
            if (j < 0 || j >= static_cast<long>(m_count)) continue;
            double w = std::exp(-0.5 * k * k / (kSigma * kSigma));
            sum += w * envelope[static_cast<size_t>(j)];
            weights += w;
        }
        smoothed[i] = sum / weights;
    }
    envelope.swap(smoothed);

    const size_t minLag = static_cast<size_t>(std::floor(60.0 * kEnvelopeRate / kMaxBpm));
    const size_t maxLag = static_cast<size_t>(std::ceil(60.0 * kEnvelopeRate / kMinBpm));
    if (maxLag + 2 >= m_count) return estimate;

    auto correlation = [&](size_t lag) {
        double sum = 0;
        for (size_t i = 0; i + lag < m_count; i++) sum += envelope[i] * envelope[i + lag];
        return sum / static_cast<double>(m_count - lag);  // unbiased: long lags are not penalised
    };
    const double energy = correlation(0);
    if (energy <= 1e-12) return estimate;

    std::vector<double> acf(maxLag + 2, 0.0);
    for (size_t lag = minLag - 1; lag <= maxLag + 1; lag++) acf[lag] = correlation(lag);

    // Weighted towards 120 BPM (a log-normal over about one octave), which settles the octave of the answer.
    size_t best = 0;
    double bestScore = 0;
    for (size_t lag = minLag; lag <= maxLag; lag++) {
        double bpm = 60.0 * kEnvelopeRate / static_cast<double>(lag);
        double octaves = std::log2(bpm / 120.0);
        double score = acf[lag] * std::exp(-0.5 * octaves * octaves);
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    if (best == 0) return estimate;

    // Parabola through the peak and its neighbours, for a tempo finer than one lag step.
    double left = acf[best - 1], centre = acf[best], right = acf[best + 1];
    double denominator = left - 2.0 * centre + right;
    double offset = std::fabs(denominator) > 1e-12 ? 0.5 * (left - right) / denominator : 0.0;
    double lag = static_cast<double>(best) + std::clamp(offset, -0.5, 0.5);
    estimate.bpm = 60.0 * kEnvelopeRate / lag;
    estimate.confidence = std::clamp(centre / energy, 0.0, 1.0);
    return estimate;
}

AudioAnalyzer::AudioAnalyzer()
    : m_ring(kRingSize, 0.0f),
      m_spectrum(kSpectrumFftSize, kBands, kLowHz, kHighHz),
      m_onsetFft(kOnsetFftSize),
      m_onsetWindow(HannWindow(kOnsetFftSize)),
      m_onsetWork(kOnsetFftSize),
      m_previousMagnitudes(kOnsetFftSize / 2 + 1, 0.0f) {}

std::vector<double> AudioAnalyzer::BandCentres() const { return m_spectrum.Centres(); }

void AudioAnalyzer::Reset() {
    double rate = m_rate;
    m_rate = 0;
    if (rate > 0) Restart(rate);
}

void AudioAnalyzer::Restart(double sampleRate) {
    m_rate = sampleRate;
    m_hop = std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate / TempoEstimator::kEnvelopeRate)));
    m_frameLength = std::max<size_t>(1, static_cast<size_t>(std::lround(sampleRate * kFrameSeconds)));
    // About 43 ms per spectrum block, whatever the rate: 2048 points up to 48 kHz, 4096 above.
    const size_t fftSize = sampleRate > 60000 ? kHighRateSpectrumFftSize : kSpectrumFftSize;
    m_spectrum = SpectrumAnalyzer(fftSize, kBands, kLowHz, std::min(kHighHz, sampleRate * 0.45));
    std::fill(m_ring.begin(), m_ring.end(), 0.0f);
    std::fill(m_previousMagnitudes.begin(), m_previousMagnitudes.end(), 0.0f);
    m_ringNext = 0;
    m_sinceHop = 0;
    m_sinceFrame = 0;
    m_sumSquares = 0;
    m_framePeak = 0;
    m_samples = 0;
    m_tempo.Reset();
}

void AudioAnalyzer::Feed(const float* interleaved, size_t frames, int channels, double sampleRate, float scale) {
    if (!interleaved || frames == 0 || channels <= 0 || sampleRate <= 0) return;
    if (sampleRate != m_rate) Restart(sampleRate);
    const size_t width = static_cast<size_t>(channels);
    const float mix = scale / static_cast<float>(channels);
    for (size_t frame = 0; frame < frames; frame++) {
        const float* sample = interleaved + frame * width;
        float sum = 0;
        for (size_t channel = 0; channel < width; channel++) sum += sample[channel];
        PushSample(sum * mix);
    }
}

void AudioAnalyzer::PushSample(float sample) {
    m_ring[m_ringNext] = sample;
    m_ringNext = (m_ringNext + 1) % kRingSize;
    m_samples++;
    m_sumSquares += static_cast<double>(sample) * sample;
    m_framePeak = std::max(m_framePeak, std::fabs(sample));
    if (++m_sinceHop >= m_hop) {
        m_sinceHop = 0;
        Onset();
    }
    if (++m_sinceFrame >= m_frameLength) {
        m_sinceFrame = 0;
        Frame();
    }
}

// Spectral flux: how much the compressed magnitude of each bin rose since the last hop, summed.
void AudioAnalyzer::Onset() {
    size_t start = (m_ringNext + kRingSize - kOnsetFftSize) % kRingSize;
    for (size_t i = 0; i < kOnsetFftSize; i++) {
        m_onsetWork[i] = {m_ring[(start + i) % kRingSize] * m_onsetWindow[i], 0.0f};
    }
    m_onsetFft.Forward(m_onsetWork);
    const float norm = 4.0f / static_cast<float>(kOnsetFftSize);
    float flux = 0;
    for (size_t bin = 1; bin <= kOnsetFftSize / 2; bin++) {
        float magnitude = std::log1p(100.0f * std::abs(m_onsetWork[bin]) * norm);
        flux += std::max(0.0f, magnitude - m_previousMagnitudes[bin]);
        m_previousMagnitudes[bin] = magnitude;
    }
    m_tempo.Push(flux);
}

void AudioAnalyzer::Frame() {
    const size_t size = m_spectrum.FftSize();
    m_block.resize(size);
    size_t start = (m_ringNext + kRingSize - size) % kRingSize;
    for (size_t i = 0; i < size; i++) m_block[i] = m_ring[(start + i) % kRingSize];
    m_spectrum.Analyze(m_block.data(), m_rate, m_latest.bandsDb);

    m_latest.rms = static_cast<float>(std::sqrt(m_sumSquares / static_cast<double>(m_frameLength)));
    m_latest.peak = m_framePeak;
    m_latest.seconds = static_cast<double>(m_samples) / m_rate;
    m_sumSquares = 0;
    m_framePeak = 0;
    m_frameCount++;
}

}  // namespace ampify::audio
