// The offline bench: ReplayBackend over a recorded tree, the replay of a theme through the theme service's own pass,
// the diff of two reports, and the scrubber. tests/data/replay/transport-bar.json is the play bar of the CI bench's
// Apple Music (no account signed in), cut out of a scrubbed inspector.json.

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "../src/cli/dump_scrub.hpp"
#include "../src/cli/theme_replay.hpp"
#include "../src/core/json.hpp"
#include "../src/core/theme/inspector.hpp"
#include "../src/core/theme/replay.hpp"
#include "replay_support.hpp"
#include "test.hpp"

namespace th = ampify::theme;
namespace fs = std::filesystem;
using namespace replay_support;

namespace {

bool AnyReason(const ampify::Json& report, std::string_view part) {
    const ampify::Json::Array* refused = Member(report, "refused")->AsArray();
    if (!refused) return false;
    for (const ampify::Json& item : *refused) {
        if (Text(item.Find("reason")).find(part) != std::string::npos) return true;
    }
    return false;
}

// A small tree as a version 2 inspector.json: recorded under a theme, with boxes and the theme's originals.
constexpr const char* kVersion2 = R"json({"version":2,"truncated":false,"theme":"night",
  "window":{"width":400,"height":100,"sidebarFolded":false},
  "resources":[{"key":"AccentFillColorDefaultBrush","value":"#FF0A84FF"}],
  "nodes":[
   {"index":0,"id":"9","type":"Grid","fullType":"","name":"Bar","parent":-1,"depth":0,"selector":"Grid#Bar",
    "icon":false,"panel":true,"player":false,"playing":false,"box":[0,0,400,100],
    "properties":{"Background":"#FF000000","Margin":"0,0,0,0","Opacity":"1","Visibility":"Visible","Width":"-nan(ind)",
                  "Height":"-nan(ind)","Padding":"0,0,0,0","BorderBrush":"present","CornerRadius":"0,0,0,0"},
    "originals":{"Background":"#FFFFFFFF"}},
   {"index":1,"id":"10","type":"TextBlock","fullType":"","name":"Title","parent":0,"depth":1,"selector":"TextBlock#Title",
    "icon":false,"panel":false,"player":false,"playing":false,"box":[10,10,200,20],
    "properties":{"Foreground":"#FFEEEEEE","FontSize":"14","Margin":"0,0,0,0","Opacity":"1","Visibility":"Visible",
                  "Width":"-nan(ind)","Height":"-nan(ind)"},
    "originals":{"Foreground":"no-own-value"}}]})json";

}  // namespace

TEST_CASE(Replay_LoadsAVersion1DumpAndJudgesWhatItLacks) {
    std::string problem;
    std::unique_ptr<th::ReplayBackend> tree = th::ReplayBackend::Load(ReadFile(Fixture()), &problem);
    CHECK(tree != nullptr);
    if (!tree) {
        ampify::test::ReportFailure(__FILE__, __LINE__, problem);
        return;
    }
    CHECK(tree->Version() == 1);
    CHECK(tree->Nodes().size() == 118);
    CHECK(tree->Snapshot().size() == 118);
    CHECK(tree->Nodes()[0].name == "TransportBar" && tree->Nodes()[0].parent == -1);
    CHECK(!tree->WindowSize().has_value());
    CHECK(!tree->Warnings().empty() && tree->Warnings()[0].find("no element flags") != std::string::npos);

    // A Grid is a panel, a Border is not; a Button has a template and a BorderThickness the inspector never read.
    CHECK(tree->Nodes()[0].panel);
    int border = -1;
    for (int i = 0; i < static_cast<int>(tree->Nodes().size()); i++) {
        if (tree->Nodes()[i].type == "Border" && tree->Nodes()[i].reachable) border = i;
    }
    CHECK(border >= 0 && !tree->Nodes()[border].panel);
    int repeat = IndexNamed(*tree, "RepeatButton");
    CHECK(repeat >= 0);
    CHECK(tree->Read(th::ReplayBackend::IdOf(repeat), "Template").has_value());
    CHECK(tree->Read(th::ReplayBackend::IdOf(repeat), "BorderThickness")->text == "0,0,0,0");
    // A TextBlock has no Background, in the live tree as here.
    int time = IndexNamed(*tree, "CurrentTime");
    CHECK(time >= 0 && !tree->Read(th::ReplayBackend::IdOf(time), "Background").has_value());
}

TEST_CASE(Replay_RefusesWhatTheLiveBackendWouldAndRecordsEveryWrite) {
    std::unique_ptr<th::ReplayBackend> tree = th::ReplayBackend::Load(ReadFile(Fixture()));
    CHECK(tree != nullptr);
    if (!tree) return;
    const th::NodeId bar = th::ReplayBackend::IdOf(0);
    tree->BeginPass(1);
    CHECK(tree->Write(bar, "Background", {th::ValueKind::Color, "#FF112233"}));
    CHECK(tree->Read(bar, "Background")->text == "#FF112233");
    CHECK(!tree->Write(bar, "Padding", {th::ValueKind::Thickness, "0,0,-2,0"}));       // ends Apple Music live
    CHECK(!tree->Write(bar, "Visibility", {th::ValueKind::Visibility, "Hidden"}));     // not a member
    CHECK(!tree->Write(bar, "Background", {th::ValueKind::Color, "red"}));             // not #AARRGGBB
    CHECK(!tree->Write(bar, "FontSize", {th::ValueKind::Length, "14"}));               // a Grid has no FontSize
    CHECK(tree->Write(bar, "Margin", {th::ValueKind::Thickness, "0,0,-2,0"}));         // a margin may be negative

    const auto& changes = tree->Changes();
    CHECK(changes.size() == 6);
    if (changes.size() != 6) return;
    CHECK(changes[0].accepted && changes[0].before == "#00FFFFFF" && changes[0].after == "#FF112233");
    CHECK(!changes[1].accepted && changes[1].reason.find("negative") != std::string::npos);
    CHECK(!changes[3].accepted && changes[3].reason.find("#AARRGGBB") != std::string::npos);
    CHECK(!changes[4].accepted && changes[4].reason.find("has no property FontSize") != std::string::npos);
    CHECK(changes[5].accepted && changes[5].pass == 1);

    // A move into an element that is not a panel is refused; into a panel, it goes last.
    int time = IndexNamed(*tree, "CurrentTime");
    int lcd = IndexNamed(*tree, "LCD");
    CHECK(!tree->Reparent(th::ReplayBackend::IdOf(lcd), th::ReplayBackend::IdOf(time), -1));
    int grid = -1;
    for (int i = 1; i < static_cast<int>(tree->Nodes().size()); i++) {
        if (tree->Nodes()[i].panel && tree->Nodes()[i].parent == 0) grid = i;
    }
    int child = tree->Nodes()[0].children.front();
    if (grid >= 0 && grid != child && tree->Nodes()[child].reachable) {
        CHECK(tree->Reparent(th::ReplayBackend::IdOf(child), th::ReplayBackend::IdOf(grid), -1));
        CHECK(tree->Nodes()[grid].children.back() == child && tree->Nodes()[child].parent == grid);
    }
}

// The progress line's track and thumb, which dumps older than these properties never recorded: present on the line
// only, and checked as the live backend checks them.
TEST_CASE(Replay_GivesTheProgressLineItsTrackAndThumb) {
    std::unique_ptr<th::ReplayBackend> tree = th::ReplayBackend::Load(ReadFile(Fixture()));
    CHECK(tree != nullptr);
    if (!tree) return;
    int scrubber = IndexNamed(*tree, "LCDScrubber");
    CHECK(scrubber >= 0);
    if (scrubber < 0) return;
    const th::NodeId line = th::ReplayBackend::IdOf(scrubber);
    tree->BeginPass(1);
    CHECK(tree->Read(line, "TrackHeight").has_value());
    CHECK(tree->Write(line, "TrackHeight", {th::ValueKind::Length, "4"}));
    CHECK(tree->Write(line, "ThumbVisibility", {th::ValueKind::Visibility, "Visible"}));
    CHECK(!tree->Write(line, "TrackRectRadius", {th::ValueKind::Length, "-2"}));
    CHECK(!tree->Read(th::ReplayBackend::IdOf(0), "TrackHeight").has_value());
}

TEST_CASE(Replay_RunsTheEngineUntilItSettlesAndReportsEachRule) {
    fs::path dir = Scratch("settle");
    fs::path theme = ThemeFolder(dir, "night",
                                 "Grid#TransportBar { background: #FF112233; }\n"
                                 "TextBlock#CurrentTime { color: #FFFF0000; font-size: 120%; }\n"
                                 "TextBlock#CurrentTime { background: #FF00FF00; }\n"
                                 "Grid#NoSuchThing { background: #FF000000; }\n"
                                 "Grid#TransportBar:hover { background: #FF445566; }\n");
    th::ReplayRequest request;
    request.theme = theme;
    request.tree = Fixture();
    request.out = dir / "out";
    std::string problem;
    std::optional<th::ReplayOutcome> outcome = th::Replay(request, &problem);
    CHECK(outcome.has_value());
    if (!outcome) {
        ampify::test::ReportFailure(__FILE__, __LINE__, problem);
        return;
    }
    // Pass 1 writes, pass 2 finds everything in place, the relative size included (WinUI's single precision).
    CHECK(outcome->settled && outcome->passes == 2);
    std::optional<ampify::Json> report = ampify::Json::Parse(outcome->report);
    CHECK(report.has_value());
    if (!report) return;
    CHECK(Text(report->Find("theme")) == "night");

    bool barRule = false;
    for (const ampify::Json& rule : *Member(*report, "rules")->AsArray()) {
        if (Text(rule.Find("selector")) == "Grid#TransportBar" && Text(rule.Find("property")) == "Background" &&
            !rule.Find("state")) {
            const ampify::Json::Array* matched = rule.Find("matched")->AsArray();
            barRule = matched && matched->size() == 1 && *(*matched)[0].AsNumber() == 0;
        }
    }
    CHECK(barRule);
    bool unmatched = false;
    for (const ampify::Json& item : *Member(*report, "unmatched")->AsArray()) {
        unmatched = unmatched || Text(item.Find("selector")) == "Grid#NoSuchThing";
    }
    CHECK(unmatched);
    CHECK(AnyReason(*report, "has no property Background"));

    std::map<std::string, std::string> after;  // element property: final value
    for (const ampify::Json& item : *Member(*report, "final")->AsArray()) {
        for (const auto& [property, value] : *item.Find("values")->AsObject()) {
            after[Text(item.Find("element")) + " " + property] = Text(value.Find("after"));
        }
    }
    CHECK(after["Grid#TransportBar Background"] == "#FF112233");
    CHECK(after["TextBlock#CurrentTime Foreground"] == "#FFFF0000");
    CHECK(!after["TextBlock#CurrentTime FontSize"].empty() && after["TextBlock#CurrentTime FontSize"] != "12");
    // The :hover value is held by the backend, not written: the report lists it apart.
    const ampify::Json::Array* hover = Member(*report, "hover")->AsArray();
    CHECK(hover && hover->size() == 1 && Text((*hover)[0].Find("element")) == "Grid#TransportBar");
    CHECK(outcome->wireframe.find("<svg") == 0 && outcome->wireframe.find("Grid#TransportBar") != std::string::npos);
}

TEST_CASE(Replay_Version2GivesTheOriginalsBackAndDrawsTheBoxes) {
    std::unique_ptr<th::ReplayBackend> tree = th::ReplayBackend::Load(kVersion2);
    CHECK(tree != nullptr);
    if (!tree) return;
    CHECK(tree->Version() == 2 && tree->HasBoxes() && tree->RecordedTheme() == "night");
    CHECK(tree->WindowSize() && tree->WindowSize()->width == 400);
    CHECK(tree->Shown(0, "Background")->text == "#FFFFFFFF");  // Apple's own, under the theme's black
    CHECK(tree->Shown(1, "Foreground")->text == "#FFEEEEEE");  // no value of its own: kept as recorded
    CHECK(tree->Nodes()[0].panel && !tree->Nodes()[1].panel);
    std::unique_ptr<th::ReplayBackend> recorded = th::ReplayBackend::Load(kVersion2, nullptr, true);
    CHECK(recorded && recorded->Shown(0, "Background")->text == "#FF000000");

    // The inspector writes the same fields back from any backend: flags, boxes, window and originals.
    th::OriginalValues originals{{{th::ReplayBackend::IdOf(0), "Background"}, {th::ValueKind::Color, "#FF000000"}},
                                 {{th::ReplayBackend::IdOf(1), "Foreground"}, th::kNoOwnValue}};
    th::InspectOptions options;
    options.theme = "night";
    options.originals = &originals;
    std::optional<ampify::Json> dump = ampify::Json::Parse(th::InspectTreeJson(*tree, options));
    CHECK(dump.has_value());
    if (!dump) return;
    CHECK(*dump->Find("version")->AsNumber() == 2 && Text(dump->Find("theme")) == "night");
    const ampify::Json& bar = (*dump->Find("nodes")->AsArray())[0];
    CHECK(*bar.Find("panel")->AsBool() && bar.Find("box")->AsArray()->size() == 4);
    CHECK(Text(bar.Find("originals")->Find("Background")) == "#FF000000");
    const ampify::Json& title = (*dump->Find("nodes")->AsArray())[1];
    CHECK(Text(title.Find("originals")->Find("Foreground")) == "no-own-value");

    fs::path dir = Scratch("boxes");
    std::ofstream(dir / "tree.json", std::ios::binary) << kVersion2;
    th::ReplayRequest request;
    request.theme = ThemeFolder(dir, "red", "Grid#Bar { background: #FFFF0000; }\n");
    request.tree = dir / "tree.json";
    request.out = dir / "out";
    std::optional<th::ReplayOutcome> outcome = th::Replay(request, nullptr);
    CHECK(outcome && outcome->settled);
    CHECK(outcome && outcome->wireframe.find("width=\"400.0\" height=\"100.0\" rx=\"0.0\" fill=\"#FF0000\"") !=
                         std::string::npos);
    CHECK(outcome && outcome->wireframe.find("fill=\"#EEEEEE\"") != std::string::npos);  // the title's text bar
}

TEST_CASE(Replay_DiffTellsTwoThemesApartAndATwinFromItself) {
    fs::path dir = Scratch("diff");
    th::ReplayRequest request;
    request.tree = Fixture();
    request.theme = ThemeFolder(dir, "blue", "Grid#TransportBar { background: #FF0000FF; }\n");
    request.out = dir / "out-blue";
    std::optional<th::ReplayOutcome> blue = th::Replay(request, nullptr);
    request.out = dir / "out-blue-again";
    std::optional<th::ReplayOutcome> twin = th::Replay(request, nullptr);
    request.theme = ThemeFolder(dir, "green", "Grid#TransportBar { background: #FF00FF00; }\n"
                                              "TextBlock#Duration { color: #FF00FF00; }\n");
    request.out = dir / "out-green";
    std::optional<th::ReplayOutcome> green = th::Replay(request, nullptr);
    CHECK(blue && twin && green);
    if (!blue || !twin || !green) return;

    bool same = false;
    std::optional<std::string> diff = th::DiffReports(blue->report, green->report, same, nullptr);
    CHECK(diff && !same);
    std::optional<ampify::Json> json = diff ? ampify::Json::Parse(*diff) : std::nullopt;
    CHECK(json && json->Find("values")->AsArray()->size() == 2);
    if (json && json->Find("values")->AsArray()->size() == 2) {
        const ampify::Json& first = (*json->Find("values")->AsArray())[0];
        CHECK(Text(first.Find("property")) == "Background" && Text(first.Find("before")) == "#FF0000FF" &&
              Text(first.Find("after")) == "#FF00FF00");
        const ampify::Json& second = (*json->Find("values")->AsArray())[1];
        CHECK(Text(second.Find("element")) == "TextBlock#Duration" && Text(second.Find("after")) == "#FF00FF00");
    }
    CHECK(th::DiffReports(blue->report, twin->report, same, nullptr) && same);
    CHECK(!th::DiffReports("{}", blue->report, same, nullptr));

    std::ostringstream out;
    std::ostringstream err;
    CHECK(th::RunReplayCommand({"replay", "--tree"}, out, err) == 2);
    CHECK(th::RunReplayCommand({"replay", "none", "--out", "x"}, out, err) == 2);  // no --tree
}

TEST_CASE(Scrub_ReplacesTextsFreeNamesAndAddressesAndKeepsTheShape) {
    const std::string dump = R"json({"version":2,"theme":"glass","window":{"width":800,"height":600,"sidebarFolded":false},
      "resources":[{"key":"AccentFillColorDefaultBrush","value":"#FF0A84FF"}],
      "nodes":[
       {"index":0,"id":"2349283","type":"Grid","fullType":"Microsoft.UI.Xaml.Controls.Grid","name":"Road Trip Mix","parent":-1,
        "depth":0,"selector":"Grid#Road Trip Mix","panel":true,"box":[0,0,800,600],
        "properties":{"Background":"#FF101010","FontFamily":"file:///C:/Users/someone/Ampify/themes/x/fonts/a.ttf#Inter"}},
       {"index":1,"id":"2349999","type":"TextBlock","fullType":"","name":"Title","parent":0,"depth":1,"selector":"TextBlock#Title",
        "text":"A Song Someone Likes","properties":{"Foreground":"#FFFFFFFF","Text":"A Song Someone Likes",
        "FontFamily":"ms-appx:///AMP.UI.Controls/Assets/SFHello.ttc#SF Hello"}},
       {"index":2,"id":"2350000","type":"Image","fullType":"","name":"","parent":0,"depth":1,"selector":"Grid#Road Trip Mix > Image",
        "properties":{"Source":"https://is1-ssl.mzstatic.com/image/thumb/abc/600x600bb.jpg"}}]})json";
    ampify::cli::ScrubCounts counts;
    std::string problem;
    std::optional<std::string> scrubbed = ampify::cli::ScrubDump(dump, ampify::cli::Identity{}, counts, &problem);
    CHECK(scrubbed.has_value());
    if (!scrubbed) {
        ampify::test::ReportFailure(__FILE__, __LINE__, problem);
        return;
    }
    CHECK(scrubbed->find("Road Trip") == std::string::npos);
    CHECK(scrubbed->find("Someone") == std::string::npos && scrubbed->find("someone") == std::string::npos);
    CHECK(scrubbed->find("mzstatic") == std::string::npos && scrubbed->find("2349283") == std::string::npos);
    CHECK(counts.names == 1 && counts.texts == 2 && counts.addresses == 2);

    std::optional<ampify::Json> json = ampify::Json::Parse(*scrubbed);
    CHECK(json.has_value());
    if (!json) return;
    CHECK(*json->Find("scrubbed")->AsBool() && *json->Find("version")->AsNumber() == 2);
    const ampify::Json::Array& nodes = *json->Find("nodes")->AsArray();
    CHECK(nodes.size() == 3);
    if (nodes.size() != 3) return;
    CHECK(Text(nodes[0].Find("name")) == "name1" && Text(nodes[0].Find("selector")) == "Grid#name1");
    CHECK(Text(nodes[0].Find("id")) == "0" && nodes[0].Find("box")->AsArray()->size() == 4);
    CHECK(Text(nodes[0].Find("properties")->Find("Background")) == "#FF101010");
    CHECK(Text(nodes[0].Find("properties")->Find("FontFamily")) == "<address>#Inter");
    CHECK(Text(nodes[1].Find("name")) == "Title" && Text(nodes[1].Find("text")) == "<text>");
    CHECK(Text(nodes[1].Find("properties")->Find("FontFamily")) == "ms-appx:///AMP.UI.Controls/Assets/SFHello.ttc#SF Hello");
    CHECK(Text(nodes[2].Find("selector")) == "Grid#name1 > Image");
    CHECK(Text(nodes[2].Find("properties")->Find("Source")) == "<address>");
    // The scrubbed copy still replays.
    CHECK(th::ReplayBackend::Load(*scrubbed) != nullptr);
    CHECK(!ampify::cli::ScrubDump("[1]", ampify::cli::Identity{}, counts, &problem));
}
