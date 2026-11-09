#include "../src/core/song_room.hpp"
#include "test.hpp"

using ampify::Edges;
using ampify::SameEdges;
using ampify::SongMargin;

namespace {

// Left, top, right, bottom, as XAML writes a Thickness.
Edges Margin(double left, double top, double right, double bottom) {
    Edges edges;
    edges.left = left;
    edges.top = top;
    edges.right = right;
    edges.bottom = bottom;
    return edges;
}

const Edges kApple = Margin(0, 0, 0, 0);
const Edges kThemeMargin = Margin(4, 0, 4, 8);
const Edges kFitted = Margin(0, 0, 406.4, 0);

}  // namespace

TEST_CASE(SongRoom_FirstFitStartsFromTheLayoutsMargin) {
    SongMargin margin;
    CHECK(SameEdges(margin.Base(kApple), kApple));
}

TEST_CASE(SongRoom_ItsOwnMarginNeverBecomesTheBase) {
    SongMargin margin;
    margin.Base(kApple);
    margin.Wrote(kFitted);
    CHECK(SameEdges(margin.Base(kFitted), kApple));
}

TEST_CASE(SongRoom_AMarginSomeoneElseWroteBecomesTheBase) {
    SongMargin margin;
    margin.Base(kApple);
    margin.Wrote(Margin(0, 0, 300, 0));
    CHECK(SameEdges(margin.Base(kThemeMargin), kThemeMargin));
}

TEST_CASE(SongRoom_ReleasePutsTheLayoutsMarginBackOverItsOwn) {
    SongMargin margin;
    margin.Base(kApple);
    margin.Wrote(kFitted);
    std::optional<Edges> back = margin.Release(kFitted);
    CHECK(back.has_value());
    CHECK(back && SameEdges(*back, kApple));
}

TEST_CASE(SongRoom_ReleaseLeavesAThemesOwnMargin) {
    SongMargin margin;
    margin.Base(kApple);
    margin.Wrote(kFitted);
    CHECK(!margin.Release(kThemeMargin).has_value());
}

TEST_CASE(SongRoom_NothingToReleaseBeforeAnyFit) {
    SongMargin margin;
    CHECK(!margin.Release(kApple).has_value());
}

// The theme engine keeps the margin it found when a theme set its own, and puts it back when the theme goes. Found
// while Ampify fitted the song, it is Ampify's margin: taken for Apple's, it stayed under every later fit, and the
// display shrank to its artwork until Apple Music restarted.
TEST_CASE(SongRoom_AnOwnMarginTheEnginePutsBackIsStillRecognised) {
    SongMargin margin;
    margin.Base(kApple);  // a floating-panels theme: Ampify fits the song
    margin.Wrote(kFitted);
    CHECK(!margin.Release(kThemeMargin).has_value());  // a bottom-bar theme: its own margin stays
    CHECK(SameEdges(margin.Base(kFitted), kApple));    // it goes, the engine puts back what it found: Apple's is kept
    std::optional<Edges> back = margin.Release(kFitted);  // no theme: Apple's margin comes back
    CHECK(back && SameEdges(*back, kApple));
}

TEST_CASE(SongRoom_AnOwnMarginFromAnEarlierFitIsStillRecognised) {
    SongMargin margin;
    margin.Base(kApple);
    margin.Wrote(kFitted);
    margin.Wrote(Margin(0, 0, 395, 0));  // a later fit, before the engine puts the first one back
    CHECK(SameEdges(margin.Base(kFitted), kApple));
    std::optional<Edges> back = margin.Release(kFitted);
    CHECK(back && SameEdges(*back, kApple));
}