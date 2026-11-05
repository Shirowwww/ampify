#pragma once

// What plays now, shared inside one process: the extension thread reads Apple's player every 250 ms and publishes it
// here, and the interface thread draws from it (the karaoke of the `lyrics` node, src/win/ui_renderer.cpp) at its own
// pace, moving the position on between two reads. Any thread.

#include <cstdint>
#include <memory>
#include <string>

#include "lyrics.hpp"

namespace ampify {

struct NowPlaying {
    std::string trackKey;  // empty when nothing plays; changes with each new song or queue entry
    bool playing = false;
    bool moving = false;  // playing and heard moving: false while the song loads or its stream stalls
    int64_t positionMs = 0;
    int64_t atMs = 0;                      // steady clock, in ms, when positionMs was read
    std::shared_ptr<const Lyrics> lyrics;  // of trackKey, null until Apple has them

    // The position at `nowMs` on the same clock as atMs: it moves on while the song is heard moving.
    int64_t PositionAt(int64_t nowMs) const { return moving && nowMs > atMs ? positionMs + (nowMs - atMs) : positionMs; }
};

void PublishNowPlaying(NowPlaying now);
NowPlaying CurrentNowPlaying();

}  // namespace ampify
