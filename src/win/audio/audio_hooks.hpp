#pragma once

// Ampify's audio platform in AMPLibraryAgent.exe, where Apple Music decodes and renders the sound. Hooks reach what it
// renders:
//  - stereo: the WASAPI render client (IAudioRenderClient), float32 samples, after the player volume;
//  - Dolby Atmos: the spatial audio objects, one mono buffer per object, before the slider volume.
// On those samples run the gain (a policy's, such as the Atmos leveler's, times the elements'), Ampify.audio's
// equalizer and the soft knee; the live loudness and the analysis read them (src/core/audio/sample_path.hpp).
// The player volume is followed through AVCFPlayerSetVolume, and set the same way for Ampify.player.setVolume.
// Every hook goes through src/win/hook.hpp, whose guard refuses CoreFP, CoreADI64 and FairPlay. Not compiled on Linux.

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <vector>

#include "../../core/journal.hpp"

namespace ampify::win::audio_hooks {

// Every line of the platform and of the leveler starts with it: the bench's live cases read them. Named after the
// leveler, which held the platform until the split; renaming it means changing those cases in the same commit.
constexpr const char* kJournalPrefix = "Atmos: ";

// A gain policy on the sample path: the Atmos leveler (extensions/atmos-leveler). The Feed and rate calls come from the
// audio threads, which must never wait: a policy that cannot take its lock at once skips the block.
class GainPolicy {
public:
    virtual ~GainPolicy() = default;

    // Once Apple's modules are loaded, before the hooks go in. False: the policy is left out and the gain stays 1.
    virtual bool Prepare() = 0;

    // The rate of the stereo stream, or of the spatial objects, once a stream of that kind is seen.
    virtual void StreamRate(int sampleRate) = 0;
    virtual void ObjectRate(int sampleRate) = 0;

    // Stereo samples before the gain, with the player volume `playerVolume` already in them.
    virtual void FeedStream(const float* samples, size_t frames, float playerVolume) = 0;
    // The spatial objects of one update pass, before the gain and the slider volume: power per frame and peak.
    virtual void FeedObjects(const std::vector<double>& framePower, float peak) = 0;

    // About every 300 ms, on the platform's thread: the linear gain the hooks apply from now on.
    virtual float Poll() = 0;
};

// The policy that Start prepares and the hooks feed. Set before Start; it lives until the process exits.
void SetPolicy(GainPolicy* policy);

enum class StartResult { Placed, AlreadyPlaced, Failed };

// Waits for Apple's audio modules (up to 60 s), hooks the player volume and the audio, then runs the poll thread.
// The hooks stay until the process exits; a later call finds them in place. Call it off the interface's paths.
StartResult Start(Journal& journal);

// The same, for the sound alone (a theme's bars(n), Ampify.audio in the interface) when no policy started them.
bool StartSoundHooks(Journal& journal);

bool Placed();

// Sets Apple's player volume (0 to 1), as Apple does itself. False until Apple has set a volume since the hook is in
// place (the player is not known yet). Apple's own slider is not redrawn: it shows the value until Apple moves it.
bool SetPlayerVolume(double volume);

// The player Apple Music set a volume on last, retained for the caller (CFRelease), or null.
void* RetainFollowedPlayer();

// True while the stereo stream carried sound in the last half second.
bool SoundIsPlaying();

// The export `name` of a loaded module, as a function of type Fn. False when the module or the export is missing.
template <typename Fn>
bool FindExport(const wchar_t* module, const char* name, Fn& out) {
    HMODULE handle = GetModuleHandleW(module);
    FARPROC address = handle ? GetProcAddress(handle, name) : nullptr;
    if (!address) return false;
    std::memcpy(&out, &address, sizeof(out));  // a function address of another type: copied, not cast
    return true;
}

}  // namespace ampify::win::audio_hooks
