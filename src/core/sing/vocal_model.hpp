#pragma once

// The vocal models Sing runs. A model reads and writes a spectrum, never audio: VocalSplitter turns a window of
// 44.1 kHz stereo into that spectrum and the model's answer, the vocals' spectrum, back into audio. Each model is
// described by a ModelSpec (its STFT, its window, how the stream runs it); the model itself is the platform's (ONNX
// Runtime on Windows), behind VocalModel, so everything else here stays portable and testable.

#include <cstddef>
#include <string_view>

namespace ampify::sing {

constexpr int kModelRate = 44100;

struct ModelSpec {
    std::string_view id;    // "standard" or "precise": the name the interface and the pack use
    size_t fftSize;         // n_fft, periodic Hann window, torch.stft's centre and reflect padding
    size_t fftHop;
    size_t bins;            // the first bins of the n_fft / 2 + 1 the model sees; the rest are zero
    size_t frames;          // STFT frames in one window
    size_t clearedLowBins;  // zeroed before the model
    double compensate;      // gain on the vocals subtracted for the instrumental
    double hopSeconds;      // the stream runs a window this often
    double aheadSeconds;    // and takes its vocals this long before the window's end, where the model hears more
    double delaySeconds;    // holding the sound back this long: two hops, the ahead, a run and some margin

    constexpr size_t Chunk() const { return fftHop * (frames - 1); }  // samples a window covers
    // Seconds one window may take: within a hop to keep up at all, and within what the delay leaves after the two
    // hops and the ahead, or each hop's vocals come after its sound starts (StudioStream then plays that moment
    // with the voice, once every hop). The standard model has 0.4 s, the precise one 0.3 s.
    constexpr double RunBudget() const {
        const double left = delaySeconds - 2 * hopSeconds - aheadSeconds;
        return left < hopSeconds ? left : hopSeconds;
    }
    constexpr size_t Plane() const { return bins * frames; }
    // [channel][bin][frame], channels L real, L imag, R real, R imag.
    constexpr size_t SpectrumSize() const { return 4 * Plane(); }
};

// UVR's MDX-Net Voc_FT: input "input", output "output", both float [1,4,3072,256], 5.92 s a window. UVR clears the
// three lowest bins and lifts the vocals by 2.1 % before subtracting them.
inline constexpr ModelSpec kStandard{"standard", 7680, 1024, 3072, 256, 3, 1.021, 0.4, 0.0, 1.3};
// Kim's Mel-Band RoFormer, exported to give the vocals' spectrum itself (its complex mask applied, bin 0 zeroed):
// input "input", output "output", both float [1,4,1025,frames]. A 3 s window every second, its vocals taken 0.5 s
// before its end, scores within 0.01 dB of the best streamed setting measured (MUSDB18) for half the GPU time of a
// window every 0.5 s; shorter windows lose more than 0.1 dB.
inline constexpr ModelSpec kPrecise{"precise", 2048, 441, 1025, 301, 0, 1.0, 1.0, 0.5, 2.8};
inline constexpr const ModelSpec* kModels[] = {&kStandard, &kPrecise};

// The standard model's numbers, which the tests and most of the code were written with.
constexpr size_t kFftSize = kStandard.fftSize;
constexpr size_t kFftHop = kStandard.fftHop;
constexpr size_t kBins = kStandard.bins;
constexpr size_t kFrames = kStandard.frames;
constexpr size_t kChunk = kStandard.Chunk();  // 261120, 5.92 s
constexpr size_t kChannelPlane = kStandard.Plane();
constexpr size_t kSpectrumSize = kStandard.SpectrumSize();
constexpr size_t kClearedLowBins = kStandard.clearedLowBins;
constexpr double kCompensate = kStandard.compensate;

struct VocalModel {
    explicit VocalModel(const ModelSpec& modelSpec = kStandard) : spec(modelSpec) {}
    virtual ~VocalModel();
    VocalModel(const VocalModel&) = delete;
    VocalModel& operator=(const VocalModel&) = delete;

    const ModelSpec& spec;
    // `spectrum` and `vocals` hold spec.SpectrumSize() floats each. False when the model could not run.
    virtual bool Run(const float* spectrum, float* vocals) = 0;
};

}  // namespace ampify::sing
