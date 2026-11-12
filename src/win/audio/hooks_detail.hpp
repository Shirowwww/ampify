#pragma once

// What the files of the audio hooks share among themselves: the journal, and the casts between functions and plain
// addresses. Not for code outside src/win/audio/.

#include <cstring>
#include <string>

#include "../../core/journal.hpp"

namespace ampify::win::audio_hooks::detail {

void SetJournal(Journal& journal);
void Note(const std::string& message);  // with kJournalPrefix; nothing before SetJournal

// A number for the log, with a fixed number of decimals.
std::string Fixed(double value, int decimals);

// The address of a function as a plain pointer, without a cast the compiler would warn about.
template <typename Fn>
void* AddressOf(Fn fn) {
    void* address = nullptr;
    std::memcpy(&address, &fn, sizeof(address));
    return address;
}

// The other way round: a plain pointer called as a function of type Fn.
template <typename Fn>
Fn FromAddress(void* address) {
    Fn fn = nullptr;
    std::memcpy(&fn, &address, sizeof(fn));
    return fn;
}

// The player volume (player_volume.cpp), for the hooks.
bool ResolvePlayerApi();     // CFRetain and CFRelease, which following the player needs
bool HookPlayerVolume();     // AVCFPlayerSetVolume in every module that imports it
float LastPlayerVolume();    // what Apple Music set on its player last, 1 until then
bool PlayerVolumeSeen();     // Apple Music set a volume on its player since Ampify came in
void PublishStoredVolume();  // the slider's value from Apple's preferences, until the player has one. Poll thread

// AutoMix (automix.cpp): Apple's queue player followed, and what ends a mix (pause, seek, skip) watched.
void StartAutoMixWatch();
float AutoMixMainGain(void* player);  // what a mix leaves of Apple's volume on `player`, 1 outside a mix
void SetPlayerVolumeDirect(void* player, float volume);  // past the hook: the slider's value stays

// Diagnostic (output_record.cpp): while record.on exists in the kit, what the stereo stream sends goes to a WAV file.
void RecordOutput(const float* samples, unsigned frames, int rate);  // audio thread, after Ampify's steps
void PollRecording();                                                // poll thread

}  // namespace ampify::win::audio_hooks::detail
