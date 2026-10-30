#pragma once

#include <optional>
#include <string>
#include <vector>

#include "host.hpp"

namespace amk {

// Turns successive player snapshots into the events that elements subscribe to: "trackchange" when the track or
// its variant changes, "statechange" when playback starts or stops. The first snapshot only records the state.
class PlayerWatcher {
public:
    // The events for this snapshot, in order. Empty when nothing changed.
    std::vector<std::string> Poll(const PlayerSnapshot& snapshot);

private:
    std::optional<std::string> m_track;  // track id and variant, or empty when nothing is loaded
    bool m_playing = false;
    bool m_known = false;
};

}  // namespace amk
