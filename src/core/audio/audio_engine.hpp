#pragma once

// The process's AudioHost: the analysis, the equalizer settings, the loudness and the player volume that the
// sample path publishes, and the delivery of analysis frames to the elements. Portable: the Windows kernel feeds it
// from the audio hooks (src/win/audio/, through sample_path.hpp), the tests and `ampify test` feed it synthetic
// signals.
//
// Threads: the audio threads call FeedAnalysis and the Publish* methods, and read Eq(); they never block (the
// analysis is skipped for a block when the JavaScript thread holds it). Everything else may come from any thread.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "analysis.hpp"
#include "audio_host.hpp"
#include "equalizer.hpp"

namespace ampify::audio {

class AudioEngine : public AudioHost {
public:
    AudioEngine();
    ~AudioEngine() override;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // ---- The sample path ----------------------------------------------------------------------------------------

    // The equalizer's target gains. Each stream keeps its own EqProcessor over them.
    EqSettings& Eq() { return m_eq; }

    // True while an element wants analysis frames or the tempo: FeedAnalysis does nothing otherwise.
    bool AnalysisWanted() const { return m_wanted.load(std::memory_order_relaxed); }

    // Samples as they leave for the output, interleaved. `scale` brings them to the slider at full (1 / player
    // volume for a stream that already has it). Never blocks: a block is skipped when the analysis is busy.
    void FeedAnalysis(const float* interleaved, size_t frames, int channels, double sampleRate, float scale = 1.0f);

    // The live loudness. A reading older than kLoudnessFreshMs counts as unknown.
    void PublishLoudness(const LoudnessReading& reading);
    void PublishPlayerVolume(double volume);

    // ---- Delivery -------------------------------------------------------------------------------------------------

    // Hands the newest frame to every sink, once per frame. Called by the delivery thread, or by a test.
    void DeliverPending();

    // Starts a thread that calls DeliverPending every `period` (50 ms: 20 frames a second at most). Once only.
    void StartDeliveryThread(std::chrono::milliseconds period = std::chrono::milliseconds(50));

    // ---- AudioHost -------------------------------------------------------------------------------------------------

    void SubscribeFrames(const std::string& elementId, FrameSink sink) override;
    AudioAnalysisResult Analyze(const std::string& elementId) override;
    LoudnessReading Loudness() override;
    std::optional<double> PlayerVolume() override;
    std::vector<EqBandState> EqBands() override;
    bool SetEqBand(const std::string& elementId, size_t band, double gainDb) override;
    void Release(const std::string& elementId) override;

private:
    void UpdateWanted();  // under m_usersLock

    EqSettings m_eq;

    std::mutex m_analyzerLock;
    AudioAnalyzer m_analyzer;
    uint64_t m_delivered = 0;  // frame count last handed to the sinks, under m_analyzerLock

    std::mutex m_usersLock;
    std::set<std::string> m_analysisUsers;
    std::string m_eqOwner;  // the element that changed the equalizer last
    std::atomic<bool> m_wanted{false};

    std::mutex m_sinkLock;  // held while the sinks run, so Release waits for a delivery in progress
    std::map<std::string, FrameSink> m_sinks;

    std::atomic<double> m_momentary;
    std::atomic<double> m_shortTerm;
    std::atomic<double> m_volume;
    std::atomic<int64_t> m_loudnessAtMs{0};  // steady clock

    std::mutex m_threadLock;
    std::condition_variable m_wake;
    bool m_stopping = false;
    std::thread m_thread;
};

// The engine of this process. Never destroyed: the audio hooks may still run while the process exits.
AudioEngine& ProcessAudio();

}  // namespace ampify::audio
