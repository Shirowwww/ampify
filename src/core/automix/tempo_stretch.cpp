#include "tempo_stretch.hpp"

#include "signalsmith-stretch.h"

namespace ampify::automix {

namespace {

// Interleaved stereo seen as buffer[channel][index], which is all the stretcher asks of a buffer.
template <typename Sample>
struct Interleaved {
    Sample* samples;
    struct Channel {
        Sample* samples;
        int channel;
        Sample& operator[](int index) const { return samples[index * 2 + channel]; }
    };
    Channel operator[](int channel) const { return {samples, channel}; }
};

}  // namespace

struct TempoStretch::Engine {
    signalsmith::stretch::SignalsmithStretch<float> stretch;
};

TempoStretch::TempoStretch(int sampleRate) : m_engine(std::make_unique<Engine>()) {
    // The work is spread over the calls (one more interval of latency): no spike on the audio thread.
    m_engine->stretch.presetDefault(2, static_cast<float>(sampleRate), true);
}

TempoStretch::~TempoStretch() = default;

int TempoStretch::Latency() const { return m_engine->stretch.inputLatency() + m_engine->stretch.outputLatency(); }

void TempoStretch::Reset() { m_engine->stretch.reset(); }

void TempoStretch::Process(const float* in, size_t inFrames, float* out, size_t outFrames) {
    m_engine->stretch.process(Interleaved<const float>{in}, static_cast<int>(inFrames), Interleaved<float>{out},
                              static_cast<int>(outFrames));
}

}  // namespace ampify::automix
