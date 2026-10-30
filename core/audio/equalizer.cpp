#include "equalizer.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>

namespace amk::audio {

namespace {

constexpr double kRingingFloor = 1e-10;  // filter state below this is silence: the band can stop

struct Preset {
    const char* name;
    std::array<double, 10> gains;  // 31 Hz to 16 kHz
};

// Shapes in the spirit of the usual presets of music players. Values in dB.
const Preset kPresets[] = {
    {"flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"acoustic", {4.5, 4.5, 3.5, 1, 1.5, 1.5, 3, 3.5, 3, 1.5}},
    {"bass-booster", {5.5, 4.5, 3.5, 2.5, 1, 0, 0, 0, 0, 0}},
    {"bass-reducer", {-5.5, -4.5, -3.5, -2.5, -1, 0, 0, 0, 0, 0}},
    {"classical", {4.5, 3.5, 3, 2.5, -1.5, -1.5, 0, 2, 3, 3.5}},
    {"dance", {3.5, 6.5, 5, 0, 2, 3.5, 5, 4.5, 3.5, 0}},
    {"electronic", {4, 3.5, 1, 0, -2, 2, 1, 1, 4, 4.5}},
    {"hip-hop", {5, 4, 1.5, 3, -1, -1, 1.5, -0.5, 2, 3}},
    {"jazz", {4, 3, 1.5, 2, -1.5, -1.5, 0, 1.5, 3, 3.5}},
    {"loudness", {6, 4, 0, 0, -2, 0, -1, -5, 5, 1}},
    {"pop", {-1.5, -1, 0, 2, 4, 4, 2, 0, -1, -1.5}},
    {"rock", {5, 4, 3, 1.5, -0.5, -1, 0.5, 2.5, 3.5, 4.5}},
    {"treble-booster", {0, 0, 0, 0, 0, 1, 2.5, 3.5, 4.5, 5.5}},
    {"treble-reducer", {0, 0, 0, 0, 0, -1, -2.5, -3.5, -4.5, -5.5}},
    {"vocal-booster", {-1.5, -3, -3, 1.5, 3.5, 3.5, 3, 1.5, 0, -1.5}},
};

}  // namespace

std::string_view FilterKindName(FilterKind kind) {
    switch (kind) {
        case FilterKind::Peaking: return "peaking";
        case FilterKind::LowShelf: return "lowshelf";
        case FilterKind::HighShelf: return "highshelf";
    }
    return "peaking";
}

BiquadCoefficients BiquadCoefficients::Design(FilterKind kind, double frequency, double q, double gainDb,
                                              double sampleRate) {
    // The centre stays below Nyquist, so a 16 kHz band still designs at 32 kHz.
    frequency = std::clamp(frequency, 1.0, sampleRate * 0.45);
    q = std::max(q, 0.05);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * frequency / sampleRate;
    const double cosine = std::cos(w0);
    const double sine = std::sin(w0);

    double b0, b1, b2, a0, a1, a2;
    switch (kind) {
        case FilterKind::LowShelf:
        case FilterKind::HighShelf: {
            const double alpha = sine / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / q - 1.0) + 2.0);
            const double root = 2.0 * std::sqrt(a) * alpha;
            if (kind == FilterKind::LowShelf) {
                b0 = a * ((a + 1) - (a - 1) * cosine + root);
                b1 = 2 * a * ((a - 1) - (a + 1) * cosine);
                b2 = a * ((a + 1) - (a - 1) * cosine - root);
                a0 = (a + 1) + (a - 1) * cosine + root;
                a1 = -2 * ((a - 1) + (a + 1) * cosine);
                a2 = (a + 1) + (a - 1) * cosine - root;
            } else {
                b0 = a * ((a + 1) + (a - 1) * cosine + root);
                b1 = -2 * a * ((a - 1) + (a + 1) * cosine);
                b2 = a * ((a + 1) + (a - 1) * cosine - root);
                a0 = (a + 1) - (a - 1) * cosine + root;
                a1 = 2 * ((a - 1) - (a + 1) * cosine);
                a2 = (a + 1) - (a - 1) * cosine - root;
            }
            break;
        }
        case FilterKind::Peaking:
        default: {
            const double alpha = sine / (2.0 * q);
            b0 = 1 + alpha * a;
            b1 = -2 * cosine;
            b2 = 1 - alpha * a;
            a0 = 1 + alpha / a;
            a1 = -2 * cosine;
            a2 = 1 - alpha / a;
            break;
        }
    }
    BiquadCoefficients c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

double BiquadCoefficients::MagnitudeDb(double frequency, double sampleRate) const {
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    std::complex<double> response = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    return 20.0 * std::log10(std::max(std::abs(response), 1e-12));
}

const std::vector<EqBandSpec>& DefaultEqBands() {
    static const std::vector<EqBandSpec> bands = [] {
        std::vector<EqBandSpec> out;
        for (double frequency : {31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0}) {
            out.push_back({FilterKind::Peaking, frequency, 1.41});  // Q 1.41: one octave wide
        }
        return out;
    }();
    return bands;
}

std::optional<std::vector<double>> PresetGains(std::string_view name) {
    for (const Preset& preset : kPresets) {
        if (name == preset.name) return std::vector<double>(preset.gains.begin(), preset.gains.end());
    }
    return std::nullopt;
}

const std::vector<std::string>& PresetNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const Preset& preset : kPresets) out.emplace_back(preset.name);
        return out;
    }();
    return names;
}

// ---- Settings ----------------------------------------------------------------------------------------------------

EqSettings::EqSettings(std::vector<EqBandSpec> bands) : m_bands(std::move(bands)) {
    if (m_bands.size() > kMaxBands) m_bands.resize(kMaxBands);
    for (auto& gain : m_gains) gain.store(0.0f, std::memory_order_relaxed);
}

bool EqSettings::SetGainDb(size_t band, double gainDb) {
    if (band >= m_bands.size() || !std::isfinite(gainDb)) return false;
    float value = static_cast<float>(std::clamp(gainDb, -kEqMaxGainDb, kEqMaxGainDb));
    if (std::fabs(value) < 0.01f) value = 0.0f;  // a band set back near 0 is exactly flat
    m_gains[band].store(value, std::memory_order_relaxed);
    m_version.fetch_add(1, std::memory_order_release);
    return true;
}

double EqSettings::GainDb(size_t band) const {
    return band < m_bands.size() ? m_gains[band].load(std::memory_order_relaxed) : 0.0;
}

void EqSettings::Flatten() {
    for (auto& gain : m_gains) gain.store(0.0f, std::memory_order_relaxed);
    m_version.fetch_add(1, std::memory_order_release);
}

bool EqSettings::IsFlat() const {
    for (size_t band = 0; band < m_bands.size(); band++) {
        if (m_gains[band].load(std::memory_order_relaxed) != 0.0f) return false;
    }
    return true;
}

// ---- Processor ---------------------------------------------------------------------------------------------------

EqProcessor::EqProcessor(const EqSettings& settings) : m_settings(settings), m_bands(settings.Bands().size()) {}

double EqProcessor::CurrentDb(size_t band) const { return band < m_bands.size() ? m_bands[band].current : 0.0; }

bool EqProcessor::Idle() const {
    for (const Band& band : m_bands) {
        if (band.active || band.target != 0) return false;
    }
    return true;
}

void EqProcessor::Design(Band& band, const EqBandSpec& spec) {
    band.coefficients = BiquadCoefficients::Design(spec.kind, spec.frequency, spec.q, band.current, m_rate);
}

void EqProcessor::Retire(Band& band) {
    band.active = false;
    band.current = 0;
    band.z1.fill(0);
    band.z2.fill(0);
    band.coefficients = BiquadCoefficients{};
}

// Reads the targets after a change. A band that wakes up starts from a clean state at its current gain.
void EqProcessor::Sync() {
    unsigned version = m_settings.Version();
    if (version == m_version) return;
    m_version = version;
    const auto& specs = m_settings.Bands();
    for (size_t i = 0; i < m_bands.size(); i++) {
        Band& band = m_bands[i];
        band.target = m_settings.GainDb(i);
        if (!band.active && band.target != 0) {
            band.active = true;
            band.current = 0;
            band.z1.fill(0);
            band.z2.fill(0);
            Design(band, specs[i]);
        }
    }
}

bool EqProcessor::Process(float* samples, size_t frames, int channels, double sampleRate) {
    if (!samples || frames == 0 || channels <= 0 || channels > kMaxChannels || sampleRate <= 0) return false;
    const auto& specs = m_settings.Bands();
    Sync();
    if (sampleRate != m_rate) {
        m_rate = sampleRate;
        for (size_t i = 0; i < m_bands.size(); i++) {
            if (m_bands[i].active) Design(m_bands[i], specs[i]);
        }
    }
    bool any = false;
    for (const Band& band : m_bands) any = any || band.active;
    if (!any) return false;

    const size_t width = static_cast<size_t>(channels);
    for (size_t offset = 0; offset < frames; offset += kBlock) {
        const size_t count = std::min(kBlock, frames - offset);
        float* block = samples + offset * width;
        const double step = kDbPerSecond * static_cast<double>(count) / m_rate;

        for (size_t i = 0; i < m_bands.size(); i++) {
            Band& band = m_bands[i];
            if (!band.active) continue;
            if (band.current != band.target) {
                double delta = band.target - band.current;
                band.current = std::fabs(delta) <= step ? band.target : band.current + std::copysign(step, delta);
                Design(band, specs[i]);
            }
            const BiquadCoefficients& c = band.coefficients;
            for (size_t channel = 0; channel < width; channel++) {
                double z1 = band.z1[channel];
                double z2 = band.z2[channel];
                for (size_t frame = 0; frame < count; frame++) {
                    float& sample = block[frame * width + channel];
                    double x = sample;
                    double y = c.b0 * x + z1;  // transposed direct form II
                    z1 = c.b1 * x - c.a1 * y + z2;
                    z2 = c.b2 * x - c.a2 * y;
                    sample = static_cast<float>(y);
                }
                band.z1[channel] = z1;
                band.z2[channel] = z2;
            }
        }
    }

    // A band back at 0 dB is the identity: once its state has rung out, it stops, and so does the processor.
    for (Band& band : m_bands) {
        if (!band.active || band.current != 0 || band.target != 0) continue;
        double ringing = 0;
        for (size_t channel = 0; channel < width; channel++) {
            ringing = std::max({ringing, std::fabs(band.z1[channel]), std::fabs(band.z2[channel])});
        }
        if (ringing < kRingingFloor) Retire(band);
    }
    return true;
}

}  // namespace amk::audio
