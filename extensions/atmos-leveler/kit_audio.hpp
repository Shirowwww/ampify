#pragma once

// Ampify's own processing on the leveler's sample path, for Ampify.audio: the equalizer (after the leveler's gain,
// before the soft knee), the live loudness and the analysis frames. The Windows hooks (atmos_windows.cpp) call it;
// it has no Windows code, so the tests drive it with synthetic buffers.
//
// It works on Ampify's own hooks, the stereo IAudioRenderClient stream and the Dolby Atmos spatial objects, and
// never on Apple's audio tap, which Apple skips for Atmos (knowledge/atmos-loudness.md). The samples stay in the
// buffers Apple Music already hands to Windows: nothing is copied out but small frames of numbers.
//
// Threads: the stream methods run on the stream's audio thread, the object methods on the spatial audio thread.
// The two paths share nothing but the engine, which is thread-safe.

#include <cstddef>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "../../core/audio/audio_engine.hpp"
#include "../../core/audio/equalizer.hpp"
#include "gain_ramp.hpp"
#include "live_loudness.hpp"
#include "object_pass.hpp"

namespace atmos {

// The equalizer of one path: one EqProcessor per stream or per spatial object, over the engine's settings.
// Inactive (the samples untouched) while the settings are flat and every processor has rung out.
class EqStage : public SampleStage {
public:
    explicit EqStage(const ampify::audio::EqSettings& settings, size_t maxProcessors = ObjectPass::kMaxObjects);

    void SetSampleRate(double sampleRate) { m_rate = sampleRate; }

    bool Active() override;
    void Run(const void* key, float* interleaved, size_t frames, int channels) override;

    size_t Processors() const { return m_processors.size(); }

private:
    const ampify::audio::EqSettings& m_settings;
    size_t m_max;
    double m_rate = 48000;
    bool m_running = false;
    std::unordered_map<const void*, std::unique_ptr<ampify::audio::EqProcessor>> m_processors;
};

class KitAudio {
public:
    explicit KitAudio(ampify::audio::AudioEngine& engine);

    // ---- Stereo stream (IAudioRenderClient, after the player volume) ----

    // Before the gain: the live loudness, divided by the player volume as the leveler does.
    void BeforeStream(const float* samples, size_t frames, int channels, int sampleRate, float playerVolume);
    // The equalizer stage for ApplyCurve, at the stream's rate.
    SampleStage* StreamStage(int sampleRate);
    // After the gain, the equalizer and the knee: the analysis sees what is heard, slider at full.
    void AfterStream(const float* samples, size_t frames, int channels, int sampleRate, float playerVolume);

    // ---- Dolby Atmos spatial objects (before the slider volume) ----

    // The equalizer stage for ObjectPass::Process, at the objects' rate.
    SampleStage* ObjectStage(int sampleRate);
    // After ObjectPass::Process: the live loudness from the pass's power (before the gain), and the analysis of the
    // objects summed to mono (after the gain and the equalizer).
    void AfterObjects(std::span<const ObjectBuffer> objects, size_t frames, int sampleRate, const PassLevels& levels);

private:
    ampify::audio::AudioEngine& m_engine;
    EqStage m_streamEq;
    EqStage m_objectEq;
    LiveLoudness m_streamLoudness;
    LiveLoudness m_objectLoudness;
    std::vector<float> m_mix;  // objects summed, spatial thread only
};

}  // namespace atmos
