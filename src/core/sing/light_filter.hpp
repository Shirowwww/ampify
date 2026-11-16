#pragma once

// Sing's light mode: karaoke with no latency and no model. What both channels share in the voice's band
// (150 Hz to 7 kHz) is taken away: each channel loses the other's band, so a sound in the centre cancels there while
// one panned hard to a side stays on its side. Outside the band nothing cancels: the bass and the cymbals stay.
//
// The band is each channel minus its 4th-order Butterworth low (150 Hz) and high (7 kHz) parts, so inside it the
// band is the channel itself, phase included, and a centred voice cancels deeply.

#include <array>
#include <cstddef>

namespace ampify::sing {

class LightFilter {
public:
    // In place, interleaved stereo. `voice` 0..1: how much of the centre's band stays.
    void Process(float* interleaved, size_t frames, int rate, double voice);
    void Reset();

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    struct State {
        double z1 = 0, z2 = 0;
    };
    static constexpr size_t kSections = 4;  // two low-pass, two high-pass

    void Design(int rate);
    double Band(size_t channel, double x);

    int m_rate = 0;
    std::array<Biquad, kSections> m_sections{};
    std::array<std::array<State, kSections>, 2> m_state{};
    double m_voice = -1;  // smoothed; below 0 until the first block
    double m_voiceStep = 0;
};

}  // namespace ampify::sing
