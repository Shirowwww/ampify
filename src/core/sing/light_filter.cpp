#include "light_filter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "real_fft.hpp"

namespace ampify::sing {

namespace {

// The analysis window at 44.1 and 48 kHz, twice that at 88.2 and 96 kHz, four times that from 176.4 kHz.
constexpr size_t kBaseSize = 1024;
constexpr size_t kBaseHop = 128;
// The removal by frequency rises from nothing at 133 Hz to all of it at 247 Hz: the bass, the kick and most of a low
// voice's fundamental stay (measured: lower took more of the bass than of the voice, higher left more voice).
constexpr double kLowStartHz = 133;
constexpr double kLowFullHz = 247;
// And falls back to nothing from 12 to 18 kHz, above the voice's sibilants: the cymbals keep their air (as much of the
// voice taken, a little more of the music kept).
constexpr double kHighFullHz = 12000;
constexpr double kHighEndHz = 18000;
// Each frame's powers are averaged with the last ones (half and half): less of the warbling a mask makes when it
// moves frame by frame, for little of the voice.
constexpr float kSmoothing = 0.5f;
constexpr double kVoiceSeconds = 0.05;
// The mono check: side to middle power, from the first frame with sound, then averaged over about a second. The 47
// stereo clips measured never went below -22 dB (reverb alone puts something on the sides); a mono song has nothing
// there. The filter fades out from -35 to -45 dB.
constexpr double kShareSeconds = 1.0;
constexpr double kMonoDb = -45;
constexpr double kStereoDb = -35;
constexpr float kTiny = 1e-20f;

double Hann(size_t i, size_t length) {  // periodic
    return 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(length));
}

}  // namespace

LightFilter::LightFilter() = default;
LightFilter::~LightFilter() = default;

size_t LightFilter::DelayFrames(int rate) {
    const size_t scale = rate >= 176400 ? 4 : rate >= 88200 ? 2 : 1;
    return 2 * kBaseHop * scale;
}

void LightFilter::Reset() {
    for (auto& ring : m_input) std::fill(ring.begin(), ring.end(), 0.0f);
    for (auto& part : m_overlap) std::fill(part.begin(), part.end(), 0.0f);
    for (auto& part : m_ready) std::fill(part.begin(), part.end(), 0.0f);
    std::fill(m_left.begin(), m_left.end(), 0.0f);
    std::fill(m_right.begin(), m_right.end(), 0.0f);
    std::fill(m_cross.begin(), m_cross.end(), std::complex<float>{});
    m_inputAt = 0;
    m_readyAt = 0;
    m_sinceReset = 0;
    m_voice = -1;
    m_music = -1;
    m_sideShare = -1;
}

// Windows after Mauler and Martin: the analysis one is the square root of a long Hann window rising over all but the
// last hop, then of a short one falling over it; the synthesis one is non-zero over the last two hops only, where the
// two multiply to a Hann window of two hops, which adds up to one at that hop.
void LightFilter::Design(int rate) {
    const size_t scale = DelayFrames(rate) / (2 * kBaseHop);
    m_size = kBaseSize * scale;
    m_hop = kBaseHop * scale;
    const size_t n = m_size, m = m_hop, half = n / 2;
    m_fft = RealFft::Make(n);
    m_analysis.assign(n, 0.0f);
    m_synthesis.assign(n, 0.0f);
    for (size_t i = 0; i < n - m; i++) m_analysis[i] = static_cast<float>(std::sqrt(Hann(i, 2 * (n - m))));
    for (size_t i = n - m; i < n; i++) {
        const double fall = std::sqrt(Hann(i - (n - 2 * m), 2 * m));
        m_analysis[i] = static_cast<float>(fall);
        m_synthesis[i] = static_cast<float>(fall / static_cast<double>(n));  // the inverse FFT gives n times
    }
    for (size_t i = n - 2 * m; i < n - m; i++) {
        const double both = Hann(i - (n - 2 * m), 2 * m);
        m_synthesis[i] = static_cast<float>(both / std::sqrt(Hann(i, 2 * (n - m))) / static_cast<double>(n));
    }
    m_weight.assign(half, 0.0f);
    for (size_t k = 1; k < half; k++) {
        const double hz = static_cast<double>(k) * rate / static_cast<double>(n);
        const double rise = (hz - kLowStartHz) / (kLowFullHz - kLowStartHz);
        const double fall = (kHighEndHz - hz) / (kHighEndHz - kHighFullHz);
        m_weight[k] = static_cast<float>(std::clamp(std::min(rise, fall), 0.0, 1.0));
    }
    for (auto& ring : m_input) ring.assign(n, 0.0f);
    m_frame.assign(n, 0.0f);
    for (auto& bins : m_spectrum) bins.assign(half, {});
    m_left.assign(half, 0.0f);
    m_right.assign(half, 0.0f);
    m_cross.assign(half, {});
    for (auto& part : m_overlap) part.assign(2 * m, 0.0f);
    for (auto& part : m_ready) part.assign(m, 0.0f);
    m_rate = rate;
    Reset();
}

void LightFilter::RunFrame() {
    const size_t n = m_size, m = m_hop, half = n / 2;
    for (size_t channel = 0; channel < 2; channel++) {
        const std::vector<float>& ring = m_input[channel];
        const size_t head = n - m_inputAt;  // the oldest samples, up to the ring's end
        for (size_t i = 0; i < head; i++) m_frame[i] = ring[m_inputAt + i] * m_analysis[i];
        for (size_t i = head; i < n; i++) m_frame[i] = ring[i - head] * m_analysis[i];
        m_fft->Forward(m_frame.data(), m_spectrum[channel].data());
    }

    const double hopSeconds = static_cast<double>(m) / m_rate;
    const double step = hopSeconds / kVoiceSeconds;
    m_voice = m_voiceTarget > m_voice ? std::min(m_voiceTarget, m_voice + step)
                                      : std::max(m_voiceTarget, m_voice - step);
    m_music = m_musicTarget > m_music ? std::min(m_musicTarget, m_music + step)
                                      : std::max(m_musicTarget, m_music - step);
    const double shareDb = m_sideShare < 0 ? 0 : 10 * std::log10(std::max(m_sideShare, 1e-12));
    const auto stereo = static_cast<float>(std::clamp((shareDb - kMonoDb) / (kStereoDb - kMonoDb), 0.0, 1.0));
    const auto voiceKept = static_cast<float>(m_voice);
    const auto musicKept = static_cast<float>(m_music);

    double side = 0, middle = 0;
    std::complex<float>* left = m_spectrum[0].data();
    std::complex<float>* right = m_spectrum[1].data();
    for (size_t k = 1; k < half; k++) {  // bin 0 holds the DC and the Nyquist: left alone
        const std::complex<float> l = left[k], r = right[k];
        const float weight = m_weight[k];
        if (weight > 0) {
            side += std::norm(l - r);
            middle += std::norm(l + r);
        }
        m_left[k] = kSmoothing * m_left[k] + (1 - kSmoothing) * std::norm(l);
        m_right[k] = kSmoothing * m_right[k] + (1 - kSmoothing) * std::norm(r);
        m_cross[k] = kSmoothing * m_cross[k] + (1 - kSmoothing) * (l * std::conj(r));
        // How alike the two sides are, 0 to 1: 1 for the same sound at the same level on both. Its square root is
        // what is taken away, worked out from its square (no std::abs, which is a slow hypot).
        const float both = m_left[k] + m_right[k] + kTiny;
        const float alikeSquared = std::min(1.0f, 4 * std::norm(m_cross[k]) / (both * both));
        // The voice's part of the bin, kept at `voiceKept`, the rest at `musicKept`; a mono song is left as it is.
        const float voiceShare = weight * std::sqrt(std::sqrt(alikeSquared));
        const float kept = musicKept * (1 - voiceShare) + voiceKept * voiceShare;
        const float gain = 1 - stereo * (1 - kept);
        left[k] = l * gain;
        right[k] = r * gain;
    }
    if (middle > 1e-12) {
        const double keep = m_sideShare < 0 ? 0 : std::exp(-hopSeconds / kShareSeconds);
        m_sideShare = keep * m_sideShare + (1 - keep) * (side / middle);
    }
    Synthesize();
}

// Each channel's spectrum back to sound, added to the last frames over the synthesis window: one more hop is whole.
void LightFilter::Synthesize() {
    const size_t n = m_size, m = m_hop;
    for (size_t channel = 0; channel < 2; channel++) {
        m_fft->Inverse(m_spectrum[channel].data(), m_frame.data());
        std::vector<float>& overlap = m_overlap[channel];
        for (size_t i = 0; i < 2 * m; i++) overlap[i] += m_frame[n - 2 * m + i] * m_synthesis[n - 2 * m + i];
        std::copy(overlap.begin(), overlap.begin() + static_cast<std::ptrdiff_t>(m), m_ready[channel].begin());
        std::copy(overlap.begin() + static_cast<std::ptrdiff_t>(m), overlap.end(), overlap.begin());
        std::fill(overlap.begin() + static_cast<std::ptrdiff_t>(m), overlap.end(), 0.0f);
    }
}

void LightFilter::Process(float* interleaved, size_t frames, int rate, double voice, double music) {
    if (interleaved == nullptr || rate < 8000) return;
    if (rate != m_rate) Design(rate);  // allocates: once per rate, as Apple's stream starts
    m_voiceTarget = std::isfinite(voice) ? std::clamp(voice, 0.0, 1.0) : 1.0;
    m_musicTarget = std::isfinite(music) ? std::clamp(music, 0.0, 1.0) : 1.0;
    if (m_voice < 0) m_voice = m_voiceTarget;
    if (m_music < 0) m_music = m_musicTarget;
    const size_t n = m_size, m = m_hop;
    for (size_t i = 0; i < frames; i++) {
        float* sample = interleaved + i * 2;
        const float dry[2] = {sample[0], sample[1]};
        m_input[0][m_inputAt] = dry[0];
        m_input[1][m_inputAt] = dry[1];
        m_inputAt = m_inputAt + 1 == n ? 0 : m_inputAt + 1;
        const float wet[2] = {m_ready[0][m_readyAt], m_ready[1][m_readyAt]};
        // Nothing has come through the filter yet for the first 2 hops: the input as it is, then a crossfade of equal
        // power (the two are the same sound 2 hops apart, as good as unrelated over most of the spectrum).
        if (m_sinceReset < 4 * m) {
            const double into = m_sinceReset < 2 * m ? 0 : static_cast<double>(m_sinceReset - 2 * m + 1) / (2 * m);
            const auto drySide = static_cast<float>(std::cos(into * std::numbers::pi / 2));
            const auto wetSide = static_cast<float>(std::sin(into * std::numbers::pi / 2));
            sample[0] = drySide * dry[0] + wetSide * wet[0];
            sample[1] = drySide * dry[1] + wetSide * wet[1];
            m_sinceReset++;
        } else {
            sample[0] = wet[0];
            sample[1] = wet[1];
        }
        if (++m_readyAt == m) {
            RunFrame();
            m_readyAt = 0;
        }
    }
}

}  // namespace ampify::sing
