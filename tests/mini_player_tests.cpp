// The mini player's portable half: what it shows, where its parts sit, where its window goes, its colours from the
// theme, and the feed between the kernel's threads and the window's interface thread.

#include <map>
#include <string>
#include <vector>

#include "../src/core/host.hpp"
#include "../src/core/mini_player.hpp"
#include "../src/core/ui_model.hpp"
#include "test.hpp"

namespace {

using ampify::PlayerCommand;
using ampify::PlayerSnapshot;
using ampify::TrackInfo;
namespace mini = ampify::mini;

PlayerSnapshot Playing(const std::string& title, const std::string& artist, int64_t positionMs, int64_t durationMs) {
    PlayerSnapshot snapshot;
    snapshot.playing = true;
    snapshot.positionMs = positionMs;
    snapshot.durationMs = durationMs;
    snapshot.source = "apple";
    TrackInfo track;
    track.id = "track-1";
    track.title = title;
    track.artist = artist;
    snapshot.track = track;
    return snapshot;
}

bool Inside(const mini::Rect& rect, int width, int height) {
    return rect.x >= 0 && rect.y >= 0 && rect.width >= 0 && rect.height >= 0 && rect.x + rect.width <= width &&
           rect.y + rect.height <= height;
}

bool Overlaps(const mini::Rect& a, const mini::Rect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

}  // namespace

TEST_CASE(MiniPlayer_FormatsTimeAsMinutesAndSeconds) {
    CHECK(mini::FormatTime(0) == "0:00");
    CHECK(mini::FormatTime(65000) == "1:05");
    CHECK(mini::FormatTime(59999) == "0:59");
    CHECK(mini::FormatTime(3 * 60 * 1000 + 42 * 1000) == "3:42");
}

TEST_CASE(MiniPlayer_FormatsTimeFromAnHourWithHours) {
    CHECK(mini::FormatTime(3600 * 1000) == "1:00:00");
    CHECK(mini::FormatTime((3600 + 61) * 1000) == "1:01:01");
}

TEST_CASE(MiniPlayer_FormatsNegativeTimeAsZero) {
    CHECK(mini::FormatTime(-500) == "0:00");
}

TEST_CASE(MiniPlayer_InfoReadsTheTrackTimesAndProgress) {
    mini::Info info = mini::InfoFrom(Playing("Song", "Band", 45000, 180000));
    CHECK(info.hasTrack);
    CHECK(info.trackId == "track-1");
    CHECK(info.title == "Song");
    CHECK(info.artist == "Band");
    CHECK(info.playing);
    CHECK(info.elapsed == "0:45");
    CHECK(info.total == "3:00");
    CHECK_NEAR(info.progress, 0.25, 1e-9);
}

TEST_CASE(MiniPlayer_InfoIsEmptyWithoutATrack) {
    PlayerSnapshot snapshot;
    mini::Info info = mini::InfoFrom(snapshot);
    CHECK(!info.hasTrack);
    CHECK(info.title.empty());
    CHECK(!info.playing);
    CHECK(info.progress == 0);
}

TEST_CASE(MiniPlayer_ProgressStaysBetweenZeroAndOne) {
    CHECK(mini::InfoFrom(Playing("S", "A", 10000, 0)).progress == 0);
    CHECK(mini::InfoFrom(Playing("S", "A", 300000, 100000)).progress == 1);
    CHECK(mini::InfoFrom(Playing("S", "A", -5, 100000)).progress == 0);
    CHECK(mini::InfoFrom(Playing("S", "A", 10000, 0)).total.empty());
}

TEST_CASE(MiniPlayer_LayoutKeepsEveryPartInsideTheWindow) {
    const int sizes[][2] = {{280, 120}, {340, 140}, {420, 200}, {560, 320}};
    for (const auto& size : sizes) {
        mini::Layout layout = mini::LayoutFor(size[0], size[1]);
        for (const mini::Rect* rect : {&layout.artwork, &layout.title, &layout.artist, &layout.progress,
                                       &layout.previous, &layout.playPause, &layout.next, &layout.back}) {
            CHECK(Inside(*rect, size[0], size[1]));
        }
    }
}

TEST_CASE(MiniPlayer_LayoutKeepsArtworkApartFromTheText) {
    mini::Layout layout = mini::LayoutFor(340, 140);
    CHECK(!Overlaps(layout.artwork, layout.title));
    CHECK(!Overlaps(layout.artwork, layout.artist));
    CHECK(!Overlaps(layout.artwork, layout.progress));
    CHECK(!Overlaps(layout.title, layout.artist));
    CHECK(!Overlaps(layout.back, layout.title));
    CHECK(!Overlaps(layout.previous, layout.playPause));
    CHECK(!Overlaps(layout.playPause, layout.next));
}

TEST_CASE(MiniPlayer_LayoutSquaresTheArtwork) {
    mini::Layout layout = mini::LayoutFor(340, 140);
    CHECK(layout.artwork.width == layout.artwork.height);
    CHECK(layout.artwork.width > 60);
}

TEST_CASE(MiniPlayer_LayoutPutsTheBackButtonInTheTopRightCorner) {
    mini::Layout layout = mini::LayoutFor(340, 140);
    CHECK(layout.back.x + layout.back.width > 340 - 24);
    CHECK(layout.back.y < 24);
}

TEST_CASE(MiniPlayer_LayoutPutsTheTransportRowAtTheBottom) {
    mini::Layout layout = mini::LayoutFor(340, 140);
    CHECK(layout.previous.y == layout.playPause.y);
    CHECK(layout.playPause.y == layout.next.y);
    CHECK(layout.previous.y + layout.previous.height > 140 - 24);
    CHECK(layout.previous.x < layout.playPause.x);
    CHECK(layout.playPause.x < layout.next.x);
    CHECK(layout.progress.y + layout.progress.height <= layout.previous.y);
}

TEST_CASE(MiniPlayer_LayoutClampsTheSizeToTheLimits) {
    mini::Layout small = mini::LayoutFor(10, 10);
    CHECK(small.artwork.height == mini::LayoutFor(mini::kMinWidth, mini::kMinHeight).artwork.height);
    mini::Layout big = mini::LayoutFor(5000, 5000);
    CHECK(big.artwork.width == mini::LayoutFor(mini::kMaxWidth, mini::kMaxHeight).artwork.width);
    CHECK(Inside(big.next, mini::kMaxWidth, mini::kMaxHeight));
}

TEST_CASE(MiniPlayer_ParsesAndFormatsThePlacement) {
    mini::Placement placed;
    placed.x = -1200;
    placed.y = 40;
    placed.width = 360;
    placed.height = 150;
    std::string text = mini::FormatPlacement(placed);
    CHECK(text == "-1200,40,360,150");
    auto back = mini::ParsePlacement(text);
    CHECK(back.has_value());
    CHECK(back->x == -1200 && back->y == 40 && back->width == 360 && back->height == 150);
}

TEST_CASE(MiniPlayer_RejectsMalformedPlacements) {
    CHECK(!mini::ParsePlacement("").has_value());
    CHECK(!mini::ParsePlacement("10,20,300").has_value());
    CHECK(!mini::ParsePlacement("10,20,300,150,5").has_value());
    CHECK(!mini::ParsePlacement("a,20,300,150").has_value());
    CHECK(!mini::ParsePlacement("10,20,,150").has_value());
}

TEST_CASE(MiniPlayer_FitMovesAWindowThatIsOffScreenBackInside) {
    mini::Rect area{0, 0, 1920, 1040};
    mini::Placement placed;
    placed.x = 2500;
    placed.y = -300;
    mini::Placement fitted = mini::FitInto(placed, area);
    CHECK(fitted.x + fitted.width <= 1920);
    CHECK(fitted.y >= 0);
    CHECK(fitted.x >= 0);
}

TEST_CASE(MiniPlayer_FitClampsTheSizeToTheArea) {
    mini::Rect area{0, 0, 300, 200};
    mini::Placement placed;
    placed.width = 900;
    placed.height = 900;
    mini::Placement fitted = mini::FitInto(placed, area);
    CHECK(fitted.width <= 300);
    CHECK(fitted.height <= 200);
    CHECK(fitted.x == 0 && fitted.y == 0);
}

TEST_CASE(MiniPlayer_FitKeepsASavedPlacementThatAlreadyFits) {
    mini::Rect area{0, 0, 1920, 1040};
    mini::Placement placed;
    placed.x = 100;
    placed.y = 200;
    placed.width = 380;
    placed.height = 160;
    mini::Placement fitted = mini::FitInto(placed, area);
    CHECK(fitted.x == 100 && fitted.y == 200 && fitted.width == 380 && fitted.height == 160);
}

TEST_CASE(MiniPlayer_PlacementScalesWithTheScreen) {
    mini::Rect area{0, 0, 3840, 2080};
    mini::Placement placed = mini::DefaultPlacement(area, 2.0);
    CHECK(placed.width == mini::kDefaultWidth * 2);
    CHECK(placed.height == mini::kDefaultHeight * 2);
    CHECK(placed.x + placed.width <= 3840);
    CHECK(placed.y + placed.height <= 2080);

    mini::Placement tiny;
    tiny.width = 100;
    tiny.height = 100;
    mini::Placement fitted = mini::FitInto(tiny, area, 2.0);
    CHECK(fitted.width == mini::kMinWidth * 2);
    CHECK(fitted.height == mini::kMinHeight * 2);
}

TEST_CASE(MiniPlayer_DefaultPlacementSitsInTheBottomRightCorner) {
    mini::Rect area{0, 0, 1920, 1040};
    mini::Placement placed = mini::DefaultPlacement(area);
    CHECK(placed.width == mini::kDefaultWidth);
    CHECK(placed.height == mini::kDefaultHeight);
    CHECK(placed.x + placed.width <= 1920);
    CHECK(placed.y + placed.height <= 1040);
    CHECK(placed.x > 1920 / 2);
    CHECK(placed.y > 1040 / 2);
}

TEST_CASE(MiniPlayer_ColoursComeFromTheThemeVariables) {
    std::map<std::string, std::string> variables = {{"mini-background", "#101820"}, {"mini-accent", "#00AAFF"}};
    mini::Colours colours = mini::ColoursFrom(variables);
    CHECK(colours.background == "#FF101820");
    CHECK(colours.accent == "#FF00AAFF");
}

TEST_CASE(MiniPlayer_ColoursFallBackWhenTheThemeSaysNothing) {
    mini::Colours colours = mini::ColoursFrom({});
    CHECK(colours.background == "#FF1C1C1E");
    CHECK(colours.foreground == "#FFFFFFFF");
    CHECK(colours.muted == "#FFA1A1A6");
    CHECK(colours.accent == "#FFFA2D48");
}

TEST_CASE(MiniPlayer_ColoursIgnoreWhatIsNotAColour) {
    std::map<std::string, std::string> variables = {{"mini-foreground", "var(--x)"}, {"mini-muted", "#12"}};
    mini::Colours colours = mini::ColoursFrom(variables);
    CHECK(colours.foreground == "#FFFFFFFF");
    CHECK(colours.muted == "#FFA1A1A6");
}

TEST_CASE(MiniPlayer_FeedGivesTheLatestInfoAndColours) {
    mini::PublishInfo(mini::InfoFrom(Playing("First", "A", 0, 1000)));
    mini::PublishInfo(mini::InfoFrom(Playing("Second", "B", 500, 1000)));
    CHECK(mini::CurrentInfo().title == "Second");
    CHECK(mini::CurrentInfo().progress == 0.5);

    mini::Colours colours;
    colours.accent = "#FF112233";
    mini::PublishColours(colours);
    CHECK(mini::CurrentColours().accent == "#FF112233");
}

TEST_CASE(MiniPlayer_FeedKeepsTheArtworkOfTheLastPublish) {
    CHECK(!mini::CurrentArtwork().has_value());
    mini::PublishArtwork({"track-9", "image/jpeg", "bytes"});
    auto artwork = mini::CurrentArtwork();
    CHECK(artwork.has_value());
    CHECK(artwork->trackId == "track-9");
    CHECK(artwork->contentType == "image/jpeg");
    CHECK(artwork->bytes == "bytes");
}

TEST_CASE(MiniPlayer_ArtworkOfTheSameSongInTheSessionIsAccepted) {
    TrackInfo track;
    track.title = "Halo";
    track.artist = "Beyonce";

    CHECK(mini::ArtworkIsFor(track, "Halo", "Beyonce"));
    CHECK(mini::ArtworkIsFor(track, "Halo", "Beyonce \xE2\x80\x94 I Am... Sasha Fierce"));
}

TEST_CASE(MiniPlayer_ArtworkOfThePreviousSongInTheSessionIsRefused) {
    TrackInfo track;
    track.title = "Halo";
    track.artist = "Beyonce";

    CHECK(!mini::ArtworkIsFor(track, "Crazy in Love", "Beyonce"));
    CHECK(!mini::ArtworkIsFor(track, "Halo", "Someone Else \xE2\x80\x94 Covers"));
}

TEST_CASE(MiniPlayer_ArtworkWithoutSessionTitleIsAcceptedAsBefore) {
    TrackInfo track;
    track.title = "Halo";
    track.artist = "Beyonce";

    CHECK(mini::ArtworkIsFor(track, "", ""));
    CHECK(mini::ArtworkIsFor(track, "Halo", ""));
}

TEST_CASE(MiniPlayer_CommandsAreTakenOnceInOrder) {
    mini::RequestCommand(PlayerCommand::Next);
    mini::RequestCommand(PlayerCommand::Pause);
    auto taken = mini::TakeCommands();
    CHECK(taken.size() == 2);
    CHECK(taken.size() == 2 && taken[0] == PlayerCommand::Next && taken[1] == PlayerCommand::Pause);
    CHECK(mini::TakeCommands().empty());
}

TEST_CASE(MiniPlayer_OpenFlagIsSetByItsWindow) {
    CHECK(!mini::WindowOpen());
    mini::SetWindowOpen(true);
    CHECK(mini::WindowOpen());
    mini::SetWindowOpen(false);
    CHECK(!mini::WindowOpen());
}

TEST_CASE(MiniPlayer_WindowRequestsReachTheInstalledHandler) {
    CHECK(!mini::RequestWindow(mini::WindowAction::Open));
    int seen = -1;
    mini::InstallWindow([&seen](mini::WindowAction action) { seen = static_cast<int>(action); });
    CHECK(mini::RequestWindow(mini::WindowAction::Close));
    CHECK(seen == static_cast<int>(mini::WindowAction::Close));
    mini::InstallWindow(nullptr);
    CHECK(!mini::RequestWindow(mini::WindowAction::Toggle));
}

TEST_CASE(MiniPlayer_SidebarEntryIsAnAmpifyControlOfTheWindowElement) {
    ampify::UiModel model;
    mini::AddSidebarEntry(model, "Mini player");
    std::vector<ampify::UiControl> controls = model.Controls();
    CHECK(controls.size() == 1);
    CHECK(!controls.empty() && controls[0].placement == "sidebar");
    CHECK(!controls.empty() && controls[0].elementId == mini::kElementId);
    CHECK(!controls.empty() && controls[0].label == "Mini player");
    CHECK(!controls.empty() && controls[0].clickable);
}
