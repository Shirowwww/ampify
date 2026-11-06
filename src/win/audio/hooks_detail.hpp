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
void PublishStoredVolume();  // the slider's value from Apple's preferences, until the player has one. Poll thread

}  // namespace ampify::win::audio_hooks::detail
