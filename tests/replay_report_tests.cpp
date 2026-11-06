// The offline bench's report: what each section holds and what it must never hold (docs/development/offline-bench.md).

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include "../src/cli/theme_replay.hpp"
#include "../src/core/json.hpp"
#include "../src/core/theme/replay.hpp"
#include "replay_support.hpp"
#include "test.hpp"

namespace th = ampify::theme;
namespace fs = std::filesystem;
using namespace replay_support;

namespace {

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Generic(const fs::path& path) {
    std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

std::optional<ampify::Json> ReplayJson(const th::ReplayRequest& request, std::string* report = nullptr) {
    std::string problem;
    std::optional<th::ReplayOutcome> outcome = th::Replay(request, &problem);
    if (!outcome) {
        ampify::test::ReportFailure(__FILE__, __LINE__, problem);
        return std::nullopt;
    }
    if (report) *report = outcome->report;
    return ampify::Json::Parse(outcome->report);
}

}  // namespace

TEST_CASE(ReplayReport_CountsEachRefusalOnceWithTheElementsItWasRefusedOn) {
    fs::path dir = Scratch("refusal-count");
    th::ReplayRequest request;
    request.tree = Fixture();
    request.out = dir / "out";
    request.theme = ThemeFolder(dir, "fonts", "Grid#Root { font-family: Georgia; }\n");
    std::optional<ampify::Json> report = ReplayJson(request);
    CHECK(report.has_value());
    if (!report) return;

    std::unique_ptr<th::ReplayBackend> tree = th::ReplayBackend::Load(ReadFile(Fixture()));
    int grids = 0;
    for (const auto& node : tree->Nodes()) grids += node.type == "Grid" && node.name == "Root" && node.reachable;
    CHECK(grids > 1);

    int entries = 0;
    for (const ampify::Json& item : Items(*report, "refused")) {
        if (Text(item.Find("reason")).find("has no property FontFamily") == std::string::npos) continue;
        entries++;
        CHECK(Text(item.Find("from")) == "engine");
        CHECK(NumberOf(item.Find("count")) == grids);
        CHECK(Items(item, "nodes").size() == static_cast<size_t>(grids));
    }
    CHECK(entries == 1);
}

std::map<std::string, std::string> FinalValues(const ampify::Json& report) {
    std::map<std::string, std::string> after;  // "element property": final value
    for (const ampify::Json& item : Items(report, "final")) {
        for (const auto& [property, value] : *item.Find("values")->AsObject()) {
            after[Text(item.Find("element")) + " " + property] = Text(value.Find("after"));
        }
    }
    return after;
}

bool AnyWarning(const ampify::Json& report, std::string_view part) {
    for (const ampify::Json& warning : Items(report, "warnings")) {
        if (Text(&warning).find(part) != std::string::npos) return true;
    }
    return false;
}

TEST_CASE(ReplayReport_GivesTheArtworkColoursToWhatReadsThem) {
    std::string problem;
    auto variables = th::ArtworkVariablesOf("#204080, 40C0FF\n#102030", &problem);
    CHECK(variables.has_value());
    if (!variables) return;
    CHECK((*variables)["art-1"] == "#204080" && (*variables)["art-2"] == "#40c0ff" && (*variables)["art-3"] == "#102030");
    CHECK((*variables)["page-art-2"] == "#40c0ff" && (*variables)["art-text"] == "#ffffff");
    CHECK((*th::ArtworkVariablesOf("#FFFFFF", nullptr))["art-3"] != "#ffffff");  // darkened under white text
    CHECK(!th::ArtworkVariablesOf("#12345", &problem) && problem.find("not a colour") != std::string::npos);
    CHECK(!th::ArtworkVariablesOf("#111111,#222222,#333333,#444444", &problem));
    CHECK(!th::ArtworkVariablesOf("", &problem));

    fs::path dir = Scratch("artwork");
    th::ReplayRequest request;
    request.tree = Fixture();
    request.out = dir / "out";
    request.theme = ThemeFolder(dir, "cover", "Grid#TransportBar { background: var(--art-3, #FF0B0B10); }\n"
                                              "TextBlock#CurrentTime { color: var(--art-2, #FFFFFFFF); }\n");
    std::optional<ampify::Json> bare = ReplayJson(request);
    CHECK(bare && FinalValues(*bare)["Grid#TransportBar Background"] == "#FF0B0B10");
    CHECK(bare && AnyWarning(*bare, "no artwork given"));

    request.artwork = *variables;
    std::optional<ampify::Json> sung = ReplayJson(request);
    CHECK(sung && FinalValues(*sung)["Grid#TransportBar Background"] == "#FF102030");
    CHECK(sung && FinalValues(*sung)["TextBlock#CurrentTime Foreground"] == "#FF40C0FF");
    CHECK(sung && !AnyWarning(*sung, "no artwork given"));
    CHECK(sung && Text(Member(*sung, "artwork")->Find("art-1")) == "#204080");

    // theme.accent = artwork follows the song's vibrant colour, with no theme too.
    th::ReplayRequest accent;
    accent.tree = Fixture();
    accent.out = dir / "accent";
    accent.values = {{"theme.accent", "artwork"}};
    std::optional<ampify::Json> none = ReplayJson(accent);
    CHECK(none && Items(*none, "resources").empty() && AnyWarning(*none, "no artwork given"));
    accent.artwork = *variables;
    std::optional<ampify::Json> followed = ReplayJson(accent);
    bool written = false;
    for (const ampify::Json& item : followed ? Items(*followed, "resources") : ampify::Json::Array{}) {
        written = written || Text(item.Find("key")) == "AccentFillColorDefaultBrush";
    }
    CHECK(written);
}

TEST_CASE(ReplayReport_ShowsDecorValuesAndTheDiffSeesThemChange) {
    fs::path dir = Scratch("decor-diff");
    th::ReplayRequest request;
    request.tree = Fixture();
    auto run = [&](const std::string& id, const std::string& colour, int bars) {
        request.out = dir / ("out-" + id);
        request.theme = ThemeFolder(dir, id, "Grid#TransportBar::before { content: bars(" + std::to_string(bars) +
                                                 "); color: " + colour + "; width: 44px; }\n");
        std::string problem;
        std::optional<th::ReplayOutcome> outcome = th::Replay(request, &problem);
        if (!outcome) ampify::test::ReportFailure(__FILE__, __LINE__, problem);
        return outcome ? outcome->report : std::string();
    };
    const std::string red = run("red", "#FFFA2D48", 10);
    const std::string white = run("white", "#FFFFFFFF", 10);
    const std::string few = run("few", "#FFFA2D48", 2);
    std::optional<ampify::Json> report = ampify::Json::Parse(red);
    CHECK(report && Items(*report, "decors").size() == 1);
    if (!report || Items(*report, "decors").size() != 1) return;
    const ampify::Json& decor = Items(*report, "decors")[0];
    CHECK(Text(decor.Find("image")) == "bars:10" && Text(decor.Find("slot")) == "::before");
    CHECK(Text(decor.Find("values")->Find("Width")) == "44");

    bool same = true;
    std::optional<std::string> colour = th::DiffReports(red, white, same, nullptr);
    CHECK(colour && !same);
    std::optional<ampify::Json> json = colour ? ampify::Json::Parse(*colour) : std::nullopt;
    CHECK(json && Items(*json, "decors").size() == 1);
    if (json && Items(*json, "decors").size() == 1) {
        const ampify::Json& change = Items(*json, "decors")[0];
        CHECK(Text(change.Find("before")) == "#FFFA2D48" && Text(change.Find("after")) == "#FFFFFFFF");
        CHECK(Text(change.Find("decor")).find("Grid#TransportBar::before") != std::string::npos);
    }
    std::optional<std::string> count = th::DiffReports(red, few, same, nullptr);
    CHECK(count && !same && count->find("\"bars:2\"") != std::string::npos);
    CHECK(th::DiffReports(red, red, same, nullptr) && same);
}

TEST_CASE(ReplayReport_WritesPathsRelativeToTheKitAndNeverTheProfileFolder) {
    fs::path dir = Scratch("paths");
    fs::path theme = ThemeFolder(dir, "decor",
                                 "Grid#TransportBar::before { content: url(\"images/mark.png\"); width: 12px; }\n"
                                 "TextBlock#CurrentTime { font-family: url(\"fonts/none.ttf\"); }\n");
    WriteFile(theme / "images" / "mark.png", "not really a picture");
    th::ReplayRequest request;
    request.tree = Fixture();
    request.out = dir / "out";
    request.theme = theme;
    std::string text;
    std::optional<ampify::Json> report = ReplayJson(request, &text);
    CHECK(report.has_value());
    if (!report) return;

    const std::string kit = Lower(Generic(fs::absolute(request.out) / "kit"));
    CHECK(Lower(text).find(kit) == std::string::npos);
    CHECK(text.find("themes/decor/images/mark.png") != std::string::npos);
    if (const char* home = std::getenv("USERPROFILE"); home && std::string(home).size() > 3) {
        std::string generic = home;
        std::replace(generic.begin(), generic.end(), '\\', '/');
        CHECK(Lower(text).find(Lower(generic)) == std::string::npos);
        CHECK(Lower(text).find(Lower(home)) == std::string::npos);
        std::string escaped;
        for (char c : std::string(home)) escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
        CHECK(Lower(text).find(Lower(escaped)) == std::string::npos);
    }
}
