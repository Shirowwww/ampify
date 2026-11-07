#pragma once

// What plays, as every part of Ampify sees it: the state handed to elements, the identity of the entry being heard,
// the 100 ns ticks Windows counts time in, and the one model of how a position moves on between two readings. The
// producers (src/win/player_bridge.*, src/core/player_model.*) and the consumers (src/core/player_watch.*,
// src/core/now_playing.*, the elements through PlayerJson) share these definitions instead of each writing its own.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "json.hpp"

namespace ampify {

struct TrackInfo {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string variantId;
    bool atmos = false;
    bool lossless = false;
    // The entry of the play queue that holds this track, as Apple numbers it. The same song played twice has two
    // entries. Empty when the source does not tell it (SMTC).
    std::string queueItemId;
    // The cover as an https address of a 1000 px image, and its dominant colour as "#RRGGBB". Empty when the source
    // does not give them (SMTC) or Apple has none.
    std::string artwork;
    std::string artworkColor;
};

struct PlayerSnapshot {
    bool playing = false;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    std::optional<TrackInfo> track;  // empty when nothing is loaded
    // Where the state was read: "apple" (AMP.Services.Player, inside Apple Music), "smtc" (the Windows media
    // controls), or empty when no source answered. An empty source means "unknown", not "stopped".
    std::string source;
    // Apple has lyrics for the current track and the kernel holds them. Not shown to elements: it drives the
    // "lyricschange" event (src/core/player_watch.*), and Ampify.player.lyrics() gives the lyrics themselves.
    bool lyricsAvailable = false;
};

enum class PlayerCommand { Play, Pause, Next, Previous, Seek };

// The entry being heard: the song and its queue entry, so the same song played again from the queue is another one.
// Empty when nothing is loaded. The position clock, the now-playing feed and the player events all start over on it.
std::string TrackKey(const PlayerSnapshot& snapshot);

// The kernel's extension thread turns every kServiceTick and reads the player every kPlayerPeriod; services that
// follow the player count kTicksPerPlayerRead of their own turns.
inline constexpr std::chrono::milliseconds kServiceTick{50};
inline constexpr std::chrono::milliseconds kPlayerPeriod{250};
inline constexpr int kTicksPerPlayerRead = static_cast<int>(kPlayerPeriod / kServiceTick);

// Windows' TimeSpan and DateTime count 100 ns ticks.
inline constexpr int64_t kTicksPerMs = 10'000;
constexpr int64_t TicksToMs(int64_t ticks) { return ticks / kTicksPerMs; }
constexpr int64_t MsToTicks(int64_t ms) { return ms * kTicksPerMs; }

// Apple renews a playing song's position about once a second (Apple's player and SMTC alike): a position left still
// longer than kPositionStallMs is not heard moving (loading, or a stalled stream), and one further than
// kSeekToleranceMs from where steady playback would have taken it was moved by someone.
inline constexpr int64_t kPositionStallMs = 2000;
inline constexpr int64_t kSeekToleranceMs = 3000;

// A position read at `at`, moved on to `now` (one clock, one unit) while it is heard moving; never moved back.
constexpr int64_t ExtrapolatePosition(int64_t position, int64_t at, int64_t now, bool moving) {
    return moving && now > at ? position + (now - at) : position;
}

// The player state as Ampify.player.state() returns it, as the player events carry it and as GET /status gives it.
Json PlayerJson(const PlayerSnapshot& snapshot);

}  // namespace ampify
