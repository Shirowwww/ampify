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
float SpeedFor(float rate);  // what AVCFPlayerSetRate's hook passes on for Apple's rate: the speed for a 1
float LastPlayerVolume();    // what Apple Music set on its player last; until then, the slider's stored value
void PublishStoredVolume();  // the slider's value from Apple's preferences, until the player has one. Poll thread

// AutoMix (automix.cpp): Apple's queue player followed, and what ends a mix (pause, seek, skip) watched.
void StartAutoMixWatch();
bool AutoMixOpen();  // a mix is being prepared or played: the two calls below have work to do
// One block of a stream, on its audio thread, in place. `stream` is the stream's number (each new one gets the
// next), `queued` is how many frames Windows had not taken from it yet (mix_deck.hpp).
void AutoMixMain(float* samples, unsigned frames, int rate, std::uint64_t stream, unsigned queued);  // Apple's
// The second player's: its sound comes in (`silent`: none), the old song goes out. False when that is silence.
bool AutoMixTail(float* samples, unsigned frames, int rate, bool silent, std::uint64_t stream, unsigned queued);

// The second player's streams (audio_hooks.cpp). While they are expected, every new render client is the second
// player's: what it writes goes to AutoMixTail, what AutoMixTail leaves is what Windows gets, and it does not
// become the stream Ampify meters. One the deck cannot take (another rate) is kept silent, never heard as it comes.
// Apple's player makes a stream of its own at each song and at each seek, and the second player one when it starts,
// at each seek and whenever it moves to another version of the song as it plays (20/11/2026: one at 96 kHz, taken
// for Apple's, played the song 19 dB over it). Once a switch is armed, only the first new stream from about its time
// on is Apple's (ExpectAppleStreamIn).
// shortcut: a stream past the slots is not followed and plays as decoded until the mix's next tick ends the mix;
// reuse the slots of streams that stopped if a mix ever makes that many (one at the start and per seek, up to 5).
inline constexpr int kTailStreamSlots = 16;  // of one mix: the second player's start, each seek, other versions
void ExpectTailStreams(bool expected);  // false: the ones seen are kept, a new stream is Apple's again
// A switch is armed: the first new stream from `seconds` from now on is Apple's, for the next song; any other the
// second player's, as before.
void ExpectAppleStreamIn(double seconds);
void ForgetTailStreams();               // the mix is over
bool TailStreamsClaimed();              // one or more seen, and none that could not be taken
int MainStreamRate();                   // the rate of Apple's stereo stream, 0 when there is none to mix into

// Sing's studio mode holds the sound back (sing.cpp): Apple's player is told that much earlier a time
// (AVCFPlayerItemGetCurrentTime), so that its lyrics and position follow what is heard. It reads its time when it
// starts, pauses or jumps; JumpInPlace makes it jump to where it is. automix.cpp holds the player, so both live there.
void HoldClockBack(double seconds);  // 0: Apple's own time again
bool JumpInPlace(double back);       // to `back` seconds before where it is; false when the player is not known yet
bool InAtmosNow();                   // the song playing goes out as Dolby Atmos objects
const void* CurrentSong();           // the item Apple's queue plays, only to compare; null when not known
// Sing on: Apple's player is to prefer the songs' stereo versions. Asked at once while the song there is known to be in
// stereo or there is none, otherwise once one is or when Apple makes its next queue; Apple's choice comes back at once.
void WantStereo(bool want);
// Sing (sing.cpp), called from the hooks: one block of Apple's stereo stream, in place, on its audio thread; and a
// seek or a skip, after which what is held back belongs to another moment of the song.
void StartSing();  // once the hooks are in: Sing's thread follows what the interface asks
void SingMain(float* samples, unsigned frames, int rate);
// One block of the deck's stream (AutoMix's old song), in place, `heard` false when the deck gave silence (then
// `samples` is written over). True when sound goes out: in studio mode, what it holds plays on after the deck.
bool SingTail(float* samples, unsigned frames, int rate, bool heard);
void SingSeeked();
void SingPaused();  // after Apple's player paused
void SingPlays();   // before Apple's player plays
// Apple's local file player (local_player.cpp): its output unit started, reset (what it queued dropped: a pause, a
// skip or a move in the song) and stopped (a second after a pause).
void StartLocalPlayerWatch();
void SingLocalPlays();    // before it starts
void SingLocalReset();    // after it reset
void SingLocalStopped();  // after it stopped
bool SingHoldsSound();  // studio mode on: Apple's silent blocks are played through it, so what it holds is heard
double SingLag();       // seconds studio mode holds the sound back, 0 when off: a mix waits that long to end
void SingTailNewMix();  // a mix's second player starts or stops: nothing the deck's stream held before is played
void SingTailDrop();    // what the deck's stream holds goes now, after a short fade: the user moved on

// Diagnostic (output_record.cpp): while record.on exists in the kit, what the stereo stream sends goes to a WAV file.
void RecordOutput(const float* samples, unsigned frames, int rate);  // audio thread, after Ampify's steps
void PollRecording();                                                // poll thread

}  // namespace ampify::win::audio_hooks::detail
