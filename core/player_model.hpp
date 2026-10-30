#pragma once

// The portable half of the Windows player (win/player_bridge.*): what the raw readings of Apple's player
// (AMP.Services.Player) and of the Windows media controls (SMTC) become in a PlayerSnapshot, and which way a command
// goes. No Windows API here, so that the Linux tests cover it.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "host.hpp"

namespace amk {

// What win/amp_player.cpp reads from AMP.Services.Player, as raw values.
struct AmpReading {
    bool playing = false;
    bool paused = false;
    bool hasItem = false;  // a NowPlayingItem exists
    bool placeholder = false;
    std::string title;  // INowPlayingItem.Name
    std::string artist;
    std::string album;
    int64_t positionRaw = 0;  // INowPlayingItem.CurrentPosition, in the unit given by AmpTimeUnit
    int64_t durationRaw = 0;
    int32_t audioFormat = 0;  // amp::AudioFormat
    uint64_t queueItemId = 0;
};

// What win/smtc_session.cpp reads from the Apple Music session of the Windows media controls.
struct SmtcReading {
    int32_t status = 0;  // GlobalSystemMediaTransportControlsSessionPlaybackStatus: 4 playing, 5 paused
    std::string title;
    std::string artist;
    std::string album;
    int64_t startTicks = 0;  // TimeSpan, 100 ns
    int64_t endTicks = 0;
    int64_t positionTicks = 0;
    int64_t lastUpdated = 0;  // DateTime of the position, 100 ns since 1601 (FILETIME)
    int64_t now = 0;          // the same clock, when the reading was taken
};

inline constexpr int32_t kSmtcPlaying = 4;
inline constexpr int32_t kSmtcPaused = 5;

// The unit of INowPlayingItem.CurrentPosition and Duration, declared Int64 in the winmd without a unit.
enum class AmpTimeUnit { Unknown, Seconds, Milliseconds, Ticks100ns };

// Guesses the unit from a duration. With the SMTC duration of the same track (in ms), the closest unit wins; without
// it, a song is assumed to last between 20 s and 10 h. Unknown when the duration is 0.
AmpTimeUnit DetectAmpTimeUnit(int64_t durationRaw, std::optional<int64_t> smtcDurationMs);
int64_t AmpTimeToMs(int64_t raw, AmpTimeUnit unit);
const char* AmpTimeUnitName(AmpTimeUnit unit);  // "s", "ms", "100 ns", "inconnue"

// A stable id for a track from its title, artist and album: "t-" and 16 hexadecimal characters. The same song gives
// the same id whatever the source (Apple's player or SMTC), so an element can keep per-track data.
std::string TrackIdOf(const std::string& title, const std::string& artist, const std::string& album);

// The audio format as variantId ("lossless", "hi-res-lossless", "dolby-atmos", "spatial", "surround", "other"), and
// whether it is Atmos or lossless.
std::string VariantOfAudioFormat(int32_t format);
bool IsAtmosFormat(int32_t format);
bool IsLosslessFormat(int32_t format);

PlayerSnapshot SnapshotFromAmp(const AmpReading& reading, AmpTimeUnit unit);
// The position moves on between two SMTC updates while playing: it is extrapolated to `now`, and kept within the
// track.
PlayerSnapshot SnapshotFromSmtc(const SmtcReading& reading);

// ---- Commands -----------------------------------------------------------------------------------------------------

enum class CommandRoute {
    Apple,     // IPlayer.PlayAsync and the others, in Apple Music's process
    Smtc,      // the Windows media controls, from any process
    MediaKey,  // a media key, as from a keyboard
};

struct CommandStep {
    CommandRoute route;
    // For MediaKey: the key. Play and pause share one toggle key, so it is only sent when the state is known.
    enum class Key { None, PlayPause, Next, Previous } key = Key::None;
};

struct RouteAvailability {
    bool apple = false;      // AMP.Services.Player activated, and its commands allowed (setting player.appleCommands)
    bool smtc = false;       // the Apple Music session of SMTC was found
    bool mediaKeys = false;  // SendInput is possible (Windows)
};

struct CommandPlan {
    bool alreadyDone = false;  // play while playing, pause while paused: nothing to do, the answer is yes
    std::vector<CommandStep> steps;  // tried in order, until one is accepted
    int64_t seekMs = 0;              // for Seek: the position, kept within the track when its duration is known
};

// `known` is the last snapshot. Its source tells whether the state can be trusted (empty: unknown).
CommandPlan PlanCommand(PlayerCommand command, int64_t seekMs, const PlayerSnapshot& known,
                        const RouteAvailability& available);

const char* CommandName(PlayerCommand command);  // "play", "pause", "next", "previous", "seek"
const char* RouteName(CommandRoute route);       // "Apple", "SMTC", "touche multimédia"

}  // namespace amk
