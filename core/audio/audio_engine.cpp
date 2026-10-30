#include "audio_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace amk::audio {

namespace {

constexpr double kUnknown = std::numeric_limits<double>::quiet_NaN();
constexpr int64_t kLoudnessStaleMs = 1500;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::optional<double> Known(double value) {
    if (std::isnan(value)) return std::nullopt;
    return value;
}

}  // namespace

AudioEngine::AudioEngine() : m_momentary(kUnknown), m_shortTerm(kUnknown), m_volume(kUnknown) {}

AudioEngine::~AudioEngine() {
    {
        std::lock_guard<std::mutex> guard(m_threadLock);
        m_stopping = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void AudioEngine::FeedAnalysis(const float* interleaved, size_t frames, int channels, double sampleRate, float scale) {
    if (!AnalysisWanted()) return;
    std::unique_lock<std::mutex> lock(m_analyzerLock, std::try_to_lock);
    if (!lock.owns_lock()) return;
    m_analyzer.Feed(interleaved, frames, channels, sampleRate, scale);
}

void AudioEngine::PublishLoudness(const LoudnessReading& reading) {
    m_momentary.store(reading.momentary.value_or(kUnknown), std::memory_order_relaxed);
    m_shortTerm.store(reading.shortTerm.value_or(kUnknown), std::memory_order_relaxed);
    m_loudnessAtMs.store(NowMs(), std::memory_order_relaxed);
}

void AudioEngine::PublishPlayerVolume(double volume) {
    m_volume.store(std::isfinite(volume) ? std::clamp(volume, 0.0, 1.0) : kUnknown, std::memory_order_relaxed);
}

void AudioEngine::DeliverPending() {
    std::optional<AnalysisFrame> frame;
    {
        std::lock_guard<std::mutex> guard(m_analyzerLock);
        if (m_analyzer.FrameCount() != m_delivered) {
            m_delivered = m_analyzer.FrameCount();
            frame = m_analyzer.Latest();
        }
    }
    if (!frame) return;
    std::lock_guard<std::mutex> guard(m_sinkLock);
    for (auto& [id, sink] : m_sinks) {
        if (sink) sink(*frame);
    }
}

void AudioEngine::StartDeliveryThread(std::chrono::milliseconds period) {
    std::lock_guard<std::mutex> guard(m_threadLock);
    if (m_thread.joinable() || m_stopping) return;
    m_thread = std::thread([this, period] {
        std::unique_lock<std::mutex> lock(m_threadLock);
        while (!m_stopping) {
            m_wake.wait_for(lock, period);
            if (m_stopping) break;
            lock.unlock();
            DeliverPending();
            lock.lock();
        }
    });
}

void AudioEngine::UpdateWanted() {
    m_wanted.store(!m_analysisUsers.empty(), std::memory_order_relaxed);
}

void AudioEngine::SubscribeFrames(const std::string& elementId, FrameSink sink) {
    {
        std::lock_guard<std::mutex> guard(m_sinkLock);
        m_sinks[elementId] = std::move(sink);
    }
    std::lock_guard<std::mutex> guard(m_usersLock);
    m_analysisUsers.insert(elementId);
    UpdateWanted();
}

AudioAnalysisResult AudioEngine::Analyze(const std::string& elementId) {
    {
        std::lock_guard<std::mutex> guard(m_usersLock);
        m_analysisUsers.insert(elementId);
        UpdateWanted();
    }
    AudioAnalysisResult result;
    result.loudness = Loudness();
    std::lock_guard<std::mutex> guard(m_analyzerLock);
    result.tempo = m_analyzer.Tempo();
    if (m_analyzer.FrameCount() > 0) result.frame = m_analyzer.Latest();
    return result;
}

std::vector<double> AudioEngine::BandFrequencies() {
    std::lock_guard<std::mutex> guard(m_analyzerLock);
    if (m_analyzer.SampleRate() > 0) return m_analyzer.BandCentres();
    return AudioAnalyzer().BandCentres();  // before any audio: the layout the analysis starts with
}

LoudnessReading AudioEngine::Loudness() {
    LoudnessReading reading;
    if (NowMs() - m_loudnessAtMs.load(std::memory_order_relaxed) > kLoudnessStaleMs) return reading;
    reading.momentary = Known(m_momentary.load(std::memory_order_relaxed));
    reading.shortTerm = Known(m_shortTerm.load(std::memory_order_relaxed));
    return reading;
}

std::optional<double> AudioEngine::PlayerVolume() { return Known(m_volume.load(std::memory_order_relaxed)); }

std::vector<EqBandState> AudioEngine::EqBands() {
    std::vector<EqBandState> bands;
    const auto& specs = m_eq.Bands();
    for (size_t i = 0; i < specs.size(); i++) {
        bands.push_back({specs[i].frequency, specs[i].q, std::string(FilterKindName(specs[i].kind)), m_eq.GainDb(i)});
    }
    return bands;
}

bool AudioEngine::SetEqBand(const std::string& elementId, size_t band, double gainDb) {
    if (!m_eq.SetGainDb(band, gainDb)) return false;
    std::lock_guard<std::mutex> guard(m_usersLock);
    m_eqOwner = elementId;
    return true;
}

void AudioEngine::Release(const std::string& elementId) {
    {
        std::lock_guard<std::mutex> guard(m_sinkLock);
        m_sinks.erase(elementId);
    }
    std::lock_guard<std::mutex> guard(m_usersLock);
    m_analysisUsers.erase(elementId);
    UpdateWanted();
    if (!elementId.empty() && m_eqOwner == elementId) {
        m_eq.Flatten();
        m_eqOwner.clear();
    }
}

AudioEngine& ProcessAudio() {
    static AudioEngine* engine = new AudioEngine();  // leaked on purpose: see the header
    return *engine;
}

}  // namespace amk::audio
