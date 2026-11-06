#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/core/extension.hpp"
#include "../src/core/extension_loader.hpp"
#include "../src/core/settings.hpp"
#include "fake_host.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

// A kit folder with a theme `neon` whose main.js logs when it starts, and a second folder under themes that has no
// script.
struct Setup : ampify::test::KitFixture {
    explicit Setup(const std::string& name, const std::string& process = "ui") : KitFixture(name, process) {
        const fs::path themes = root / "themes";
        ampify::test::WriteElement(themes / "neon", {.id = "neon", .name = "Neon", .kind = "theme",
                                                      .extra = R"("colors":"colors.ini")"},
                                   R"(Ampify.log("theme script");)");
        ampify::test::WriteFile(themes / "neon" / "colors.ini", "[Base]\nAccentFillColorDefaultBrush = 0A84FF\n");
        ampify::test::WriteFile(themes / "plain" / "colors.ini", "[Base]\nAccentFillColorDefaultBrush = FFFFFF\n");
        loader.SetThemeRoot(themes);
    }
};

}  // namespace

TEST_CASE(ThemeScript_RunsWhileItsThemeIsActive) {
    Setup s("active");
    ampify::LoadReport report = s.loader.Discover();
    CHECK(std::find(report.loaded.begin(), report.loaded.end(), "neon") != report.loaded.end());

    s.settings.Set("theme.active", "neon");
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("neon") == ampify::ExtensionState::Running);
    CHECK(s.Logged("[neon] theme script"));
}

TEST_CASE(ThemeScript_StaysOffWhileAnotherThemeIsActive) {
    Setup s("other");
    s.loader.Discover();
    s.settings.Set("theme.active", "midnight");
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("neon") == ampify::ExtensionState::Off);
    CHECK(!s.Logged("theme script"));
}

TEST_CASE(ThemeScript_StaysOffWhenThePartIsSwitchedOff) {
    Setup s("part");
    s.loader.Discover();
    s.settings.Set("theme.active", "neon");
    s.settings.Set("theme.parts.script", "0");
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("neon") == ampify::ExtensionState::Off);
}

TEST_CASE(ThemeScript_IsLoadedOnlyInAppleMusic) {
    Setup s("engine", "engine");
    ampify::LoadReport report = s.loader.Discover();
    CHECK(std::find(report.loaded.begin(), report.loaded.end(), "neon") == report.loaded.end());
    CHECK(report.rejected.empty());
}

TEST_CASE(ThemeScript_AFolderWithoutScriptIsNotAnElement) {
    Setup s("plain-folder");
    ampify::LoadReport report = s.loader.Discover();
    CHECK(std::find(report.loaded.begin(), report.loaded.end(), "plain") == report.loaded.end());
    CHECK(std::find(report.rejected.begin(), report.rejected.end(), "plain") == report.rejected.end());
}

TEST_CASE(ThemeScript_ThemeRootIsOptional) {
    Setup s("no-themes");
    ampify::ExtensionLoader loader(s.hosts, s.host, s.journal, s.root / "extensions");
    ampify::LoadReport report = loader.Discover();
    CHECK(report.loaded.empty());
}
