#include "sample_path.hpp"

#include <algorithm>

namespace ampify::audio {

namespace {

// Render clients come one or two at a time; a handful of filter states covers a device change.
constexpr size_t kStreamEqProcessors = 8;

// 1 / player volume, or 0 when the slider is too low to divide by: that block is not measured.
float Unscale(float volume) { return volume > kLowestPlayerVolume ? 1.0f / volume : 0.0f; }

}  // namespace

EqStage::EqStage(const EqSettings& settings, size_t maxProcessors)
    : m_settings(settings), m_max(std::max<size_t>(1, maxProcessors)) {}

bool EqStage::Active() {
    if (!m_settings.IsFlat()) {
        m_running = true;
        return true;
    }
    if (!m_running) return false;
    // Flat again: keep filtering until every processor has eased back to 0 dB and rung out, then let go.
    for (const auto& [key, processor] : m_processors) {
        if (!processor->Idle()) return true;
    }
    m_running = false;
    m_processors.clear();
    return false;
}

void EqStage::Run(const void* key, float* interleaved, size_t frames, int channels) {
    auto found = m_processors.find(key);
    if (found == m_processors.end()) {
        // Objects come and go. Past the limit the states are dropped rather than the table grown without end.
        if (m_processors.size() >= m_max) m_processors.clear();
        found = m_processors.emplace(key, std::make_unique<EqProcessor>(m_settings)).first;
    }
    found->second->Process(interleaved, frames, channels, m_rate);
}

SamplePath::SamplePath(AudioEngine& engine)
    : m_engine(engine),
      m_streamEq(engine.Eq(), kStreamEqProcessors),
      m_secondEq(engine.Eq(), kStreamEqProcessors),
      m_objectEq(engine.Eq(), ObjectPass::kMaxObjects),
      m_streamLoudness(kStartSampleRate, 2),
      m_objectLoudness(kStartSampleRate, 1) {}

void SamplePath::BeforeStream(const float* samples, size_t frames, int channels, int sampleRate, float playerVolume) {
    float scale = Unscale(playerVolume);
    if (channels != 2 || scale == 0.0f || sampleRate <= 0) return;
    m_streamLoudness.SetSampleRate(sampleRate);
    m_streamLoudness.Process(samples, frames, scale);
    m_engine.PublishLoudness({m_streamLoudness.MomentaryLufs(), m_streamLoudness.ShortTermLufs()});
}

SampleStage* SamplePath::StreamStage(int sampleRate) {
    m_streamEq.SetSampleRate(sampleRate);
    return &m_streamEq;
}

SampleStage* SamplePath::SecondStreamStage(int sampleRate) {
    m_secondEq.SetSampleRate(sampleRate);
    return &m_secondEq;
}

void SamplePath::AfterStream(const float* samples, size_t frames, int channels, int sampleRate, float playerVolume) {
    float scale = Unscale(playerVolume);
    if (scale == 0.0f) return;
    m_engine.FeedAnalysis(samples, frames, channels, sampleRate, scale);
}

SampleStage* SamplePath::ObjectStage(int sampleRate) {
    m_objectEq.SetSampleRate(sampleRate);
    return &m_objectEq;
}

void SamplePath::AfterObjects(std::span<const ObjectBuffer> objects, size_t frames, int sampleRate,
                              const PassLevels& levels) {
    if (sampleRate <= 0 || frames == 0) return;
    m_objectLoudness.SetSampleRate(sampleRate);
    m_objectLoudness.ProcessPower(levels.framePower.data(), std::min(frames, levels.framePower.size()));
    m_engine.PublishLoudness({m_objectLoudness.MomentaryLufs(), m_objectLoudness.ShortTermLufs()});

    if (!m_engine.AnalysisWanted()) return;
    m_mix.assign(frames, 0.0f);
    for (const ObjectBuffer& object : objects) {
        for (size_t frame = 0; frame < frames; frame++) m_mix[frame] += object.samples[frame];
    }
    m_engine.FeedAnalysis(m_mix.data(), frames, 1, sampleRate);
}

}  // namespace ampify::audio
