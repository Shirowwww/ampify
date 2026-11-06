// `ampify theme replay` on the command line: what it refuses before replaying, with the exit code 2 of a wrong use.

#include <sstream>
#include <string>
#include <vector>

#include "../src/cli/theme_replay.hpp"
#include "replay_support.hpp"
#include "test.hpp"

namespace th = ampify::theme;
namespace fs = std::filesystem;
using namespace replay_support;

namespace {

constexpr const char* kThemeManifest = R"({"id": "night", "kind": "theme", "settings": [
  {"key": "radius", "label": "Radius", "type": "number", "min": 0, "max": 40, "default": 8},
  {"key": "position", "label": "Bar", "type": "choice", "options": ["Top", "Bottom"], "default": "Top"},
  {"key": "glow", "label": "Glow", "type": "toggle", "default": true}]})";

struct Result {
    int code = -1;
    std::string out;
    std::string err;
};

Result Run(std::vector<std::string> args) {
    std::ostringstream out;
    std::ostringstream err;
    Result result;
    result.code = th::RunReplayCommand(args, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

std::string Generic(const fs::path& path) {
    std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace

TEST_CASE(ReplayCli_ChecksEachSetAgainstTheManifestBeforeReplaying) {
    ScratchDir dir("cli-set");
    const std::string theme = Generic(ThemeFolder(dir, "night", "Grid#TransportBar { background: #FF112233; }\n",
                                                  kThemeManifest));
    const std::string tree = Generic(Fixture());
    const std::string out = Generic(dir / "out");
    auto replay = [&](const std::string& set) { return Run({"replay", theme, "--tree", tree, "--out", out, "--set", set}); };

    Result unknown = replay("themes.night.nonsense=1");
    CHECK(unknown.code == 2 && unknown.err.find("warning: --set themes.night.nonsense=1") != std::string::npos);
    CHECK(unknown.err.find("declares no setting \"nonsense\" (it declares radius, position, glow)") != std::string::npos);
    CHECK(!fs::exists(dir / "out" / "report.json"));  // nothing replayed

    Result range = replay("themes.night.radius=500");
    CHECK(range.code == 2 && range.err.find("above its maximum, 40") != std::string::npos);
    Result choice = replay("themes.night.position=Sideways");
    CHECK(choice.code == 2 && choice.err.find("not one of its choices: Top, Bottom") != std::string::npos);
    Result toggle = replay("themes.night.glow=maybe");
    CHECK(toggle.code == 2 && toggle.err.find("not on or off") != std::string::npos);
    Result other = replay("themes.glass.blur=0");
    CHECK(other.code == 2 && other.err.find("no theme \"glass\" in this replay") != std::string::npos);
    Result snippet = replay("snippets.declutter.badges=0");
    CHECK(snippet.code == 2 && snippet.err.find("no snippet \"declutter\"") != std::string::npos);
    Result service = replay("theme.nonsense=1");
    CHECK(service.code == 2 && service.err.find("not a key of the theme service") != std::string::npos);
    Result accent = replay("theme.accent=reddish");
    CHECK(accent.code == 2 && accent.err.find("not RRGGBB") != std::string::npos);

    Result fits = replay("themes.night.radius=12");
    CHECK(fits.code == 0 && fits.err.empty());
    CHECK(replay("theme.accent=#FF3B30").code == 0);
}

TEST_CASE(ReplayCli_RefusesAFileForOutAThemeAsASnippetAndAnEmptyWindow) {
    ScratchDir dir("cli-edges");
    const std::string theme = Generic(ThemeFolder(dir, "night", "Grid#TransportBar { background: #FF112233; }\n",
                                                  kThemeManifest));
    const std::string snippet = Generic(ThemeFolder(dir, "bars", "Grid#TransportBar { opacity: 0.5; }\n",
                                                    R"({"id": "bars", "kind": "snippet"})"));
    const std::string tree = Generic(Fixture());
    const std::string out = Generic(dir / "out");

    WriteFile(dir / "file.txt", "x");
    Result file = Run({"replay", theme, "--tree", tree, "--out", Generic(dir / "file.txt")});
    CHECK(file.code == 2 && file.err.find("--out is a file, not a folder") != std::string::npos);

    Result asTheme = Run({"replay", snippet, "--tree", tree, "--out", out});
    CHECK(asTheme.code == 2 && asTheme.err.find("is a snippet, not a theme") != std::string::npos);
    Result asSnippet = Run({"replay", "none", "--tree", tree, "--out", out, "--snippet", theme});
    CHECK(asSnippet.code == 2 && asSnippet.err.find("is a theme, not a snippet") != std::string::npos);
    CHECK(Run({"replay", "none", "--tree", tree, "--out", out, "--snippet", snippet}).code == 0);

    WriteFile(dir / "palette.txt", "#204080\n#40C0FF\n");
    CHECK(Run({"replay", theme, "--tree", tree, "--out", out, "--artwork", Generic(dir / "palette.txt")}).code == 0);
    CHECK(Run({"replay", theme, "--tree", tree, "--out", out, "--artwork", "#204080,#40C0FF"}).code == 0);
    Result colour = Run({"replay", theme, "--tree", tree, "--out", out, "--artwork", "red"});
    CHECK(colour.code == 2 && colour.err.find("\"red\" is not a colour") != std::string::npos);

    Result window = Run({"replay", theme, "--tree", tree, "--out", out, "--window", "0x0"});
    CHECK(window.code == 2 && window.err.find("both above 0") != std::string::npos);
    CHECK(Run({"replay", theme, "--tree", tree, "--out", out, "--window", "800x0"}).code == 2);
}
