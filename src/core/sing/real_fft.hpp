#pragma once

// The real FFT of Sing's model size, 7680 points. Signalsmith's own takes 7680 as 256 x 15 and runs the factor 15 as a
// generic DFT, which made it four times slower than a power of two (129 us against 34 us for 8192): here the complex
// half-size transform, 3840 points, is three of 1280 (Signalsmith's fast radix-5 path) joined by a radix-3 step.
// Same layout and scale as signalsmith::linear::RealFFT; tests/sing_tests.cpp checks it against a direct DFT.

#include <complex>
#include <cstddef>
#include <memory>
#include <vector>

namespace ampify::sing {

// A real FFT of one size, Signalsmith's layout and scale: Forward gives size / 2 complex bins with {DC, Nyquist} in
// bin 0; Inverse of Forward gives size times the input.
class RealFft {
public:
    virtual ~RealFft() = default;
    virtual void Forward(const float* time, std::complex<float>* freq) = 0;
    virtual void Inverse(const std::complex<float>* freq, float* time) = 0;
    // RealFft7680 for 7680, Signalsmith's own for any other even size.
    static std::unique_ptr<RealFft> Make(size_t size);
};

class RealFft7680 final : public RealFft {
public:
    static constexpr size_t kSize = 7680;
    static constexpr size_t kHalf = kSize / 2;  // complex bins out; bin 0 holds {DC, Nyquist}, as Signalsmith's

    RealFft7680();
    ~RealFft7680();
    RealFft7680(const RealFft7680&) = delete;
    RealFft7680& operator=(const RealFft7680&) = delete;

    void Forward(const float* time, std::complex<float>* freq) override;
    void Inverse(const std::complex<float>* freq, float* time) override;

private:
    static constexpr size_t kThird = kHalf / 3;

    void Complex(bool inverse);  // m_z, kHalf points, in place

    struct Third;  // Signalsmith's FFT of kThird points, kept out of this header
    std::unique_ptr<Third> m_third;
    std::vector<std::complex<float>> m_z;
    std::vector<std::complex<float>> m_parts;      // the three interleaved thirds, then their transforms
    std::vector<std::complex<float>> m_partsOut;
    std::vector<std::complex<float>> m_twiddles;   // exp(-2 pi i k / kHalf), k < kHalf
    std::vector<std::complex<float>> m_unpack;     // exp(-2 pi i k / kSize), k <= kHalf / 2
};

}  // namespace ampify::sing
