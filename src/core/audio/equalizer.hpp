#pragma once

// The kit's equalizer: biquad filters from the RBJ Audio EQ Cookbook, a fixed set of bands whose gains the
// elements set (Ampify.audio.eq), applied to the decoded samples the kit's hooks already process. Pure C++.
//
// Two halves, for two threads: EqSettings holds the target gains (written by the JavaScript thread, read by the
// audio threads without a lock), and each audio stream has its own EqProcessor, which keeps the filter state and
// eases its gains towards the targets so a change never clicks. With every gain at 0 dB the processor does not
// touch the samples at all: a flat equalizer is a bit-exact passthrough.

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::audio {

enum class FilterKind { Peaking, LowShelf, HighShelf };

std::string_view FilterKindName(FilterKind kind);  // "peaking", "lowshelf", "highshelf"

// Normalised biquad (a0 = 1).
struct BiquadCoefficients {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    // RBJ cookbook designs. For the shelves, q is the cookbook's shelf slope S (1 = steepest without overshoot).
    static BiquadCoefficients Design(FilterKind kind, double frequency, double q, double gainDb, double sampleRate);

    // Magnitude of the response at `frequency`, in dB.
    double MagnitudeDb(double frequency, double sampleRate) const;
};

struct EqBandSpec {
    FilterKind kind = FilterKind::Peaking;
    double frequency = 1000;
    double q = 1.41;
};

// The ten bands of the usual graphic equalizer (31 Hz to 16 kHz, one octave apart), all peaking.
const std::vector<EqBandSpec>& DefaultEqBands();

// Gains of a named preset for DefaultEqBands(), or empty for an unknown name. Names: PresetNames().
std::optional<std::vector<double>> PresetGains(std::string_view name);
const std::vector<std::string>& PresetNames();

inline constexpr double kEqMaxGainDb = 12.0;  // a band is clamped to +/- this

// The target gains, shared by the processors of every stream. Thread-safe.
class EqSettings {
public:
    static constexpr size_t kMaxBands = 16;

    explicit EqSettings(std::vector<EqBandSpec> bands = DefaultEqBands());

    const std::vector<EqBandSpec>& Bands() const { return m_bands; }

    // Clamped to +/- kEqMaxGainDb. False for a band that does not exist or a value that is not a number.
    bool SetGainDb(size_t band, double gainDb);
    double GainDb(size_t band) const;
    void Flatten();
    bool IsFlat() const;

    // Bumped on every change, so a processor knows when to read the gains again.
    unsigned Version() const { return m_version.load(std::memory_order_acquire); }

private:
    std::vector<EqBandSpec> m_bands;
    std::array<std::atomic<float>, kMaxBands> m_gains{};
    std::atomic<unsigned> m_version{0};
};

// The equalizer of one stream: filter state per channel, gains eased towards the settings. Audio thread only.
class EqProcessor {
public:
    static constexpr int kMaxChannels = 8;
    static constexpr size_t kBlock = 32;           // frames between two coefficient updates while a gain moves
    static constexpr double kDbPerSecond = 120.0;  // easing speed: 12 dB in 0.1 s

    explicit EqProcessor(const EqSettings& settings);

    // Filters interleaved samples in place. Returns false, without touching a single sample, while every band
    // is at 0 dB and has nothing left ringing.
    bool Process(float* samples, size_t frames, int channels, double sampleRate);

    // Every band at 0 dB, its target too, and its filter state decayed: the next Process will not touch samples.
    bool Idle() const;

    // Current (eased) gain of a band.
    double CurrentDb(size_t band) const;

private:
    struct Band {
        double current = 0;  // dB, eased towards the target
        double target = 0;
        bool active = false;  // filtering: gain not 0, or state still ringing
        BiquadCoefficients coefficients;
        std::array<double, kMaxChannels> z1{};
        std::array<double, kMaxChannels> z2{};
    };

    void Sync();
    void Design(Band& band, const EqBandSpec& spec);
    void Retire(Band& band);

    const EqSettings& m_settings;
    std::vector<Band> m_bands;
    unsigned m_version = ~0u;
    double m_rate = 0;
};

}  // namespace ampify::audio
