#include "../src/core/caption_clearance.hpp"
#include "test.hpp"

using ampify::TogglesDrop;

TEST_CASE(CaptionClearance_AppleRowClearOfTheButtonsStaysInPlace) {
    // Apple's four toggles alone, centred in their column of a 1920-pixel window.
    CHECK(TogglesDrop(1712, 16, 1920) == 0);
}

TEST_CASE(CaptionClearance_ARowUnderTheButtonsGoesLower) {
    // Seen on screen: with Ampify's buttons the row ended at 1830, under the minimize button.
    CHECK(TogglesDrop(1830, 16, 1920) == ampify::kTogglesDrop);
}

TEST_CASE(CaptionClearance_ARowBelowTheirStripStaysInPlace) {
    // A bar at the bottom of the window: its toggles are far below the caption buttons.
    CHECK(TogglesDrop(1900, 980, 1920) == 0);
}

TEST_CASE(CaptionClearance_ARowJustShortOfTheClearanceStaysInPlace) {
    CHECK(TogglesDrop(1920 - 138 - 8, 16, 1920) == 0);
    CHECK(TogglesDrop(1920 - 138 - 7, 16, 1920) == ampify::kTogglesDrop);
}
