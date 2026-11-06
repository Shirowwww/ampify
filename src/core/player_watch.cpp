#include "player_watch.hpp"

#include <cstdlib>

namespace ampify {

namespace {

// A position this far from where steady playback would be counts as a seek.
constexpr int64_t kSeekToleranceMs = 3000;
constexpr int64_t kMsPerSecond = 1000;

}  // namespace

std::vector<std::string> PlayerWatcher::Poll(const PlayerSnapshot& snapshot, int64_t nowMs) {
    std::optional<std::string> track;
    // The queue entry counts too: the same song played again from the queue is a new track for the elements.
    if (snapshot.track) track = snapshot.track->id + "|" + snapshot.track->variantId + "|" + snapshot.track->queueItemId;

    const bool lyrics = track && snapshot.lyricsAvailable;
    std::vector<std::string> events;
    if (m_known) {
        const bool sameTrack = track == m_track;
        if (!sameTrack) events.push_back("trackchange");
        if (snapshot.playing != m_playing || (sameTrack && Moved(snapshot, nowMs))) events.push_back("statechange");
        if (lyrics && !(sameTrack && m_lyrics)) events.push_back("lyricschange");
    }
    m_progressed = m_known && track && snapshot.positionMs / kMsPerSecond != m_positionMs / kMsPerSecond;
    m_track = track;
    m_playing = snapshot.playing;
    m_lyrics = lyrics;
    m_positionMs = snapshot.positionMs;
    m_durationMs = snapshot.durationMs;
    m_polledAtMs = nowMs;
    m_known = true;
    return events;
}

bool PlayerWatcher::Moved(const PlayerSnapshot& snapshot, int64_t nowMs) const {
    if (snapshot.playing != m_playing) return false;  // reported as a start or a stop already
    // The length became known, or Windows replaced the previous track's length; rounding drift is not a change.
    if (snapshot.durationMs > 0 && std::llabs(snapshot.durationMs - m_durationMs) > kSeekToleranceMs) return true;
    if (nowMs < 0 || m_polledAtMs < 0) return false;
    int64_t expected = m_positionMs + (snapshot.playing ? nowMs - m_polledAtMs : 0);
    return std::llabs(snapshot.positionMs - expected) > kSeekToleranceMs;
}

}  // namespace ampify
