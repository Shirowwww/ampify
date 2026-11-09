// The offline bench's removal check (`--then`): a second state replayed on the same tree after the first, through the
// engine's restore path, against the same state on a fresh tree.

#include <sstream>
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

// A theme that leaves a trace of every kind: values, a relative size, a decor, values under the pointer, a resource.
constexpr const char* kBusy = "Grid#TransportBar { background: #FF112233; corner-radius: 6; }\n"
                              "Grid#TransportBar::before { content: bars(6); color: #FFFA2D48; width: 30px; }\n"
                              "Grid#TransportBar:hover { background: #FF445566; }\n"
                              "TextBlock#CurrentTime { color: #FFFF0000; font-size: 120%; }\n";

std::string Generic(const fs::path& path) {
    std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace

TEST_CASE(ReplayRemoval_FindsWhatOneTreeShowsAndAFreshOneDoesNot) {
    std::unique_ptr<th::ReplayBackend> tree = th::ReplayBackend::Load(ReadFile(Fixture()));
    std::unique_ptr<th::ReplayBackend> fresh = th::ReplayBackend::Load(ReadFile(Fixture()));
    CHECK(tree && fresh);
    if (!tree || !fresh) return;
    const th::NodeId bar = th::ReplayBackend::IdOf(0);
    tree->BeginPass(1);
    fresh->BeginPass(1);
    CHECK(tree->Write(bar, "Opacity", {th::ValueKind::Number, "0.5"}));
    CHECK(tree->Write(bar, "Margin", {th::ValueKind::Thickness, "1,1,1,1"}));
    CHECK(fresh->Write(bar, "Margin", {th::ValueKind::Thickness, "1,1,1,1"}));  // the same on both: not left behind
    th::Decor decor{"bars:4", {}};
    CHECK(tree->SetDecor(bar, th::PseudoElement::Before, &decor) == th::DecorChange::Applied);

    std::optional<ampify::Json> left = ampify::Json::Parse(th::LeftoversJson(*tree, *fresh));
    CHECK(left && left->AsArray() && left->AsArray()->size() == 2);
    if (!left || !left->AsArray() || left->AsArray()->size() != 2) return;
    const ampify::Json& opacity = (*left->AsArray())[0];
    CHECK(Text(opacity.Find("property")) == "Opacity" && Text(opacity.Find("value")) == "0.5");
    CHECK(Text(opacity.Find("element")) == "Grid#TransportBar" && Text(opacity.Find("fresh")) != "0.5");
    const ampify::Json& before = (*left->AsArray())[1];
    CHECK(Text(before.Find("property")) == "::before" && Text(before.Find("value")) == "bars:4");
    CHECK(Text(before.Find("fresh")) == "(none)");
    CHECK(th::LeftoversJson(*fresh, *fresh) == "[]");
}

TEST_CASE(ReplayRemoval_TakingAThemeOffLeavesNothingAndReportsTheSecondState) {
    ScratchDir dir("removal");
    th::ReplayRequest request;
    request.tree = Fixture();
    request.out = dir / "out";
    request.theme = ThemeFolder(dir, "busy", kBusy);
    request.then = fs::path();  // none
    std::string problem;
    std::optional<th::ReplayOutcome> outcome = th::Replay(request, &problem);
    CHECK(outcome.has_value());
    if (!outcome) {
        ampify::test::ReportFailure(__FILE__, __LINE__, problem);
        return;
    }
    CHECK(outcome->settled && outcome->leftovers == 0);
    std::optional<ampify::Json> report = ampify::Json::Parse(outcome->report);
    CHECK(report && Text(report->Find("theme")).empty());
    CHECK(report && Text(Member(*report, "then")->Find("first")) == "busy");
    CHECK(report && Items(*report, "leftovers").empty() && Items(*report, "decors").empty() &&
          Items(*report, "hover").empty());
    CHECK(report && Items(*report, "final").empty());  // everything the theme set was given back: nothing changed
    bool same = false;
    CHECK(th::DiffReports(outcome->report, outcome->freshReport, same, nullptr) && same);

    // Another theme over it: the second state is that theme's, as on a fresh tree.
    request.then = ThemeFolder(dir, "plain", "Grid#TransportBar { background: #FF00FF00; }\n");
    std::optional<th::ReplayOutcome> switched = th::Replay(request, &problem);
    CHECK(switched && switched->settled && switched->leftovers == 0);
    std::optional<ampify::Json> second = switched ? ampify::Json::Parse(switched->report) : std::nullopt;
    CHECK(second && Text(second->Find("theme")) == "plain");
    if (second) {
        // The bar's background, not what the first theme set and gave back. Ampify's own rule for the bar's toggles
        // (KeepTogglesAtTheRightEnd) holds under every theme, so the toggles' row is left out of the count, and so is
        // the icon of the veil over the bar's artwork (KeepLcdVeilInItsArtwork).
        std::vector<ampify::Json> finals;
        for (const ampify::Json& item : Items(*second, "final")) {
            const std::string element = Text(item.Find("element"));
            if (element != "StackPanel#NowPlayingToggleButtons" && element != "VolumeControl#VolumeControl" &&
                element != "ToggleButton#LyricsToggleButton" && element.find("FontIcon") == std::string::npos) {
                finals.push_back(item);
            }
        }
        CHECK(finals.size() == 1 && Text(finals.front().Find("element")) == "Grid#TransportBar");
        for (const ampify::Json& item : Items(*second, "final")) {
            for (const auto& [property, value] : *item.Find("values")->AsObject()) {
                CHECK(Text(value.Find("before")) != Text(value.Find("after")));
            }
        }
    }
    CHECK(switched && th::DiffReports(switched->report, switched->freshReport, same, nullptr) && same);
}

TEST_CASE(ReplayRemoval_CommandWritesTheFreshTreeAndTheDiff) {
    ScratchDir dir("removal-cli");
    const std::string theme = Generic(ThemeFolder(dir, "busy", kBusy));
    const std::string snippet = Generic(ThemeFolder(dir, "dim", "TextBlock#Duration { opacity: 0.4; }\n",
                                                    R"({"id": "dim", "kind": "snippet"})"));
    const std::string out = Generic(dir / "out");
    std::ostringstream text;
    std::ostringstream err;
    int code = th::RunReplayCommand({"replay", theme, "--tree", Generic(Fixture()), "--out", out, "--snippet", snippet,
                                     "--then", "none"},
                                    text, err);
    CHECK(code == 0);
    CHECK(text.str().find("Nothing left behind") != std::string::npos);
    CHECK(fs::exists(dir / "out" / "fresh" / "report.json") && fs::exists(dir / "out" / "diff.json"));

    // The snippet kept on in the second state with --then-snippet; --then-snippet alone and with --compare refused.
    CHECK(th::RunReplayCommand({"replay", theme, "--tree", Generic(Fixture()), "--out", out, "--snippet", snippet,
                                "--then", "none", "--then-snippet", snippet},
                               text, err) == 0);
    CHECK(th::RunReplayCommand({"replay", theme, "--tree", Generic(Fixture()), "--out", out, "--then-snippet", snippet},
                               text, err) == 2);
    CHECK(th::RunReplayCommand({"replay", theme, "--tree", Generic(Fixture()), "--out", out, "--then", "none",
                                "--compare", "none"},
                               text, err) == 2);
    CHECK(th::RunReplayCommand({"replay", theme, "--tree", Generic(Fixture()), "--out", out, "--then", snippet},
                               text, err) == 2);  // a snippet given as the second theme
}
