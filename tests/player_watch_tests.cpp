#include <string>

#include "../src/core/player_watch.hpp"
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

TEST_CASE(PlayerWatcher_ProgressFollowsEachNewSecondOfTheTrack) {
    auto at = [](bool playing, int64_t positionMs) {
        ampify::PlayerSnapshot snapshot = Snapshot("1", "v1", playing);
        snapshot.positionMs = positionMs;
        snapshot.durationMs = 200'000;
        return snapshot;
    };
    ampify::PlayerWatcher watcher;
    watcher.Poll(at(true, 1'200), 0);
    CHECK(!watcher.Progressed());  // the first snapshot only records the state
    watcher.Poll(at(true, 1'450), 250);
    CHECK(!watcher.Progressed());  // the same second
    watcher.Poll(at(true, 2'000), 800);
    CHECK(watcher.Progressed());
    CHECK(watcher.Poll(at(true, 2'250), 1'050).empty());  // progress is not one of Poll's events
    CHECK(!watcher.Progressed());
    watcher.Poll(at(false, 90'000), 1'300);  // a pause with a seek: the position moved
    CHECK(watcher.Progressed());
    watcher.Poll(at(false, 90'000), 5'000);
    CHECK(!watcher.Progressed());
    ampify::PlayerSnapshot none;
    watcher.Poll(none, 5'250);
    CHECK(!watcher.Progressed());  // nothing loaded: no position to follow
}
