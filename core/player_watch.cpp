#include "player_watch.hpp"

namespace ampify {

std::vector<std::string> PlayerWatcher::Poll(const PlayerSnapshot& snapshot) {
    std::optional<std::string> track;
    // The queue entry counts too: the same song played again from the queue is a new track for the elements.
    if (snapshot.track) track = snapshot.track->id + "|" + snapshot.track->variantId + "|" + snapshot.track->queueItemId;

    const bool lyrics = track && snapshot.lyricsAvailable;
    std::vector<std::string> events;
    if (m_known) {
        const bool sameTrack = track == m_track;
        if (!sameTrack) events.push_back("trackchange");
        if (snapshot.playing != m_playing) events.push_back("statechange");
        if (lyrics && !(sameTrack && m_lyrics)) events.push_back("lyricschange");
    }
    m_track = track;
    m_playing = snapshot.playing;
    m_lyrics = lyrics;
    m_known = true;
    return events;
}

}  // namespace ampify
