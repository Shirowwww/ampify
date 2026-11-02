#pragma once

// The shared block of src/core/audio/audio_share.hpp between AMPLibraryAgent.exe and AppleMusic.exe: a named file
// mapping of this Windows session ("Local\"), made by whichever process comes first.
//
// In AMPLibraryAgent.exe, StartAudioSharing publishes the volume and the loudness, and the analysis frames while the
// interface asks for them. In AppleMusic.exe, InterfaceAudio() reads them: it is the AudioHost of that process, so the
// extensions of the interface and the themes' `content: bars(n)` see the sound too. The equalizer stays in the engine.

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "../core/audio/audio_engine.hpp"
#include "../core/audio/audio_share.hpp"
#include "../core/journal.hpp"

namespace ampify::win {

// The block, mapped once per process. Null when Windows refused the mapping.
audio::SharedAudio* SharedAudioBlock();
int64_t SharedAudioNowMs();  // GetTickCount64: the clock both processes share

// AMPLibraryAgent.exe: a thread that writes the status every 100 ms and the frames while they are wanted. Once only.
// startSound runs once, on a thread of its own, the first time the interface asks for the sound: it puts the sample
// hooks in place when nothing else did (the leveler off).
void StartAudioSharing(audio::AudioEngine& engine, Journal& journal, std::function<void()> startSound);

// AppleMusic.exe: Ampify.audio from the shared block. Frames reach the sinks from a thread of this host, at most 20 a
// second, and only while the music plays. The equalizer is the engine's: here it has no bands.
class SharedAudioHost final : public AudioHost {
public:
    void SubscribeFrames(const std::string& elementId, FrameSink sink) override;
    AudioAnalysisResult Analyze(const std::string& elementId) override;
    std::vector<double> BandFrequencies() override;
    LoudnessReading Loudness() override;
    std::optional<double> PlayerVolume() override;
    std::vector<EqBandState> EqBands() override { return {}; }
    bool SetEqBand(const std::string&, size_t, double) override { return false; }
    void Release(const std::string& elementId) override;

private:
    void Poll();  // the delivery thread: runs while a sink is subscribed

    std::mutex m_lock;
    std::map<std::string, FrameSink> m_sinks;
    bool m_polling = false;
    uint64_t m_delivered = 0;
};

// The host of this process. Never destroyed, like ProcessAudio(): its thread may run while the process exits.
SharedAudioHost& InterfaceAudio();

}  // namespace ampify::win
