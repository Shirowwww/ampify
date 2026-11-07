#include "player_state.hpp"

#include <utility>

namespace ampify {

std::string TrackKey(const PlayerSnapshot& snapshot) {
    if (!snapshot.track) return "";
    return snapshot.track->id + "|" + snapshot.track->queueItemId;
}

Json PlayerJson(const PlayerSnapshot& snapshot) {
    Json::Object members;
    members.emplace_back("playing", Json(snapshot.playing));
    members.emplace_back("positionMs", Json(static_cast<double>(snapshot.positionMs)));
    members.emplace_back("durationMs", Json(static_cast<double>(snapshot.durationMs)));
    if (!snapshot.track) {
        members.emplace_back("track", Json());
    } else {
        const TrackInfo& t = *snapshot.track;
        Json::Object track;
        track.emplace_back("id", Json(t.id));
        track.emplace_back("title", Json(t.title));
        track.emplace_back("artist", Json(t.artist));
        track.emplace_back("album", Json(t.album));
        track.emplace_back("variantId", Json(t.variantId));
        track.emplace_back("atmos", Json(t.atmos));
        track.emplace_back("lossless", Json(t.lossless));
        track.emplace_back("queueItemId", Json(t.queueItemId));
        track.emplace_back("artwork", Json(t.artwork));
        track.emplace_back("artworkColor", Json(t.artworkColor));
        members.emplace_back("track", Json(std::move(track)));
    }
    members.emplace_back("source", Json(snapshot.source));
    return Json(std::move(members));
}

}  // namespace ampify
