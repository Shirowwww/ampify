#pragma once

// The extension point of the sample path: a step on the decoded samples after the gain and before the knee, such as
// Ampify.audio's equalizer (sample_path.hpp). Portable: the Windows hooks (src/win/audio/) run it on what Apple Music
// renders, the tests on synthetic buffers.

#include <cstddef>

namespace ampify::audio {

// The rate every meter, ramp and filter assumes until its stream says otherwise.
constexpr double kStartSampleRate = 48000;

// Below this player volume the slider is too low to divide by: such a block is not measured.
constexpr float kLowestPlayerVolume = 0.001f;

// `key` names the stream or the spatial object the samples belong to, so the stage keeps one filter state for each.
class SampleStage {
public:
    virtual ~SampleStage() = default;

    // False while the stage would leave the samples unchanged. It is then not run at all.
    virtual bool Active() = 0;
    virtual void Run(const void* key, float* interleaved, size_t frames, int channels) = 0;
};

}  // namespace ampify::audio
