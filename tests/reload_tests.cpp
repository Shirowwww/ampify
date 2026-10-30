// Requests in reload.txt: which lines restart an element, and when a change is reported.

#include <string>
#include <vector>

#include "../core/reload.hpp"
#include "test.hpp"

TEST_CASE(Reload_ParsesRestartLinesOnly) {
    std::vector<std::string> ids =
        amk::ParseRestartRequests("1700000000000\nrestart demo\r\nrestart  other \nnoise\nrestart demo\nrestart \n");
    CHECK(ids.size() == 2);
    if (ids.size() == 2) {
        CHECK(ids[0] == "demo");
        CHECK(ids[1] == "other");
    }
}

TEST_CASE(Reload_FirstTextIsABaseline) {
    amk::ReloadWatcher watcher;
    CHECK(watcher.Changed("1\nrestart demo\n").empty());
    CHECK(watcher.Changed("1\nrestart demo\n").empty());
    std::vector<std::string> changed = watcher.Changed("2\nrestart demo\n");
    CHECK(changed.size() == 1);
    if (changed.size() == 1) CHECK(changed[0] == "demo");
}
