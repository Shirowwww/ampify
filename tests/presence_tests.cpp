#include <memory>
#include <vector>

#include "../src/core/presence.hpp"
#include "test.hpp"

using ampify::Presence;
using ampify::PresenceListener;

TEST_CASE(Presence_StartsSeenSoNothingPausesBeforeTheWindowIsKnown) {
    Presence presence;
    CHECK(presence.Seen());
}

TEST_CASE(Presence_ListenersHearEachChangeOnce) {
    Presence presence;
    std::vector<bool> heard;
    presence.Listen([&](bool seen) { heard.push_back(seen); });
    CHECK(presence.Set(false));
    CHECK(!presence.Set(false));  // the same state again is no change
    CHECK(presence.Set(true));
    CHECK(heard.size() == static_cast<std::size_t>(2));
    CHECK(!heard[0]);
    CHECK(heard[1]);
    CHECK(presence.Seen());
}

TEST_CASE(Presence_AForgottenListenerHearsNothing) {
    Presence presence;
    int calls = 0;
    int id = presence.Listen([&](bool) { calls++; });
    presence.Forget(id);
    presence.Set(false);
    CHECK(calls == 0);
}

TEST_CASE(Presence_AListenerForgottenByAnEarlierOneIsNotCalled) {
    Presence presence;
    auto later = std::make_unique<int>(0);
    int laterId = 0;
    presence.Listen([&](bool) {
        presence.Forget(laterId);
        later.reset();  // its owner is gone: calling it would touch freed memory
    });
    laterId = presence.Listen([&](bool) { (*later)++; });
    presence.Set(false);
    CHECK(!later);
}

TEST_CASE(Presence_AListenerMayForgetItselfWhileCalled) {
    Presence presence;
    int calls = 0;
    int id = 0;
    id = presence.Listen([&](bool) {
        calls++;
        presence.Forget(id);
    });
    presence.Set(false);
    presence.Set(true);
    CHECK(calls == 1);
}

TEST_CASE(Presence_AGameOverTheWholeWindowHidesIt) {
    const ampify::ScreenBox screen{0, 0, 2560, 1440};
    const ampify::ScreenBox apple{65, 145, 2429, 1252};
    CHECK(ampify::Covers(screen, apple));
}

TEST_CASE(Presence_AWindowThatLeavesAPartUncoveredDoesNotHideIt) {
    const ampify::ScreenBox apple{65, 145, 2429, 1252};
    CHECK(!ampify::Covers({100, 0, 2560, 1440}, apple));   // a strip on the left still shows
    CHECK(!ampify::Covers({0, 0, 2560, 1200}, apple));     // and the bottom
    CHECK(!ampify::Covers({0, 0, 2560, 1440}, {0, 0, 0, 0}));  // an empty box is no window to hide
}

TEST_CASE(Presence_ScopedListenerForgetsWhenDestroyed) {
    Presence presence;
    int calls = 0;
    {
        PresenceListener listener(presence, [&](bool) { calls++; });
        presence.Set(false);
    }
    presence.Set(true);
    CHECK(calls == 1);
}
