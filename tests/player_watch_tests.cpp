#include <string>

#include "../core/player_watch.hpp"
#include "test.hpp"

namespace {

ampify::PlayerSnapshot Snapshot(const std::string& trackId, const std::string& variant, bool playing) {
    ampify::PlayerSnapshot snapshot;
    snapshot.playing = playing;
    if (!trackId.empty()) {
        ampify::TrackInfo track;
        track.id = trackId;
        track.variantId = variant;
        snapshot.track = track;
    }
    return snapshot;
}

}  // namespace

TEST_CASE(PlayerWatcher_TheFirstSnapshotOnlyRecordsTheState) {
    ampify::PlayerWatcher watcher;
    CHECK(watcher.Poll(Snapshot("1", "v1", true)).empty());
}

TEST_CASE(PlayerWatcher_NoEventWhenNothingChanged) {
    ampify::PlayerWatcher watcher;
    watcher.Poll(Snapshot("1", "v1", true));
    CHECK(watcher.Poll(Snapshot("1", "v1", true)).empty());
}

TEST_CASE(PlayerWatcher_ReportsAPauseAndAResume) {
    ampify::PlayerWatcher watcher;
    watcher.Poll(Snapshot("1", "v1", true));
    auto paused = watcher.Poll(Snapshot("1", "v1", false));
    CHECK(paused.size() == 1);
    CHECK(paused[0] == "statechange");
    auto resumed = watcher.Poll(Snapshot("1", "v1", true));
    CHECK(resumed.size() == 1);
    CHECK(resumed[0] == "statechange");
}

TEST_CASE(PlayerWatcher_ReportsANewTrack) {
    ampify::PlayerWatcher watcher;
    watcher.Poll(Snapshot("1", "v1", true));
    auto events = watcher.Poll(Snapshot("2", "v1", true));
    CHECK(events.size() == 1);
    CHECK(events[0] == "trackchange");
}

TEST_CASE(PlayerWatcher_ANewVariantIsANewTrack) {
    ampify::PlayerWatcher watcher;
    watcher.Poll(Snapshot("1", "stereo", true));
    auto events = watcher.Poll(Snapshot("1", "atmos", true));
    CHECK(events.size() == 1);
    CHECK(events[0] == "trackchange");
}

TEST_CASE(PlayerWatcher_ATrackChangeAndAStateChangeAreBothReported) {
    ampify::PlayerWatcher watcher;
    watcher.Poll(Snapshot("1", "v1", true));
    auto events = watcher.Poll(Snapshot("2", "v1", false));
    CHECK(events.size() == 2);
    CHECK(events[0] == "trackchange");
    CHECK(events[1] == "statechange");
}

TEST_CASE(PlayerWatcher_NothingLoadedIsAState) {
    ampify::PlayerWatcher watcher;
    watcher.Poll(Snapshot("1", "v1", true));
    auto events = watcher.Poll(Snapshot("", "", false));
    CHECK(events.size() == 2);
    CHECK(events[0] == "trackchange");
}
