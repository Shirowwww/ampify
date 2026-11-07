#include "../src/core/sidebar_hold.hpp"
#include "test.hpp"

using std::chrono::milliseconds;
using Clock = std::chrono::steady_clock;

TEST_CASE(SidebarHold_ReleasesWhenTheWindowIsNotInFront) {
    CHECK(!ampify::HoldsForTyping(true, false, milliseconds(0)));
}

TEST_CASE(SidebarHold_ReleasesWhenFocusIsOutsideThePane) {
    CHECK(!ampify::HoldsForTyping(false, true, milliseconds(0)));
}

TEST_CASE(SidebarHold_HoldsForTwoSecondsAfterTyping) {
    CHECK(ampify::HoldsForTyping(true, true, milliseconds(1999)));
    CHECK(!ampify::HoldsForTyping(true, true, milliseconds(2000)));
}

TEST_CASE(SidebarHold_MenuHoldsOnlyWhileTheWindowIsInFront) {
    CHECK(ampify::HoldsForMenu(true, true));
    CHECK(!ampify::HoldsForMenu(false, true));
    CHECK(!ampify::HoldsForMenu(true, false));
}

TEST_CASE(SidebarHold_FirstLookIsNotTyping) {
    ampify::TypingTracker tracker;
    Clock::time_point start = Clock::now();
    tracker.Look(L"existing text", start);
    CHECK(!ampify::HoldsForTyping(true, true, tracker.SinceTyped(start)));
}

TEST_CASE(SidebarHold_ChangedTextCountsAsTyping) {
    ampify::TypingTracker tracker;
    Clock::time_point start = Clock::now();
    tracker.Look(L"abc", start);
    tracker.Look(L"abcd", start + milliseconds(500));
    CHECK(tracker.SinceTyped(start + milliseconds(1000)) == milliseconds(500));
}

TEST_CASE(SidebarHold_SameTextIsNotTyping) {
    ampify::TypingTracker tracker;
    Clock::time_point start = Clock::now();
    tracker.Look(L"abc", start);
    tracker.Look(L"abc", start + milliseconds(500));
    CHECK(!ampify::HoldsForTyping(true, true, tracker.SinceTyped(start + milliseconds(1000))));
}

TEST_CASE(SidebarHold_ForgetStartsOver) {
    ampify::TypingTracker tracker;
    Clock::time_point start = Clock::now();
    tracker.Look(L"abc", start);
    tracker.Look(L"abcd", start);
    tracker.Forget();
    tracker.Look(L"abcde", start);
    CHECK(!ampify::HoldsForTyping(true, true, tracker.SinceTyped(start)));
}

TEST_CASE(SidebarHold_PinButtonOffersToPinWhileThePaneFolds) {
    ampify::PinFace face = ampify::PinButtonFace(false);
    CHECK(std::wstring_view(face.glyph) == L"\xE718");
    CHECK(std::string_view(face.label) == "sidebar.pin");
}

TEST_CASE(SidebarHold_PinButtonOffersToUnpinWhilePinned) {
    ampify::PinFace face = ampify::PinButtonFace(true);
    CHECK(std::wstring_view(face.glyph) == L"\xE77A");
    CHECK(std::string_view(face.label) == "sidebar.unpin");
}

TEST_CASE(SidebarHold_HeaderButtonStandsJustLeftOfMore) {
    CHECK(ampify::LeftOfMoreMargin(4, 32, 2) == 38);
    CHECK(ampify::LeftOfMoreMargin(48, 32, 2) == 82);
}

TEST_CASE(SidebarHold_HeaderButtonMakesRoomForThePin) {
    CHECK(ampify::LeftOfMoreMargin(4, 32, 2, 30) == 68);
}

TEST_CASE(SidebarHold_PinPushesThePagesOnlyWhereAppleOpensItsSidebar) {
    CHECK(ampify::PinPushesPages(1325));
    CHECK(ampify::PinPushesPages(1920));
    CHECK(!ampify::PinPushesPages(1324));
    CHECK(!ampify::PinPushesPages(1094));
}

TEST_CASE(SidebarHold_KeysHoldThePaneWhileTheFocusStaysOnIt) {
    CHECK(ampify::HoldsForKeyboard(true, true, true));
    CHECK(!ampify::HoldsForKeyboard(true, true, false));
    CHECK(!ampify::HoldsForKeyboard(false, true, true));
}

TEST_CASE(SidebarHold_FocusAloneDoesNotHoldAPaneOpenedByThePointer) {
    CHECK(!ampify::HoldsForKeyboard(true, false, true));
}
