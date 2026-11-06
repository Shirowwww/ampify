#pragma once

// The host capability behind Ampify.audio's analysis, equalizer, loudness and volume (src/core/natives/audio_analysis.cpp).
// Read with host.Capability<AudioHost>(): null on a host without audio, where those functions answer "nothing known"
// instead of throwing. In Apple Music the engine process (AMPLibraryAgent.exe) has it in full; the interface process
// has what the engine shares, read only (AudioHost::Reach).
//
// Rules that every implementation keeps: the samples are only the decoded ones the kit's own hooks already see,
// they stay in memory, and only small frames of numbers reach the elements. The player volume is read, never set:
// the equalizer acts on the samples, so Apple Music's slider never moves.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis.hpp"

namespace ampify {

// A loudness reading older than this counts as unknown: the music is paused or stopped.
constexpr int64_t kLoudnessFreshMs = 1500;

// NaN stands for "not known" where a reading crosses an atomic or the shared block.
inline std::optional<double> KnownValue(double value) {
    if (std::isnan(value)) return std::nullopt;
    return value;
}

// What the process can do to the sound.
enum class AudioReach {
    Full,          // the samples pass here: analysis, equalizer, loudness, volume
    AnalysisOnly,  // the analysis, loudness and volume another process shares; no equalizer here
};

// Loudness of what plays, in LUFS (ITU-R BS.1770): momentary over 400 ms, short-term over 3 s. Empty when unknown.
struct LoudnessReading {
    std::optional<double> momentary;
    std::optional<double> shortTerm;
};

struct AudioAnalysisResult {
    audio::TempoEstimate tempo;
    LoudnessReading loudness;
    std::optional<audio::AnalysisFrame> frame;  // the newest analysis frame, if any
    // Centre of each band of the frames, in Hz, at the engine's live rate; before any audio, the starting layout.
    std::vector<double> bandFrequencies;
};

struct EqBandState {
    double frequency = 0;  // Hz
    double q = 0;
    std::string kind;  // "peaking", "lowshelf" or "highshelf"
    double gainDb = 0;
};

struct AudioHost {
    static constexpr std::string_view kCapabilityName = "audio";

    using FrameSink = std::function<void(const audio::AnalysisFrame& frame)>;

    virtual ~AudioHost() = default;

    virtual AudioReach Reach() const { return AudioReach::Full; }

    // Analysis frames for one element, about 20 a second at most, from a thread of the host. A second call
    // replaces the sink. After Release returns, the sink is never called again.
    virtual void SubscribeFrames(const std::string& elementId, FrameSink sink) = 0;

    // Tempo, loudness, the newest frame and the band layout. The first call starts the analysis for this element, so
    // the tempo may be unknown (0) for a few seconds.
    virtual AudioAnalysisResult Analyze(const std::string& elementId) = 0;

    virtual LoudnessReading Loudness() = 0;

    // The volume Apple Music set on its player, 0..1, as its slider shows it. Read only. Empty when not seen yet.
    virtual std::optional<double> PlayerVolume() = 0;

    // The equalizer: one set of bands for the process, applied to the samples after the kit's gain.
    virtual std::vector<EqBandState> EqBands() = 0;
    // False for a band that does not exist. The gain is clamped to +/- 12 dB.
    virtual bool SetEqBand(const std::string& elementId, size_t band, double gainDb) = 0;

    // The element stops: drops its frames and its use of the analysis, and puts the equalizer back to flat when
    // this element made the last change to it.
    virtual void Release(const std::string& elementId) = 0;
};

}  // namespace ampify
