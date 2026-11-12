// Diagnostic, off unless the file record.on exists in the kit: keeps what the stereo stream sends to Windows (after
// Ampify's own steps), up to two minutes, and writes it to logs\output-<time>.wav once the file goes or the time is
// full. The audio thread only copies into memory reserved beforehand; the file is written by the poll thread.

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "../win_common.hpp"
#include "hooks_detail.hpp"

namespace ampify::win::audio_hooks::detail {

namespace {

constexpr int kSeconds = 120;
constexpr int kChannels = 2;

struct Recording {
    std::unique_ptr<float[]> samples;
    size_t capacity = 0;  // frames
    int rate = 0;
    std::atomic<size_t> frames{0};
};

std::atomic<Recording*> g_live{nullptr};  // set by the poll thread, read by the audio thread
std::unique_ptr<Recording> g_owned;       // poll thread only
std::atomic<int> g_rate{48000};

void WriteWav(const std::filesystem::path& file, const Recording& recording) {
    const std::uint32_t frames = static_cast<std::uint32_t>(recording.frames.load());
    const std::uint32_t bytes = frames * kChannels * 4;
    std::ofstream out(file, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(3);  // IEEE float
    u16(kChannels);
    u32(static_cast<std::uint32_t>(recording.rate));
    u32(static_cast<std::uint32_t>(recording.rate) * kChannels * 4);
    u16(kChannels * 4);
    u16(32);
    out.write("data", 4);
    u32(bytes);
    out.write(reinterpret_cast<const char*>(recording.samples.get()), bytes);
}

void Finish() {
    Recording* recording = g_owned.get();
    g_live.store(nullptr);
    Sleep(100);  // the audio thread's current block lands
    char name[64];
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::strftime(name, sizeof(name), "output-%H%M%S.wav", &local);
    std::filesystem::path file = KitDirectory() / L"logs" / name;
    WriteWav(file, *recording);
    Note("record: " + std::to_string(recording->frames.load()) + " frames at " + std::to_string(recording->rate) +
         " Hz written to logs\\" + name);
    g_owned.reset();
}

}  // namespace

void RecordOutput(const float* samples, unsigned frames, int rate) {
    g_rate.store(rate, std::memory_order_relaxed);
    Recording* recording = g_live.load(std::memory_order_acquire);
    if (!recording || recording->rate != rate) return;
    size_t at = recording->frames.load(std::memory_order_relaxed);
    size_t room = recording->capacity - at;
    size_t take = std::min<size_t>(room, frames);
    if (take == 0) return;
    std::memcpy(recording->samples.get() + at * kChannels, samples, take * kChannels * sizeof(float));
    recording->frames.store(at + take, std::memory_order_release);
}

void PollRecording() {
    static const std::wstring flag = (KitDirectory() / L"record.on").wstring();
    const bool wanted = GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (wanted && !g_owned) {
        auto recording = std::make_unique<Recording>();
        recording->rate = g_rate.load();
        recording->capacity = static_cast<size_t>(recording->rate) * kSeconds;
        recording->samples = std::make_unique<float[]>(recording->capacity * kChannels);
        g_owned = std::move(recording);
        g_live.store(g_owned.get(), std::memory_order_release);
        Note("record: started at " + std::to_string(g_owned->rate) + " Hz");
        return;
    }
    if (g_owned && (!wanted || g_owned->frames.load() >= g_owned->capacity)) {
        if (wanted) DeleteFileW(flag.c_str());
        Finish();
    }
}

}  // namespace ampify::win::audio_hooks::detail
