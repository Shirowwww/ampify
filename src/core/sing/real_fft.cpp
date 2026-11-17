#include "real_fft.hpp"

#include <cmath>
#include <numbers>

#include "signalsmith-linear/fft.h"

namespace ampify::sing {

struct RealFft7680::Third {
    signalsmith::linear::SplitFFT<float> fft{kThird};
};

namespace {

using Cf = std::complex<float>;

Cf Turn(double fraction) {
    const double angle = -2 * std::numbers::pi * fraction;
    return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
}

// Signalsmith's real FFT, for the sizes it is fast at (a power of two times 3 or 5 at most).
class AnyRealFft final : public RealFft {
public:
    explicit AnyRealFft(size_t size) : m_fft(size) {}
    void Forward(const float* time, Cf* freq) override { m_fft.fft(time, freq); }
    void Inverse(const Cf* freq, float* time) override { m_fft.ifft(freq, time); }

private:
    signalsmith::linear::RealFFT<float> m_fft;
};

}  // namespace

std::unique_ptr<RealFft> RealFft::Make(size_t size) {
    if (size == RealFft7680::kSize) return std::make_unique<RealFft7680>();
    return std::make_unique<AnyRealFft>(size);
}

RealFft7680::RealFft7680()
    : m_third(std::make_unique<Third>()),
      m_z(kHalf),
      m_parts(kHalf),
      m_partsOut(kHalf),
      m_twiddles(kHalf),
      m_unpack(kHalf / 2 + 1) {
    for (size_t k = 0; k < kHalf; k++) m_twiddles[k] = Turn(static_cast<double>(k) / kHalf);
    for (size_t k = 0; k < m_unpack.size(); k++) m_unpack[k] = Turn(static_cast<double>(k) / kSize);
}

RealFft7680::~RealFft7680() = default;

// Decimation in time by 3: Z[k1 + q kThird] = A0 + w^q A1 + w^2q A2, where Ar is the transform of the samples
// r, r + 3, r + 6... turned by exp(-2 pi i r k1 / kHalf), and w = exp(-2 pi i / 3). The inverse conjugates the turns.
void RealFft7680::Complex(bool inverse) {
    for (size_t m = 0; m < kThird; m++) {
        for (size_t r = 0; r < 3; r++) m_parts[r * kThird + m] = m_z[3 * m + r];
    }
    for (size_t r = 0; r < 3; r++) {
        if (inverse) m_third->fft.ifft(m_parts.data() + r * kThird, m_partsOut.data() + r * kThird);
        else m_third->fft.fft(m_parts.data() + r * kThird, m_partsOut.data() + r * kThird);
    }
    const float sin3 = inverse ? 0.8660254037844386f : -0.8660254037844386f;  // the sine of w
    for (size_t k = 0; k < kThird; k++) {
        Cf t1 = m_twiddles[k];
        Cf t2 = m_twiddles[2 * k];
        if (inverse) {
            t1 = std::conj(t1);
            t2 = std::conj(t2);
        }
        const Cf a0 = m_partsOut[k];
        const Cf a1 = m_partsOut[kThird + k] * t1;
        const Cf a2 = m_partsOut[2 * kThird + k] * t2;
        const Cf sum = a1 + a2;
        const Cf diff = a1 - a2;
        const Cf mid = a0 - 0.5f * sum;
        const Cf rotated{-sin3 * diff.imag(), sin3 * diff.real()};  // i sin(w) (a1 - a2)
        m_z[k] = a0 + sum;
        m_z[k + kThird] = mid + rotated;
        m_z[k + 2 * kThird] = mid - rotated;
    }
}

void RealFft7680::Forward(const float* time, Cf* freq) {
    for (size_t n = 0; n < kHalf; n++) m_z[n] = {time[2 * n], time[2 * n + 1]};
    Complex(false);
    // Even samples' spectrum E and odd samples' O from Z and its mirror; X[k] = E[k] + exp(-2 pi i k / kSize) O[k].
    freq[0] = {m_z[0].real() + m_z[0].imag(), m_z[0].real() - m_z[0].imag()};
    for (size_t k = 1; k <= kHalf / 2; k++) {
        const Cf z = m_z[k];
        const Cf mirror = std::conj(m_z[kHalf - k]);
        const Cf even = 0.5f * (z + mirror);
        const Cf odd = Cf(0.0f, -0.5f) * (z - mirror);
        const Cf turned = m_unpack[k] * odd;
        freq[k] = even + turned;
        if (k != kHalf - k) freq[kHalf - k] = std::conj(even - turned);
    }
}

void RealFft7680::Inverse(const Cf* freq, float* time) {
    // Z[k] = E[k] + i O[k], from X[k] and X[kHalf - k]; doubled, so that the result is kSize times the input.
    const float dc = freq[0].real();
    const float nyquist = freq[0].imag();
    m_z[0] = {dc + nyquist, dc - nyquist};
    for (size_t k = 1; k <= kHalf / 2; k++) {
        const Cf x = freq[k];
        const Cf mirror = std::conj(freq[kHalf - k]);
        const Cf even = x + mirror;
        const Cf odd = (x - mirror) * std::conj(m_unpack[k]);
        m_z[k] = even + Cf(0.0f, 1.0f) * odd;
        if (k != kHalf - k) m_z[kHalf - k] = std::conj(even - Cf(0.0f, 1.0f) * odd);
    }
    Complex(true);
    for (size_t n = 0; n < kHalf; n++) {
        time[2 * n] = m_z[n].real();
        time[2 * n + 1] = m_z[n].imag();
    }
}

}  // namespace ampify::sing
