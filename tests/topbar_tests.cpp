#include "test.hpp"

#include "../src/core/theme/color.hpp"
#include "../src/core/topbar.hpp"

using namespace ampify;

TEST_CASE(Topbar_FitsOnlyAWideEnoughWindow) {
    CHECK(TopbarFits(1600));
    CHECK(!TopbarFits(kTopbarMinWidth - 1));
    CHECK(TopbarFits(kTopbarMinWidth));
}

TEST_CASE(Topbar_TransportCountsAtTheTopOnlyWhenVisibleAndInTheStrip) {
    CHECK(TransportAtTop(true, 0));                       // Apple's own layout, no theme
    CHECK(TransportAtTop(true, kTopbarHeight - 1));       // a theme that insets the bar a little
    CHECK(!TransportAtTop(true, kTopbarHeight));          // starts under the strip
    CHECK(!TransportAtTop(true, 900));                    // a bottom bar
    CHECK(!TransportAtTop(false, 0));                     // collapsed by a theme
}

TEST_CASE(Topbar_SearchFieldIsCentredWithinBounds) {
    CHECK(TopbarSearchWidth(760) == 388);            // room between Back and the caption buttons
    CHECK(TopbarSearchWidth(2000) == kTopbarSearchMaxWidth);
    CHECK(TopbarSearchWidth(300) == kTopbarSearchMinWidth);
    // The field clears the caption buttons on its right, and the same margin is kept on its left.
    for (double width : {760.0, 900.0, 1600.0}) {
        double left = (width - TopbarSearchWidth(width)) / 2;
        CHECK(left >= kTopbarCaptionWidth);
    }
}

TEST_CASE(Topbar_BackSitsLeftOfTheCentredFieldAndBothClearTheCaptionButtons) {
    for (double width : {kTopbarMinWidth, 900.0, 1324.0, 2560.0}) {
        TopbarLayout layout = LayOutTopbar(width, kTopbarCaptionHeight);
        CHECK(layout.search.x + layout.search.width / 2 == width / 2);
        CHECK(layout.back.x + layout.back.width + kTopbarGap == layout.search.x);
        CHECK(layout.back.x >= 0);
        CHECK(layout.search.x + layout.search.width <= width - kTopbarCaptionWidth);
        CHECK(layout.back.height == kTopbarControlHeight && layout.search.height == kTopbarControlHeight);
    }
}

TEST_CASE(Topbar_ControlsCentreOnTallerCaptionButtonsNotOnTheStrip) {
    for (double caption : {40.0, 48.0}) {
        TopbarLayout layout = LayOutTopbar(1140, caption);
        CHECK(layout.back.y + layout.back.height / 2 == caption / 2);
        CHECK(layout.search.y + layout.search.height / 2 == caption / 2);
    }
}

TEST_CASE(Topbar_ControlsKeepAGapAboveThemUnderAStandardOrShortCaption) {
    for (double caption : {kTopbarCaptionHeight, 24.0}) {
        TopbarLayout layout = LayOutTopbar(1140, caption);
        CHECK(layout.back.y == kTopbarMinTop && layout.search.y == kTopbarMinTop);
        CHECK(layout.search.y + layout.search.height <= kTopbarHeight);
    }
}

// A theme that leaves a taller strip above its panels (Glass) gets the controls centred in it, with room above them
// (seen on screen: the field all but touched the window's top); the standard strip keeps the gap of 4.
TEST_CASE(Topbar_ControlsCentreInAStripTallerThanTheCaptionButtons) {
    TopbarLayout tall = LayOutTopbar(1140, kTopbarCaptionHeight, 48);
    CHECK(tall.search.y == 8 && tall.back.y == 8);
    TopbarLayout standard = LayOutTopbar(1140, kTopbarCaptionHeight, kTopbarHeight);
    CHECK(standard.search.y == kTopbarMinTop);
    TopbarLayout tallCaption = LayOutTopbar(1140, 48, kTopbarHeight);
    CHECK(tallCaption.search.y + tallCaption.search.height / 2 == 24);
    TopbarLayout band = LayOutTopbar(1140, kTopbarCaptionHeight, 120);  // pages under a theme's own band
    CHECK(band.search.y == kTopbarMinTop);
}

TEST_CASE(Topbar_SearchSlotIsAsTallAsApplesSearchBoxSoItsBottomIsNotCut) {
    for (double caption : {kTopbarCaptionHeight, 40.0, 48.0}) {
        TopbarLayout layout = LayOutTopbar(1140, caption);
        CHECK(layout.search.height >= kAppleSearchBoxHeight);
        CHECK(layout.back.height == layout.search.height);
    }
}

TEST_CASE(Topbar_CaptionHeightIsReadInDevicePixelsOrFallsBackToStandard) {
    CHECK(TopbarCaptionHeight(40, 1.25) == 32);                   // Windows' standard caption at 125%
    CHECK(TopbarCaptionHeight(60, 1.25) == 48);                   // a tall caption
    CHECK(TopbarCaptionHeight(0, 1.25) == kTopbarCaptionHeight);  // not extended into the title bar yet
    CHECK(TopbarCaptionHeight(40, 0) == kTopbarCaptionHeight);    // no scale known
}

TEST_CASE(Topbar_PassthroughRoundsOutwardInDevicePixels) {
    TopbarRect rect{10, 0, 100, 48};
    DeviceRect at100 = ToDeviceRect(rect, 1.0);
    CHECK(at100.x == 10 && at100.y == 0 && at100.width == 100 && at100.height == 48);

    DeviceRect at150 = ToDeviceRect(rect, 1.5);
    CHECK(at150.x == 15 && at150.width == 150 && at150.height == 72);

    DeviceRect at125 = ToDeviceRect(rect, 1.25);
    CHECK(at125.x == 12 && at125.width == 126 && at125.height == 60);  // 12.5 .. 137.5 rounds to 12 .. 138
}

TEST_CASE(Topbar_MinimumWidthIsWhatTheStripNeeds) {
    CHECK(kTopbarMinWidth == 612);                       // Back and both caption sides, plus the narrowest field
    CHECK(TopbarSearchWidth(kTopbarMinWidth) == kTopbarSearchMinWidth);
}

TEST_CASE(Topbar_PassthroughRoundsOutwardBelowOneAndAcrossNegativeOrigins) {
    DeviceRect half = ToDeviceRect(TopbarRect{-3, 0, 10, 10}, 0.5);
    CHECK(half.x == -2 && half.width == 6);              // -1.5 .. 3.5 widens to -2 .. 4
    DeviceRect shrunk = ToDeviceRect(TopbarRect{1, 0, 3, 3}, 0.5);
    CHECK(shrunk.x == 0 && shrunk.width == 2);           // 0.5 .. 2 widens to 0 .. 2
    DeviceRect negative = ToDeviceRect(TopbarRect{-10, 0, 20, 48}, 1.0);
    CHECK(negative.x == -10 && negative.width == 20);
}

TEST_CASE(Topbar_OwnRegionsAreSwappedWithoutTouchingApplesOwn) {
    std::vector<DeviceRect> apple{{0, 0, 300, 40}, {1000, 0, 200, 64}};
    std::vector<DeviceRect> before{{500, 4, 40, 32}};
    std::vector<DeviceRect> now{{510, 4, 40, 32}, {558, 4, 400, 32}};
    std::vector<DeviceRect> current = apple;
    current.push_back(before[0]);

    std::vector<DeviceRect> merged = MergeRegions(current, before, now);

    CHECK(merged.size() == 4);
    CHECK(merged[0] == apple[0] && merged[1] == apple[1]);
    CHECK(merged[2] == now[0] && merged[3] == now[1]);
    CHECK(MergeRegions(merged, now, {}) == apple);       // turned off: only Apple's are left
}

TEST_CASE(Topbar_PresenterSetterFoundByItsResolvedTarget) {
    std::vector<StateSetterLook> setters{{true, false, false}, {true, true, true}, {false, std::nullopt, false}};
    CHECK(PresenterSetterIndex(setters) == std::optional<std::size_t>(1));
}

TEST_CASE(Topbar_PresenterSetterFoundByItsCollapseWhenNoTargetResolves) {
    // Apple's ClosedCompact: the presenter collapses, the search button and the hamburger show, the header's height.
    std::vector<StateSetterLook> setters{{true, std::nullopt, true},
                                         {true, std::nullopt, false},
                                         {false, std::nullopt, false},
                                         {true, std::nullopt, false}};
    CHECK(PresenterSetterIndex(setters) == std::optional<std::size_t>(0));
}

TEST_CASE(Topbar_CollapsedReadFromTheNumberOrTheText) {
    // Apple's template boxes a Visibility setter's value as an Int32: Collapsed is 1, Visible 0.
    CHECK(VisibilityCollapses(1, L""));
    CHECK(!VisibilityCollapses(0, L""));
    CHECK(VisibilityCollapses(std::nullopt, L"Collapsed"));
    CHECK(!VisibilityCollapses(std::nullopt, L"Visible"));
    CHECK(!VisibilityCollapses(std::nullopt, L""));
}

TEST_CASE(Topbar_PresenterSetterRefusedWhenAmbiguousOrAbsent) {
    std::vector<StateSetterLook> two{{true, std::nullopt, true}, {true, std::nullopt, true}};
    CHECK(!PresenterSetterIndex(two));
    std::vector<StateSetterLook> elsewhere{{true, false, true}, {false, std::nullopt, true}};
    CHECK(!PresenterSetterIndex(elsewhere));             // a resolved target that is not the presenter never counts
    CHECK(!PresenterSetterIndex({}));
}

TEST_CASE(Topbar_HeaderKeepsItsPlaceWithoutArrowWhileTheArrowIsInTheStrip) {
    ArrowRoom grid;
    CHECK(!grid.Next(false, -26));               // Apple's layout without its arrow: learnt, left alone
    CHECK(grid.Next(true, 5) == std::optional<double>(-26));  // Apple made room for the arrow: held as without it
    CHECK(!grid.Next(true, -26));                // Ampify's own write, seen again: nothing more to do
    CHECK(grid.Restore(true) == std::optional<double>(5));    // given back while the arrow shows: Apple's room
    CHECK(!grid.Next(false, -26));               // the arrow went: Apple's value stands
    CHECK(!grid.Restore(true));                  // nothing of Apple's is held any more
}

TEST_CASE(Topbar_HeaderLeftAsAppleHadItUntilItsLayoutWithoutArrowIsKnown) {
    ArrowRoom grid;
    CHECK(!grid.Next(true, 5));                  // the arrow already showed when the strip took it
    CHECK(!grid.Restore(true));
    CHECK(!grid.Next(false, -26));
    CHECK(grid.Next(true, 6) == std::optional<double>(-26));  // the folded column's room (6) is held the same
    CHECK(!grid.Restore(false));                 // the arrow went meanwhile: Apple has written its own value
}

TEST_CASE(Topbar_HeaderHeldFromTheStartWithAppleMusicsOwnLayoutWithoutArrow) {
    // The strip turned on with a page to go back to: Apple was never seen without its arrow.
    ArrowRoom grid{kAppleHeaderLeft};
    CHECK(grid.Next(true, 5) == std::optional<double>(kAppleHeaderLeft));
    CHECK(grid.Restore(true) == std::optional<double>(5));
    CHECK(!grid.Next(false, -20));               // Apple's own value, once seen, wins over the known one
    CHECK(grid.Next(true, 5) == std::optional<double>(-20));
    ArrowRoom title{kAppleTitleLeft};
    CHECK(title.Next(true, 26) == std::optional<double>(kAppleTitleLeft));
}

namespace {

// What a picture under the strip may show: its brightest, its darkest, Ember's pink and a mid grey.
const theme::Argb kUnders[] = {{0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0, 0, 0}, {0xFF, 0xC8, 0x64, 0x78}, {0xFF, 0x80, 0x80, 0x80}};

theme::Argb Over(theme::Argb under, theme::Argb top) {
    return theme::Blend(under, top, top.a / 255.0);
}

}  // namespace

TEST_CASE(Topbar_SearchPlaceholderKeepsItsContrastOverAnyPicture) {
    // The box's own rest fill in each mode; Fluent's placeholder, Ember's themed one, and the #9F9F9F Apple Music's box
    // drew in the strip under Ember (measured at 100 %).
    struct Mode { bool dark; theme::Argb fill; std::vector<theme::Argb> placeholders; };
    const Mode modes[] = {{true, {0x0F, 0xFF, 0xFF, 0xFF},
                           {{0xC5, 0xFF, 0xFF, 0xFF}, {0xFF, 0xC9, 0xBF, 0xD0}, {0xFF, 0x9F, 0x9F, 0x9F}}},
                          {false, {0xB3, 0xFF, 0xFF, 0xFF}, {{0x9E, 0, 0, 0}}}};
    for (const Mode& mode : modes) {
        for (theme::Argb under : kUnders) {
            theme::Argb box = Over(Over(under, TopbarBackingFor(mode.dark).field), mode.fill);
            for (theme::Argb placeholder : mode.placeholders) CHECK(theme::ContrastRatio(Over(box, placeholder), box) >= 4.5);
        }
    }
}

TEST_CASE(Topbar_CaptionGlyphsKeepTheirContrastOverAnyPicture) {
    for (bool dark : {true, false}) {
        // A one-pixel stroke never reaches its full colour: the close glyph measured #EFEFEF at 100 %, so #E0E0E0 here.
        theme::Argb glyph = dark ? theme::Argb{0xFF, 0xE0, 0xE0, 0xE0} : theme::Argb{0xE4, 0, 0, 0};
        theme::Argb veil = TopbarBackingFor(dark).veil;
        veil.a = static_cast<std::uint8_t>(veil.a * TopbarVeilShare(kTopbarCaptionHeight / 2));
        for (theme::Argb under : kUnders) {
            theme::Argb behind = Over(under, veil);
            CHECK(theme::ContrastRatio(Over(behind, glyph), behind) >= 4.5);
        }
    }
}

TEST_CASE(Topbar_VeilFadesOutWhereThePanelsStart) {
    CHECK(TopbarVeilShare(0) == 1);
    CHECK(TopbarVeilShare(kTopbarCaptionHeight / 2) == 1);    // held over the glyphs' line
    CHECK(TopbarVeilShare(28) > 0 && TopbarVeilShare(28) < 1);  // a soft edge, no band
    CHECK(TopbarVeilShare(kTopbarHeight) == 0);
    CHECK(TopbarVeilShare(kTopbarHeight + 10) == 0);
}
