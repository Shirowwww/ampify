#include "audio_share.hpp"

#include <windows.h>

#include <chrono>
#include <thread>

namespace ampify::win {

namespace {

constexpr wchar_t kMappingName[] = L"Local\\Ampify.Audio.1";
constexpr const char* kShareUser = "ampify:interface";  // the engine's analysis user for the interface process
constexpr auto kStatusEvery = std::chrono::milliseconds(100);
constexpr auto kPollEvery = std::chrono::milliseconds(50);

// The block's sequence allows one writer: the status loop and the frame delivery take turns.
std::mutex g_writeLock;

}  // namespace

audio::SharedAudio* SharedAudioBlock() {
    static audio::SharedAudio* block = [] () -> audio::SharedAudio* {
        // Never unmapped: the other process and this one's threads may use it until the process exits.
        HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                            static_cast<DWORD>(sizeof(audio::SharedAudio)), kMappingName);
        if (!mapping) return nullptr;
        void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(audio::SharedAudio));
        if (!view) {
            CloseHandle(mapping);
            return nullptr;
        }
        auto* shared = static_cast<audio::SharedAudio*>(view);  // a new mapping is all zero, as ClaimShared expects
        if (!audio::ClaimShared(*shared)) return nullptr;
        return shared;
    }();
    return block;
}

int64_t SharedAudioNowMs() { return static_cast<int64_t>(GetTickCount64()); }

void StartAudioSharing(audio::AudioEngine& engine, Journal& journal, std::function<void()> startSound) {
    audio::SharedAudio* block = SharedAudioBlock();
    if (!block) {
        journal.Write("Audio sharing: Windows refused the shared memory, the interface will not see the sound");
        return;
    }
    std::thread([&engine, &journal, block, startSound = std::move(startSound)] {
        bool subscribed = false;
        bool soundStarted = false;
        while (true) {
            int64_t now = SharedAudioNowMs();
            if (audio::SharedUsable(*block)) {
                {
                    std::lock_guard<std::mutex> writing(g_writeLock);
                    audio::WriteSharedStatus(*block, engine.PlayerVolume(), engine.Loudness(), now);
                }
                bool wanted = audio::SharedWanted(*block, now);
                if (audio::SoundWanted(*block, now) && !soundStarted && startSound) {
                    soundStarted = true;
                    std::thread(startSound).detach();  // may wait for Apple's audio modules: not on this loop
                }
                if (wanted && !subscribed) {
                    engine.StartDeliveryThread();
                    engine.SubscribeFrames(kShareUser, [&engine, block](const audio::AnalysisFrame& frame) {
                        audio::TempoEstimate tempo = engine.Analyze(kShareUser).tempo;
                        std::lock_guard<std::mutex> writing(g_writeLock);
                        audio::WriteSharedFrame(*block, frame, tempo, SharedAudioNowMs());
                    });
                    journal.Write("Audio sharing: the interface asks for the analysis");
                } else if (!wanted && subscribed) {
                    engine.Release(kShareUser);
                }
                subscribed = wanted;
            }
            std::this_thread::sleep_for(kStatusEvery);
        }
    }).detach();
}

void SharedAudioHost::SubscribeFrames(const std::string& elementId, FrameSink sink) {
    std::lock_guard<std::mutex> guard(m_lock);
    m_sinks[elementId] = std::move(sink);
    if (m_polling) return;
    m_polling = true;
    std::thread([this] { Poll(); }).detach();
}

void SharedAudioHost::Poll() {
    while (true) {
        std::this_thread::sleep_for(kPollEvery);
        std::lock_guard<std::mutex> guard(m_lock);  // held while the sinks run: Release waits for a delivery
        if (m_sinks.empty()) {
            m_polling = false;
            return;
        }
        audio::SharedAudio* block = SharedAudioBlock();
        if (!block) continue;
        int64_t now = SharedAudioNowMs();
        audio::DemandShared(*block, now);
        audio::SharedAudioReading reading;
        if (!audio::ReadShared(*block, now, reading) || !reading.frame || reading.frames == m_delivered) continue;
        m_delivered = reading.frames;
        for (auto& [id, sink] : m_sinks) {
            if (sink) sink(*reading.frame);
        }
    }
}

AudioAnalysisResult SharedAudioHost::Analyze(const std::string&) {
    AudioAnalysisResult result;
    audio::SharedAudio* block = SharedAudioBlock();
    if (!block) return result;
    int64_t now = SharedAudioNowMs();
    audio::DemandShared(*block, now);  // the first call starts the analysis, as in the engine
    audio::SharedAudioReading reading;
    if (!audio::ReadShared(*block, now, reading)) return result;
    result.tempo = reading.tempo;
    result.loudness = reading.loudness;
    result.frame = reading.frame;
    return result;
}

std::vector<double> SharedAudioHost::BandFrequencies() { return audio::AudioAnalyzer().BandCentres(); }

LoudnessReading SharedAudioHost::Loudness() {
    audio::SharedAudio* block = SharedAudioBlock();
    audio::SharedAudioReading reading;
    if (!block || !audio::ReadShared(*block, SharedAudioNowMs(), reading)) return {};
    return reading.loudness;
}

std::optional<double> SharedAudioHost::PlayerVolume() {
    audio::SharedAudio* block = SharedAudioBlock();
    if (!block) return std::nullopt;
    int64_t now = SharedAudioNowMs();
    audio::DemandSound(*block, now);  // Apple's volume is only seen through the sample hooks, which this starts
    audio::SharedAudioReading reading;
    if (!audio::ReadShared(*block, now, reading)) return std::nullopt;
    return reading.volume;
}

void SharedAudioHost::Release(const std::string& elementId) {
    std::lock_guard<std::mutex> guard(m_lock);
    m_sinks.erase(elementId);
}

SharedAudioHost& InterfaceAudio() {
    static SharedAudioHost* host = new SharedAudioHost();  // leaked on purpose: see the header
    return *host;
}

}  // namespace ampify::win
