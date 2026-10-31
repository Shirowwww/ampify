#pragma once

#include <optional>
#include <string>
#include <vector>

#include "host.hpp"

namespace ampify {

// Turns successive player snapshots into the events that elements subscribe to: "trackchange" when the track or
// its variant or its queue entry changes, "statechange" when playback starts or stops, "lyricschange" when the
// lyrics of the current track become available (after "trackchange" when both happen together). The first snapshot
// only records the state.
class PlayerWatcher {
public:
    // The events for this snapshot, in order. Empty when nothing changed.
    std::vector<std::string> Poll(const PlayerSnapshot& snapshot);

private:
    std::optional<std::string> m_track;  // track id, variant and queue entry, or empty when nothing is loaded
    bool m_playing = false;
    bool m_lyrics = false;  // lyrics were available for m_track at the last snapshot
    bool m_known = false;
};

}  // namespace ampify
