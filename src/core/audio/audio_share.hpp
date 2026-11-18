#pragma once

// What AMPLibraryAgent.exe, where the audio is rendered, shares with AppleMusic.exe, where the interface is: the
// newest analysis frame, the tempo, the loudness and the player volume, in a small block of shared memory. The
// interface process asks for the analysis by writing a deadline into the block; the engine runs it only while that
// deadline is ahead, so nothing is analysed when no theme or element of the interface wants it.
//
// Portable: the block is plain memory here. src/win/audio/audio_sharing.* maps it between the two processes. Numbers
// only: never a sample.

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
    // Raised when a field moves. It also names the mapping, so builds of two layouts never map the same block, and a
    // reader that finds another layout in it anyway reads nothing.
    static constexpr uint32_t kLayout = 6;
    static constexpr size_t kMaxBands = 32;

    std::atomic<uint32_t> magic;
    std::atomic<uint32_t> layout;
    std::atomic<uint32_t> sequence;  // odd while the engine writes: a reader retries. One writer at a time
    uint32_t bandCount;
    float bandsDb[kMaxBands];
    float bandHz[kMaxBands];  // centre of each band at the engine's live rate
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

    // Sing: the interface asks, the engine answers. Plain atomics, no sequence of the block: each field stands alone.
    // The request is written before its sequence moves, so an engine that sees a new sequence reads the new request.
    std::atomic<uint32_t> singRequestSequence;
    std::atomic<uint32_t> singRequestMode;        // SingMode
    std::atomic<uint32_t> singRequestVoiceMilli;  // 0..1000: the share of the singer's voice left in the mix
    std::atomic<uint32_t> singMode;               // SingMode in effect
    std::atomic<uint32_t> singPhase;              // SingPhase
    std::atomic<uint32_t> singProgressMilli;      // 0..1000, while downloading or loading
    std::atomic<uint32_t> singDelayMs;            // how long the sound is held back
    std::atomic<uint32_t> singStudio;             // SingStudio
    std::atomic<uint32_t> singReason;             // SingReason
    std::atomic<uint32_t> singRequestModel;       // SingModel: the voice model studio mode runs
    std::atomic<uint32_t> singRequestMusicMilli;  // 0..1000: the share of the music left (the singer alone at 0)
};

enum class SingMode : uint32_t { Off = 0, Light = 1, Studio = 2 };
enum class SingPhase : uint32_t { Off = 0, Downloading = 1, Loading = 2, Buffering = 3, On = 4, Failed = 5 };
enum class SingModel : uint32_t { Standard = 0, Precise = 1 };
enum class SingStudio : uint32_t { Unknown = 0, Yes = 1, No = 2 };  // can this PC run the studio quality
enum class SingReason : uint32_t {
    None = 0, NoGraphicsCard = 1, DownloadFailed = 2, ModelFailed = 3, DolbyAtmos = 4, AutoMixMixing = 5
};

struct SingRequest {
    SingMode mode = SingMode::Off;
    double voice = 0;       // 0..1
    SingModel model = SingModel::Standard;
    double music = 1;       // 0..1: below 1, the music is turned down instead and the singer stays
    uint32_t sequence = 0;  // moves with each request, so the engine sees a repeat of the same one
};

struct SingStatus {
    SingMode mode = SingMode::Off;
    SingPhase phase = SingPhase::Off;
    double progress = 0;  // 0..1
    double delaySeconds = 0;
    SingStudio studio = SingStudio::Unknown;
    SingReason reason = SingReason::None;
};

// Takes an unused block (all zero) for this layout. False when another program's data is there: leave it alone.
bool ClaimShared(SharedAudio& block);
// True when the block holds this layout: the writers check it before each write.
bool SharedUsable(const SharedAudio& block);

// Engine side.
void WriteSharedFrame(SharedAudio& block, const AnalysisFrame& frame, const TempoEstimate& tempo,
                      const std::vector<double>& bandFrequencies, int64_t nowMs);
void WriteSharedStatus(SharedAudio& block, std::optional<double> volume, const LoudnessReading& loudness, int64_t nowMs);
bool SharedWanted(const SharedAudio& block, int64_t nowMs);
bool SoundWanted(const SharedAudio& block, int64_t nowMs);  // the hooks, with or without the analysis

// Interface side: keeps the analysis running for the next 2 s; DemandSound, only the hooks that see the volume.
void DemandShared(SharedAudio& block, int64_t nowMs);
void DemandSound(SharedAudio& block, int64_t nowMs);

// Sing. The interface writes the request; the engine reads it and writes the status. Values out of range are clamped on
// the way in, and read back as off or none when the block holds something else. False when the block is not usable.
bool WriteSingRequest(SharedAudio& block, SingMode mode, double voice, SingModel model = SingModel::Standard,
                      double music = 1.0);
bool ReadSingRequest(const SharedAudio& block, SingRequest& out);
bool WriteSingStatus(SharedAudio& block, const SingStatus& status);
bool ReadSingStatus(const SharedAudio& block, SingStatus& out);

struct SharedAudioReading {
    std::optional<AnalysisFrame> frame;  // the newest frame, while it is less than 300 ms old (the music plays)
    uint64_t frames = 0;                 // count of frames written: a new value is a new frame
    std::vector<double> bandFrequencies;  // of the last frame written, fresh or not; empty before the first
    TempoEstimate tempo;
    std::optional<double> volume;
    LoudnessReading loudness;  // empty when older than kLoudnessFreshMs
};

// False when the block is not readable now (another layout, or a write that lasted through every retry).
bool ReadShared(const SharedAudio& block, int64_t nowMs, SharedAudioReading& out);

}  // namespace ampify::audio
