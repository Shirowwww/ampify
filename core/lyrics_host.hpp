#pragma once

// The optional host service behind Ampify.player.lyrics() (permission player.read). See core/host.hpp for how a
// capability works. The Windows host offers it in AppleMusic.exe (win/player_bridge.*); a host without it makes
// Ampify.player.lyrics() answer null.

#include <optional>
#include <string_view>

#include "lyrics.hpp"

namespace ampify {

struct LyricsHost {
    static constexpr std::string_view kCapabilityName = "player.lyrics";
    virtual ~LyricsHost() = default;

    // The raw lyrics of the current track as the player holds them, or nullopt while it has none (nothing playing,
    // no lyrics for the song, or Apple Music has not fetched them yet).
    virtual std::optional<RawLyrics> Lyrics() = 0;
};

}  // namespace ampify
