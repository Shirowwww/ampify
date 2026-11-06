// The volume of Apple Music's player: followed through AVCFPlayerSetVolume, read from Apple's preferences until the
// player has one, and set the same way Apple sets it. The hook passes Apple's own volume on unchanged, so the slider
// never moves because of Ampify.

#include <atomic>
#include <mutex>
#include <string>

#include "../../core/audio/audio_engine.hpp"
#include "../hook.hpp"
#include "audio_hooks.hpp"
#include "hooks_detail.hpp"

namespace ampify::win::audio_hooks {

namespace {

using detail::Note;

using SetVolume_t = void(__cdecl*)(void* player, float volume);
using CopyObject_t = void*(__cdecl*)(const void* object);
using CFRelease_t = void(__cdecl*)(const void* object);
using CFIndex_t = long long;
using CFTypeID_t = unsigned long long;
using CFGetTypeID_t = CFTypeID_t(__cdecl*)(const void* object);
using CFNumberGetTypeID_t = CFTypeID_t(__cdecl*)();
using CFNumberGetValue_t = unsigned char(__cdecl*)(const void* number, CFIndex_t type, void* value);
using CFStringCreateWithCString_t = const void*(__cdecl*)(const void* allocator, const char* text, unsigned encoding);
using CFPreferencesCopyAppValue_t = const void*(__cdecl*)(const void* key, const void* application);

constexpr unsigned kUtf8 = 0x08000100;
constexpr CFIndex_t kCFNumberDoubleType = 13;

CopyObject_t g_retain = nullptr;  // CFRetain
CFRelease_t g_release = nullptr;  // CFRelease
SetVolume_t g_setVolumeOriginal = nullptr;

std::atomic<float> g_playerVolume{1.0f};      // last volume Apple Music set on the player
std::atomic<bool> g_playerVolumeSeen{false};  // once true, the player's volume replaces the stored one

// The player Apple Music set a volume on last. Retained, and changed only under g_playerLock.
std::mutex g_playerLock;
void* g_player = nullptr;

// Apple Music sets no volume on a player until a song starts, but keeps the slider's gain in its preferences
// (volumeWSG, AMPLibraryAgent.exe.plist): Ampify.audio.volume() reads it until the player has one.
struct StoredVolume {
    CFGetTypeID_t GetTypeID = nullptr;
    CFNumberGetTypeID_t NumberTypeID = nullptr;
    CFNumberGetValue_t NumberGetValue = nullptr;
    CFPreferencesCopyAppValue_t CopyAppValue = nullptr;
    const void* key = nullptr;          // "volumeWSG", kept for the life of the process
    const void* application = nullptr;  // kCFPreferencesCurrentApplication
};
StoredVolume g_stored;  // poll thread only

bool Resolve(const char* name, void* out) {
    void* address = nullptr;
    if (!FindExport(L"CoreFoundation.dll", name, address)) {
        Note(std::string(name) + " not found: leveler inactive");
        return false;
    }
    std::memcpy(out, &address, sizeof(address));
    return true;
}

// Observes the volume Apple Music sets on its player. The volume itself is passed through unchanged, so
// the slider does not move.
void __cdecl SetVolume_Hook(void* player, float volume) {
    g_setVolumeOriginal(player, volume);

    g_playerVolume = volume;
    g_playerVolumeSeen = true;
    audio::ProcessAudio().PublishPlayerVolume(volume);  // Ampify.audio.volume(): read only, like the slider
    void* old = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_playerLock);
        if (player != g_player) {
            g_retain(player);
            old = g_player;
            g_player = player;
        }
    }
    if (old) {
        g_release(old);
        Note("following another player");
    }
}

// Optional: the platform works without it, so a missing export is noted, never fatal.
bool ResolveStoredVolume() {
    HMODULE cf = GetModuleHandleW(L"CoreFoundation.dll");
    auto find = [cf](const char* name) { return cf ? reinterpret_cast<void*>(GetProcAddress(cf, name)) : nullptr; };
    void* application = find("kCFPreferencesCurrentApplication");
    auto createString = detail::FromAddress<CFStringCreateWithCString_t>(find("CFStringCreateWithCString"));
    g_stored.GetTypeID = detail::FromAddress<CFGetTypeID_t>(find("CFGetTypeID"));
    g_stored.NumberTypeID = detail::FromAddress<CFNumberGetTypeID_t>(find("CFNumberGetTypeID"));
    g_stored.NumberGetValue = detail::FromAddress<CFNumberGetValue_t>(find("CFNumberGetValue"));
    g_stored.CopyAppValue = detail::FromAddress<CFPreferencesCopyAppValue_t>(find("CFPreferencesCopyAppValue"));
    if (!application || !createString || !g_stored.GetTypeID || !g_stored.NumberTypeID || !g_stored.NumberGetValue ||
        !g_stored.CopyAppValue) {
        Note("CoreFoundation preferences not found: the volume is known once a song starts");
        return false;
    }
    g_stored.application = *static_cast<const void* const*>(application);
    g_stored.key = createString(nullptr, "volumeWSG", kUtf8);
    return g_stored.application && g_stored.key;
}

}  // namespace

namespace detail {

bool ResolvePlayerApi() {
    return Resolve("CFRetain", &g_retain) && Resolve("CFRelease", &g_release);
}

bool HookPlayerVolume() {
    size_t imports = HookImportEverywhere("AVFoundationCF.dll", "AVCFPlayerSetVolume", AddressOf(SetVolume_Hook),
                                          reinterpret_cast<void**>(&g_setVolumeOriginal));
    if (imports == 0) {
        Note("AVCFPlayerSetVolume is imported by no module: volume not followed, leveler inactive");
        return false;
    }
    Note("hook AVCFPlayerSetVolume placed in " + std::to_string(imports) + " import(s)");
    return true;
}

float LastPlayerVolume() { return g_playerVolume.load(); }

void PublishStoredVolume() {
    static bool resolved = ResolveStoredVolume();
    static bool noted = false;
    if (!resolved || g_playerVolumeSeen.load()) return;
    const void* value = g_stored.CopyAppValue(g_stored.key, g_stored.application);
    if (!value) return;
    double gain = 0;
    bool number = g_stored.GetTypeID(value) == g_stored.NumberTypeID() &&
                  g_stored.NumberGetValue(value, kCFNumberDoubleType, &gain) != 0;
    g_release(value);
    if (!number || g_playerVolumeSeen.load()) return;
    audio::ProcessAudio().PublishPlayerVolume(gain);
    if (!noted) Note("volume read from Apple Music's preferences: " + Fixed(gain, 3));
    noted = true;
}

}  // namespace detail

void* RetainFollowedPlayer() {
    std::lock_guard<std::mutex> guard(g_playerLock);
    if (g_player) g_retain(g_player);
    return g_player;
}

bool SetPlayerVolume(double volume) {
    void* player = RetainFollowedPlayer();
    if (!player || !g_setVolumeOriginal) return false;
    // Through the hook, so that the policy and Ampify.audio.volume() follow the new value as they follow Apple's.
    SetVolume_Hook(player, static_cast<float>(volume));
    g_release(player);
    return true;
}

}  // namespace ampify::win::audio_hooks
