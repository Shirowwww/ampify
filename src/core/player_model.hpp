#pragma once

// The portable half of the Windows player (src/win/player_bridge.*): what the raw readings of Apple's player
// (AMP.Services.Player) and of the Windows media controls (SMTC) become in a PlayerSnapshot, and which way a command
// goes. No Windows API here, so that the Linux tests cover it.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "host.hpp"

namespace ampify {

// The cover of the item, as IArtwork gives it. Every field may be missing.
struct ArtworkReading {
    std::string uri;       // IArtwork.get_Uri, often a template with {w}, {h}, {c} and {f}
    std::string format;    // "jpg", "png"...
    std::string cropCode;  // IArtwork.GetCropCode, "bb", "cc"...
    double width = 0;
    double height = 0;
    bool hasColor = false;  // get_BGColor answered
    uint8_t alpha = 0, red = 0, green = 0, blue = 0;
};

// The https address of the cover at about 1000 px: the template filled in, or the address as it is when it has no
// placeholder. Empty for anything that is not an https address.
std::string ArtworkAddress(const ArtworkReading& artwork);
// "#RRGGBB" from the colour, or empty when Apple gave none (a fully transparent black counts as none).
std::string ArtworkColorHex(const ArtworkReading& artwork);

// What src/win/amp_player.cpp reads from AMP.Services.Player, as raw values.
struct AmpReading {
    bool playing = false;
    bool paused = false;
    bool hasItem = false;  // a NowPlayingItem exists
    bool placeholder = false;
    std::string title;  // INowPlayingItem.Name
    std::string artist;
    std::string album;
    std::string subtitle;  // INowPlayingItem.Subtitle, "Artist \u2014 Album"
    int64_t positionRaw = 0;  // INowPlayingItem.CurrentPosition, in the unit given by AmpTimeUnit
    int64_t durationRaw = 0;
    int32_t audioFormat = 0;  // amp::AudioFormat
    uint64_t queueItemId = 0;
    ArtworkReading artwork;
    bool lyricsAvailable = false;  // ILyricsInfo.HaveLyrics said yes and the text was read
};

// One entry of Apple's play queue, as src/win/amp_player.cpp reads it.
struct AmpQueueEntry {
    uint64_t id = 0;          // IPlayQueueItem.PlayQueueItemID
    int32_t sectionKind = 0;  // AMP.Services.PlayQueueSectionKind of the entry's section
    std::string title;        // ITitledComponent.Title
    std::string subtitle;     // IPlayQueueItem.Subtitle, the artist
    int64_t durationRaw = 0;  // IPlayQueueItem.Duration, in an AmpTimeUnit
    std::string catalogId;    // IContentDescriptor.AdamId, empty for a file that is not in the catalog
};

// What src/win/smtc_session.cpp reads from the Apple Music session of the Windows media controls.
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
const char* AmpTimeUnitName(AmpTimeUnit unit);  // "s", "ms", "100 ns", "unknown"

// A stable id for a track from its title, artist and album: "t-" and 16 hexadecimal characters. The same song gives
// the same id whatever the source (Apple's player or SMTC), so an element can keep per-track data.
std::string TrackIdOf(const std::string& title, const std::string& artist, const std::string& album);

// The audio format as variantId ("lossless", "hi-res-lossless", "dolby-atmos", "spatial", "surround", "other"), and
// whether it is Atmos or lossless.
std::string VariantOfAudioFormat(int32_t format);
bool IsAtmosFormat(int32_t format);
bool IsLosslessFormat(int32_t format);

// Apple Music writes "Artist \u2014 Album" where one line holds both (its subtitle, and its media session's artist):
// the two parts, or the text and an empty album when there is no separator.
std::pair<std::string, std::string> SplitArtistAlbum(const std::string& text);

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
const char* RouteName(CommandRoute route);       // "Apple", "SMTC", "media key"

}  // namespace ampify
