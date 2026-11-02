#pragma once

// What AMPLibraryAgent.exe, where the audio is rendered, shares with AppleMusic.exe, where the interface is: the
// newest analysis frame, the tempo, the loudness and the player volume, in a small block of shared memory. The
// interface process asks for the analysis by writing a deadline into the block; the engine runs it only while that
// deadline is ahead, so nothing is analysed when no theme or element of the interface wants it.
//
// Portable: the block is plain memory here. src/win/audio_share.* maps it between the two processes. Numbers only:
// never a sample.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "analysis.hpp"
#include "audio_host.hpp"

namespace ampify::audio {

struct SharedAudio {
    static constexpr uint32_t kMagic = 0x41414D50;  // "PMAA": the block is Ampify's
    static constexpr uint32_t kLayout = 2;        // raised when a field moves; a reader of another layout reads nothing
    static constexpr size_t kMaxBands = 32;

    std::atomic<uint32_t> magic;
    std::atomic<uint32_t> layout;
    std::atomic<uint32_t> sequence;  // odd while the engine writes: a reader retries. One writer at a time
    uint32_t bandCount;
    float bandsDb[kMaxBands];
    float rms;
    float peak;
    double bpm;         // 0 when not known
    double confidence;
    double volume;      // NaN when not seen yet
    double momentary;   // NaN when unknown
    double shortTerm;   // NaN when unknown
    uint64_t frames;    // frames written since the engine started
    int64_t frameAtMs;  // GetTickCount64 in both processes, so the times compare
    int64_t statusAtMs;
    std::atomic<int64_t> wantedUntilMs;       // the analysis, written by the interface
    std::atomic<int64_t> soundWantedUntilMs;  // the sample hooks only (the volume), written by the interface
};

// Takes an unused block (all zero) for this layout. False when another program's data is there: leave it alone.
bool ClaimShared(SharedAudio& block);
// True when the block holds this layout: the writers check it before each write.
bool SharedUsable(const SharedAudio& block);

// Engine side.
void WriteSharedFrame(SharedAudio& block, const AnalysisFrame& frame, const TempoEstimate& tempo, int64_t nowMs);
void WriteSharedStatus(SharedAudio& block, std::optional<double> volume, const LoudnessReading& loudness, int64_t nowMs);
bool SharedWanted(const SharedAudio& block, int64_t nowMs);
bool SoundWanted(const SharedAudio& block, int64_t nowMs);  // the hooks, with or without the analysis

// Interface side: keeps the analysis running for the next 2 s; DemandSound, only the hooks that see the volume.
void DemandShared(SharedAudio& block, int64_t nowMs);
void DemandSound(SharedAudio& block, int64_t nowMs);

struct SharedAudioReading {
    std::optional<AnalysisFrame> frame;  // the newest frame, while it is less than 300 ms old (the music plays)
    uint64_t frames = 0;                 // count of frames written: a new value is a new frame
    TempoEstimate tempo;
    std::optional<double> volume;
    LoudnessReading loudness;  // empty when older than 1.5 s
};

// False when the block is not readable now (another layout, or a write that lasted through every retry).
bool ReadShared(const SharedAudio& block, int64_t nowMs, SharedAudioReading& out);

// The heights of `count` bars from the bands of the analysis, 0 to 1, for a theme's `content: bars(n)`. The level
// follows the loudest band of the last seconds, so a quiet song moves the bars as much as a loud one. A bar rises at
// once and falls back over about a third of a second; with no fresh frame (paused, stopped), every bar falls to 0.
class BarMeter {
public:
    explicit BarMeter(size_t count);

    void Update(const std::vector<float>* bandsDb, double elapsedSeconds);
    const std::vector<float>& Levels() const { return m_levels; }

private:
    std::vector<float> m_levels;
    float m_reference = -30.0f;  // dB of the loudest band lately
};

}  // namespace ampify::audio
