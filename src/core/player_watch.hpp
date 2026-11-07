#pragma once

#include <optional>
#include <string>
#include <vector>

#include "player_state.hpp"

namespace ampify {

// Turns successive player snapshots into the events that elements subscribe to: "trackchange" when the track or
// its variant or its queue entry changes, "statechange" when playback starts or stops, when the track's length
// becomes known or after a seek, "lyricschange" when the lyrics of the current track become available (after
// "trackchange" when both happen together). The first snapshot only records the state.
class PlayerWatcher {
public:
    // The events for this snapshot, in order. Empty when nothing changed. `nowMs` is a steady clock reading of the
    // poll, needed to tell a seek from playback; without it (-1), seeks are not reported.
    std::vector<std::string> Poll(const PlayerSnapshot& snapshot, int64_t nowMs = -1);

    // The last Poll found the loaded track in another whole second than the poll before (playing on, or a seek):
    // the "progress" event, kept out of Poll's list because it fires about once a second.
    bool Progressed() const { return m_progressed; }

private:
    // Same track: its length changed, or its position is not where steady playback would have taken it.
    bool Moved(const PlayerSnapshot& snapshot, int64_t nowMs) const;

    struct Heard {
        std::string entry;  // TrackKey
        std::string variant;
        bool operator==(const Heard&) const = default;
    };
    std::optional<Heard> m_track;  // empty when nothing is loaded
    bool m_playing = false;
    bool m_lyrics = false;  // lyrics were available for m_track at the last snapshot
    int64_t m_positionMs = 0;
    int64_t m_durationMs = 0;
    int64_t m_polledAtMs = -1;
    bool m_known = false;
    bool m_progressed = false;
};

}  // namespace ampify
