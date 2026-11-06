// The offline bench's contrast check: the colour under a text found up the tree, and what it cannot judge when the
// theme turns Apple Music to the other mode than the dump's (src/core/theme/replay_contrast.hpp).

#include <string>

#include "../src/cli/theme_replay.hpp"
#include "../src/core/json.hpp"
#include "../src/core/theme/replay.hpp"
#include "../src/core/theme/replay_contrast.hpp"
#include "replay_support.hpp"
#include "test.hpp"

namespace th = ampify::theme;
namespace fs = std::filesystem;
using namespace replay_support;

namespace {

// A light page: an opaque page background, a band whose brush the inspector could not read, and a title on it.
std::string LightPage(const std::string& pageBackground) {
    return R"json({"version":2,"theme":"","window":{"width":400,"height":200,"sidebarFolded":false},
  "resources":[{"key":"SolidBackgroundFillColorBaseBrush","value":"#FFF3F3F3"}],
  "nodes":[
   {"index":0,"id":"1","type":"Grid","name":"Page","parent":-1,"depth":0,"panel":true,"box":[0,0,400,200],
    "properties":{"Background":")json" + pageBackground + R"json(","Margin":"0,0,0,0","Opacity":"1","Visibility":"Visible"}},
   {"index":1,"id":"2","type":"Grid","name":"Band","parent":0,"depth":1,"panel":true,"box":[0,0,400,40],
    "properties":{"Background":"present","Margin":"0,0,0,0","Opacity":"1","Visibility":"Visible"}},
   {"index":2,"id":"3","type":"TextBlock","name":"Title","parent":1,"depth":2,"box":[10,10,200,20],
    "properties":{"Foreground":"#E4000000","FontSize":"14","Margin":"0,0,0,0","Opacity":"1","Visibility":"Visible"}},
   {"index":3,"id":"4","type":"TextBlock","name":"Note","parent":1,"depth":2,"box":[10,22,200,14],
    "properties":{"Foreground":"#E4000000","FontSize":"12","Margin":"0,0,0,0","Opacity":"0.5","Visibility":"Visible"}}]})json";
}

std::optional<ampify::Json> Replayed(const fs::path& dir, const std::string& tree, const std::string& css,
                                     const std::string& colors = {}) {
    WriteFile(dir / "tree.json", tree);
    fs::path theme = ThemeFolder(dir, "t", css);
    if (!colors.empty()) WriteFile(theme / "colors.ini", colors);
    th::ReplayRequest request;
    request.tree = dir / "tree.json";
    request.out = dir / "out";
    request.theme = theme;
    std::string problem;
    std::optional<th::ReplayOutcome> outcome = th::Replay(request, &problem);
    if (!outcome) {
        ampify::test::ReportFailure(__FILE__, __LINE__, problem);
        return std::nullopt;
    }
    return ampify::Json::Parse(outcome->report);
}

const ampify::Json* TextNamed(const ampify::Json& report, const std::string& element) {
    for (const ampify::Json& item : Items(report, "texts")) {
        if (Text(item.Find("element")) == element) return &item;
    }
    return nullptr;
}

}  // namespace

TEST_CASE(ReplayContrast_JudgesATextOnTheNearestKnownOpaqueColourUpTheTree) {
    fs::path dir = Scratch("contrast-walk");
    // Pale grey on Apple's light page, through a band whose brush reads "present".
    std::optional<ampify::Json> pale = Replayed(dir, LightPage("#FFF3F3F3"), "TextBlock#Title { color: #FFC8C8C8; }\n");
    CHECK(pale.has_value());
    if (!pale) return;
    const ampify::Json* title = TextNamed(*pale, "TextBlock#Title");
    CHECK(title != nullptr);
    if (!title) return;
    CHECK(Text(title->Find("verdict")) == "low");
    CHECK(Text(title->Find("backdropElement")) == "Grid#Page" && Text(title->Find("under")) == "#F3F3F3");
    CHECK(NumberOf(title->Find("passedOver")) == 1 && NumberOf(title->Find("needed")) == 4.5);
    CHECK(NumberOf(title->Find("ratio")) > 1 && NumberOf(title->Find("ratio")) < 2);
    CHECK(NumberOf(Member(*pale, "contrast")->Find("low")) == 1);
    CHECK(Text(Member(*pale, "contrast")->Find("recordedMode")) == "light");
    // The note keeps Apple's colours on both sides: Apple's design, not listed.
    CHECK(TextNamed(*pale, "TextBlock#Note") == nullptr);

    // Black reads; at half opacity it is laid over the page at half strength (#7A7A7A), which does not at 12 px.
    std::optional<ampify::Json> dark = Replayed(dir, LightPage("#FFF3F3F3"),
                                                "TextBlock#Title, TextBlock#Note { color: #FF000000; }\n");
    CHECK(dark && TextNamed(*dark, "TextBlock#Title") == nullptr);
    CHECK(dark && NumberOf(Member(*dark, "contrast")->Find("fine")) == 1);
    const ampify::Json* note = dark ? TextNamed(*dark, "TextBlock#Note") : nullptr;
    CHECK(note && Text(note->Find("verdict")) == "low" && Text(note->Find("text")) == "#7A7A7A");

    // With no known opaque colour up the tree, the verdict is unknown, never a guess.
    std::optional<ampify::Json> unknown = Replayed(dir, LightPage("present"), "TextBlock#Title { color: #FFC8C8C8; }\n");
    const ampify::Json* lost = unknown ? TextNamed(*unknown, "TextBlock#Title") : nullptr;
    CHECK(lost && Text(lost->Find("verdict")) == "unknown" && !lost->Find("ratio"));
    CHECK(lost && NumberOf(lost->Find("passedOver")) == 2);
}

TEST_CASE(ReplayContrast_DoesNotJudgeApplesColoursOfTheOtherMode) {
    fs::path dir = Scratch("contrast-mode");
    // A dark theme on a dump recorded light: the page's light grey is Apple's light mode, which Apple Music would
    // replace; the title's colour is the theme's.
    std::optional<ampify::Json> report = Replayed(dir, LightPage("#FFF3F3F3"), "TextBlock#Title { color: #FFEEEEEE; }\n",
                                                  "[Night]\nSolidBackgroundFillColorBaseBrush = 202020\n");
    CHECK(report.has_value());
    if (!report) return;
    const ampify::Json* title = TextNamed(*report, "TextBlock#Title");
    CHECK(title && Text(title->Find("verdict")) == "not judged");
    CHECK(Text(Member(*report, "contrast")->Find("appMode")) == "dark");
    CHECK(NumberOf(Member(*report, "contrast")->Find("notJudged")) == 1);
    bool warned = false;
    for (const ampify::Json& warning : Items(*report, "warnings")) {
        warned = warned || Text(&warning).find("recorded in light mode") != std::string::npos;
    }
    CHECK(warned);

    // The same colours when the theme also writes the page: everything is the theme's, so it is judged.
    std::optional<ampify::Json> owned = Replayed(dir, LightPage("#FFF3F3F3"),
                                                 "Grid#Page { background: #FF181818; }\n"
                                                 "TextBlock#Title { color: #FFEEEEEE; }\n",
                                                 "[Night]\nSolidBackgroundFillColorBaseBrush = 202020\n");
    CHECK(owned && TextNamed(*owned, "TextBlock#Title") == nullptr);
    CHECK(owned && NumberOf(Member(*owned, "contrast")->Find("fine")) >= 1);

    // The backend's own view: the app's mode, and color-scheme on an element over it.
    std::unique_ptr<th::ReplayBackend> tree = th::ReplayBackend::Load(LightPage("#FFF3F3F3"));
    CHECK(tree && tree->RecordedDark() == false && !tree->ModeDiffers(2));
    tree->SetAppMode(true);
    CHECK(tree->ModeDiffers(2) && tree->ModeOf(2) == true);
    tree->BeginPass(1);
    CHECK(tree->Write(th::ReplayBackend::IdOf(1), "RequestedTheme", {th::ValueKind::Keyword, "Light"}));
    CHECK(!tree->ModeDiffers(2) && tree->ModeDiffers(0));
}
