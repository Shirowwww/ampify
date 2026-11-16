#pragma once

// The vocal model Sing runs: UVR-MDX-NET-Voc_FT (ONNX, input "input", output "output", both float [1,4,3072,256]).
// It reads and writes a spectrum, never audio: VocalSplitter turns 5.92 s of 44.1 kHz stereo into that spectrum and
// the model's answer back into audio. The model itself is the platform's (ONNX Runtime on Windows), behind
// VocalModel, so everything else here stays portable and testable.

#include <cstddef>

namespace ampify::sing {

constexpr int kModelRate = 44100;
constexpr size_t kFftSize = 7680;   // n_fft
constexpr size_t kFftHop = 1024;
constexpr size_t kBins = 3072;      // of the 3841 an FFT of 7680 gives; the rest are zero
constexpr size_t kFrames = 256;
constexpr size_t kChunk = kFftHop * (kFrames - 1);  // samples a window covers: 261120, 5.92 s
constexpr size_t kChannelPlane = kBins * kFrames;
// [channel][bin][frame], channels L real, L imag, R real, R imag.
constexpr size_t kSpectrumSize = 4 * kChannelPlane;
constexpr size_t kClearedLowBins = 3;  // zeroed before the model, as UVR does
constexpr double kCompensate = 1.021;  // UVR's gain on the vocals it subtracts for the instrumental

struct VocalModel {
    virtual ~VocalModel();
    // `spectrum` and `vocals` hold kSpectrumSize floats each. False when the model could not run.
    virtual bool Run(const float* spectrum, float* vocals) = 0;
};

}  // namespace ampify::sing
