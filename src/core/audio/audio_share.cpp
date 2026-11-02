#include "audio_share.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ampify::audio {

namespace {

constexpr int64_t kWantedForMs = 2000;
constexpr int64_t kFrameFreshMs = 300;
constexpr int64_t kStatusFreshMs = 1500;
constexpr int kReadAttempts = 8;

constexpr float kRange = 42.0f;        // dB from the reference down to an empty bar
constexpr float kReferenceFloor = -60.0f;
constexpr float kReferenceFall = 4.0f;  // dB per second: a song that gets quieter fills the bars again within seconds
constexpr float kBarFall = 3.0f;       // of the full height per second

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::optional<double> Known(double value) {
    if (std::isnan(value)) return std::nullopt;
    return value;
}

void BeginWrite(SharedAudio& block) {
    block.sequence.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
}

void EndWrite(SharedAudio& block) {
    std::atomic_thread_fence(std::memory_order_release);
    block.sequence.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

bool ClaimShared(SharedAudio& block) {
    uint32_t unused = 0;
    if (block.magic.compare_exchange_strong(unused, SharedAudio::kMagic)) {
        block.volume = kNaN;
        block.momentary = kNaN;
        block.shortTerm = kNaN;
        block.layout.store(SharedAudio::kLayout, std::memory_order_release);
        return true;
    }
    // Claimed by the other process: its layout may land a moment after its magic, and every read checks both.
    return unused == SharedAudio::kMagic;
}

bool SharedUsable(const SharedAudio& block) {
    return block.magic.load(std::memory_order_acquire) == SharedAudio::kMagic &&
           block.layout.load(std::memory_order_acquire) == SharedAudio::kLayout;
}

void WriteSharedFrame(SharedAudio& block, const AnalysisFrame& frame, const TempoEstimate& tempo, int64_t nowMs) {
    BeginWrite(block);
    block.bandCount = static_cast<uint32_t>(std::min(frame.bandsDb.size(), SharedAudio::kMaxBands));
    std::copy_n(frame.bandsDb.begin(), block.bandCount, block.bandsDb);
    block.rms = frame.rms;
    block.peak = frame.peak;
    block.bpm = tempo.bpm;
    block.confidence = tempo.confidence;
    block.frames++;
    block.frameAtMs = nowMs;
    EndWrite(block);
}

void WriteSharedStatus(SharedAudio& block, std::optional<double> volume, const LoudnessReading& loudness, int64_t nowMs) {
    BeginWrite(block);
    block.volume = volume.value_or(kNaN);
    block.momentary = loudness.momentary.value_or(kNaN);
    block.shortTerm = loudness.shortTerm.value_or(kNaN);
    block.statusAtMs = nowMs;
    EndWrite(block);
}

bool SharedWanted(const SharedAudio& block, int64_t nowMs) {
    return block.wantedUntilMs.load(std::memory_order_relaxed) > nowMs;
}

bool SoundWanted(const SharedAudio& block, int64_t nowMs) {
    return SharedWanted(block, nowMs) || block.soundWantedUntilMs.load(std::memory_order_relaxed) > nowMs;
}

void DemandShared(SharedAudio& block, int64_t nowMs) {
    block.wantedUntilMs.store(nowMs + kWantedForMs, std::memory_order_relaxed);
}

void DemandSound(SharedAudio& block, int64_t nowMs) {
    block.soundWantedUntilMs.store(nowMs + kWantedForMs, std::memory_order_relaxed);
}

bool ReadShared(const SharedAudio& block, int64_t nowMs, SharedAudioReading& out) {
    if (!SharedUsable(block)) return false;
    for (int attempt = 0; attempt < kReadAttempts; attempt++) {
        uint32_t before = block.sequence.load(std::memory_order_acquire);
        if (before % 2 != 0) continue;
        SharedAudio copy{};
        uint32_t bands = std::min<uint32_t>(block.bandCount, SharedAudio::kMaxBands);
        std::copy_n(block.bandsDb, bands, copy.bandsDb);
        copy.rms = block.rms;
        copy.peak = block.peak;
        copy.bpm = block.bpm;
        copy.confidence = block.confidence;
        copy.volume = block.volume;
        copy.momentary = block.momentary;
        copy.shortTerm = block.shortTerm;
        copy.frames = block.frames;
        copy.frameAtMs = block.frameAtMs;
        copy.statusAtMs = block.statusAtMs;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (block.sequence.load(std::memory_order_relaxed) != before) continue;

        out = SharedAudioReading{};
        out.frames = copy.frames;
        if (copy.frames > 0 && nowMs - copy.frameAtMs <= kFrameFreshMs) {
            AnalysisFrame frame;
            frame.bandsDb.assign(copy.bandsDb, copy.bandsDb + bands);
            frame.rms = copy.rms;
            frame.peak = copy.peak;
            out.frame = std::move(frame);
            out.tempo = {copy.bpm, copy.confidence};
        }
        out.volume = Known(copy.volume);
        if (copy.statusAtMs > 0 && nowMs - copy.statusAtMs <= kStatusFreshMs) {
            out.loudness = {Known(copy.momentary), Known(copy.shortTerm)};
        }
        return true;
    }
    return false;
}

BarMeter::BarMeter(size_t count) : m_levels(std::clamp<size_t>(count, 1, SharedAudio::kMaxBands), 0.0f) {}

void BarMeter::Update(const std::vector<float>* bandsDb, double elapsedSeconds) {
    const float dt = static_cast<float>(std::clamp(elapsedSeconds, 0.0, 0.5));
    const size_t bars = m_levels.size();
    if (!bandsDb || bandsDb->empty()) {
        for (float& level : m_levels) level = std::max(0.0f, level - kBarFall * dt);
        return;
    }
    const size_t bands = bandsDb->size();
    const float loudest = *std::max_element(bandsDb->begin(), bandsDb->end());
    m_reference = std::max({loudest, m_reference - kReferenceFall * dt, kReferenceFloor});
    for (size_t i = 0; i < bars; i++) {
        // Each bar takes the loudest of its bands; with more bars than bands, neighbours share one.
        size_t first = i * bands / bars;
        size_t last = std::max(first + 1, (i + 1) * bands / bars);
        float db = *std::max_element(bandsDb->begin() + static_cast<std::ptrdiff_t>(first),
                                     bandsDb->begin() + static_cast<std::ptrdiff_t>(std::min(last, bands)));
        float target = std::clamp((db - (m_reference - kRange)) / kRange, 0.0f, 1.0f);
        target = target * target;  // the quiet end stays low, so the beats stand out
        m_levels[i] = std::max(target, m_levels[i] - kBarFall * dt);
    }
}

}  // namespace ampify::audio
