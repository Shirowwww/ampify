// The speed of Apple's player (Ampify.player.setRate). Apple Music plays and pauses its player through
// AVCFPlayerSetRate, at 1 and 0 only (knowledge/apple-music-playback.md). AutoMix's hook on that function
// (automix.cpp) is the only one an import entry can hold, so it asks SpeedFor what to pass on: a pause unchanged, a 1
// turned into the speed asked for, so the speed holds across pauses, seeks and songs. The speed is set at once on the
// player that plays. Other players (AutoMix's second one) keep their own rates.

#include <atomic>
#include <cmath>

#include <windows.h>

#include "audio_hooks.hpp"
#include "hooks_detail.hpp"

namespace ampify::win::audio_hooks {

namespace {

using SetRate_t = void(__cdecl*)(void* player, float rate);
using GetRate_t = float(__cdecl*)(void* player);

constexpr double kSlowest = 0.5;
constexpr double kFastest = 2.0;

std::atomic<float> g_speed{1.0f};

// Apple's own functions, called directly: the hooks are not on Ampify's calls. Looked up at each change of speed
// (rare), so that a module loaded late is found.
struct RateApi {
    SetRate_t setRate = nullptr;
    GetRate_t getRate = nullptr;
    void (*release)(const void*) = nullptr;
};

RateApi Api() {
    RateApi found;
    if (HMODULE av = GetModuleHandleW(L"AVFoundationCF.dll")) {
        found.setRate = reinterpret_cast<SetRate_t>(GetProcAddress(av, "AVCFPlayerSetRate"));
        found.getRate = reinterpret_cast<GetRate_t>(GetProcAddress(av, "AVCFPlayerGetRate"));
    }
    if (HMODULE cf = GetModuleHandleW(L"CoreFoundation.dll")) {
        found.release = reinterpret_cast<void (*)(const void*)>(GetProcAddress(cf, "CFRelease"));
    }
    return found;
}

}  // namespace

namespace detail {

float SpeedFor(float rate) { return rate == 1.0f ? g_speed.load() : rate; }

}  // namespace detail

bool SetPlaybackRate(double rate) {
    const RateApi api = Api();
    if (!std::isfinite(rate) || rate < kSlowest || rate > kFastest || !api.setRate || !api.getRate || !api.release) {
        return false;
    }
    float previous = g_speed.exchange(static_cast<float>(rate));
    if (previous == static_cast<float>(rate)) return true;
    // Changed at once only on a player that plays, held by the volume's follower (Apple sets its volume on the same
    // player) so that it cannot be freed meanwhile; a paused one takes the speed at its next play.
    void* followed = RetainFollowedPlayer();
    if (!followed) return true;
    if (api.getRate(followed) > 0.0f) api.setRate(followed, static_cast<float>(rate));
    api.release(followed);
    return true;
}

double PlaybackRate() { return g_speed.load(); }

}  // namespace ampify::win::audio_hooks
