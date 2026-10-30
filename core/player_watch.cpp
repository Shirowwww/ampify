#include "player_watch.hpp"

namespace amk {

std::vector<std::string> PlayerWatcher::Poll(const PlayerSnapshot& snapshot) {
    std::optional<std::string> track;
    if (snapshot.track) track = snapshot.track->id + "|" + snapshot.track->variantId;

    std::vector<std::string> events;
    if (m_known) {
        if (track != m_track) events.push_back("trackchange");
        if (snapshot.playing != m_playing) events.push_back("statechange");
    }
    m_track = track;
    m_playing = snapshot.playing;
    m_known = true;
    return events;
}

}  // namespace amk
