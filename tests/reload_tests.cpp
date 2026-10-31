// Requests in reload.txt: which lines count, and when a change is reported.

#include <string>
#include <vector>

#include "../core/reload.hpp"
#include "test.hpp"

TEST_CASE(Reload_ParsesRestartLinesOnly) {
    std::vector<std::string> ids =
        ampify::ParseRestartRequests("1700000000000\nrestart demo\r\nrestart  other \nnoise\nrestart demo\nrestart \n");
    CHECK(ids.size() == 2);
    if (ids.size() == 2) {
        CHECK(ids[0] == "demo");
        CHECK(ids[1] == "other");
    }
}

TEST_CASE(Reload_FirstTextIsABaseline) {
    ampify::ReloadWatcher watcher;
    CHECK(watcher.Changed("1\nrestart demo\n").empty());
    CHECK(watcher.Changed("1\nrestart demo\n").empty());
    std::vector<std::string> changed = watcher.Changed("2\nrestart demo\n");
    CHECK(changed.size() == 1);
    if (changed.size() == 1) CHECK(changed[0] == "demo");
}

TEST_CASE(Reload_ParsesEveryVerbAndRefusesBadTargets) {
    std::vector<ampify::ReloadRequest> requests = ampify::ParseReloadRequests(
        "1\nstart demo\nstop app:demo\nrestart demo\nrescan\nstart ../x\nstart app:\nstart Demo\nstop a b\nstart demo\n");
    using Action = ampify::ReloadRequest::Action;
    CHECK(requests.size() == 4);
    if (requests.size() == 4) {
        CHECK(requests[0] == (ampify::ReloadRequest{Action::Start, "demo"}));
        CHECK(requests[1] == (ampify::ReloadRequest{Action::Stop, "app:demo"}));
        CHECK(requests[2] == (ampify::ReloadRequest{Action::Restart, "demo"}));
        CHECK(requests[3] == (ampify::ReloadRequest{Action::Rescan, ""}));
    }
    CHECK(ampify::IsReloadTarget("app:demo"));
    CHECK(!ampify::IsReloadTarget("theme:demo"));
    CHECK(!ampify::IsReloadTarget("-demo"));
}

TEST_CASE(Reload_AppendedLinesCountOnce) {
    ampify::ReloadWatcher watcher;
    CHECK(watcher.Requests("1\nstart old\n").empty());  // baseline
    std::vector<ampify::ReloadRequest> added = watcher.Requests("1\nstart old\nstart new\n");
    CHECK(added.size() == 1);
    if (added.size() == 1) CHECK(added[0].target == "new");

    // A line read half-written waits for its newline.
    CHECK(watcher.Requests("1\nstart old\nstart new\nstop ne").empty());
    added = watcher.Requests("1\nstart old\nstart new\nstop new\n");
    CHECK(added.size() == 1);
    if (added.size() == 1) CHECK(added[0].action == ampify::ReloadRequest::Action::Stop);

    // A rewritten file (ampify starts it again once it grew too large) counts as a whole.
    added = watcher.Requests("2\nrescan\n");
    CHECK(added.size() == 1);
    if (added.size() == 1) CHECK(added[0].action == ampify::ReloadRequest::Action::Rescan);
}

TEST_CASE(Reload_AFileCreatedAfterTheStartIsRead) {
    ampify::ReloadWatcher watcher;
    CHECK(watcher.Requests("").empty());
    CHECK(watcher.Requests("start demo\n").size() == 1);
}
