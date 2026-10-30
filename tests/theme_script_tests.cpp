#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../core/extension.hpp"
#include "../core/extension_loader.hpp"
#include "../core/settings.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

// A kit folder with a theme `neon` whose main.js logs when it starts, and a second folder under themes that has no
// script. Each test has its own name, so the folders never clash.
struct Kit {
    fs::path extensions;
    fs::path themes;

    explicit Kit(const std::string& name) {
        fs::path root = fs::temp_directory_path() / "amk-tests" / ("theme-kit-" + name);
        fs::remove_all(root);
        extensions = root / "extensions";
        themes = root / "themes";
        fs::create_directories(extensions);
        fs::create_directories(themes / "neon");
        fs::create_directories(themes / "plain");
        std::ofstream(themes / "neon" / "manifest.json")
            << R"({"id":"neon","name":"Néon","version":"1.0.0","kind":"theme","author":"Shirowwww",)"
            << R"("description":"Test.","kitApi":"1.0","main":"main.js","colors":"colors.ini","permissions":[]})";
        std::ofstream(themes / "neon" / "main.js") << R"(AMK.log("script du thème");)";
        std::ofstream(themes / "neon" / "colors.ini") << "[Base]\nAccentFillColorDefaultBrush = 0A84FF\n";
        std::ofstream(themes / "plain" / "colors.ini") << "[Base]\nAccentFillColorDefaultBrush = FFFFFF\n";
    }
};

struct Setup {
    Kit kit;
    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal{out, [] { return std::string("t"); }};
    amk::ExtensionHost hosts{journal};
    amk::ExtensionLoader loader;
    amk::Settings settings;

    explicit Setup(const std::string& name, const std::string& process = "ui")
        : kit(name), loader(hosts, host, journal, kit.extensions) {
        host.process = process;
        loader.SetThemeRoot(kit.themes);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

}  // namespace

TEST_CASE(ThemeScript_RunsWhileItsThemeIsActive) {
    Setup s("active");
    amk::LoadReport report = s.loader.Discover();
    CHECK(std::find(report.loaded.begin(), report.loaded.end(), "neon") != report.loaded.end());

    s.settings.Set("theme.active", "neon");
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("neon") == amk::ExtensionState::Running);
    CHECK(s.Logged("[neon] script du thème"));
}

TEST_CASE(ThemeScript_StaysOffWhileAnotherThemeIsActive) {
    Setup s("other");
    s.loader.Discover();
    s.settings.Set("theme.active", "minuit");
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("neon") == amk::ExtensionState::Off);
    CHECK(!s.Logged("script du thème"));
}

TEST_CASE(ThemeScript_StaysOffWhenThePartIsSwitchedOff) {
    Setup s("part");
    s.loader.Discover();
    s.settings.Set("theme.active", "neon");
    s.settings.Set("theme.parts.script", "0");
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("neon") == amk::ExtensionState::Off);
}

TEST_CASE(ThemeScript_IsLoadedOnlyInAppleMusic) {
    Setup s("engine", "engine");
    amk::LoadReport report = s.loader.Discover();
    CHECK(std::find(report.loaded.begin(), report.loaded.end(), "neon") == report.loaded.end());
    CHECK(report.rejected.empty());
}

TEST_CASE(ThemeScript_AFolderWithoutScriptIsNotAnElement) {
    Setup s("plain-folder");
    amk::LoadReport report = s.loader.Discover();
    CHECK(std::find(report.loaded.begin(), report.loaded.end(), "plain") == report.loaded.end());
    CHECK(std::find(report.rejected.begin(), report.rejected.end(), "plain") == report.rejected.end());
}

TEST_CASE(ThemeScript_ThemeRootIsOptional) {
    Setup s("no-themes");
    amk::ExtensionLoader loader(s.hosts, s.host, s.journal, s.kit.extensions);
    amk::LoadReport report = loader.Discover();
    CHECK(report.loaded.empty());
}
