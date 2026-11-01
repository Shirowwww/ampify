// Tests of Ampify.fonts as an extension sees it: the roles the theme service publishes (core/font_roles.hpp), and the
// defaults before it does.

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../core/font_roles.hpp"
#include "../core/manifest.hpp"
#include "../core/script_extension.hpp"
#include "../core/theme/fonts.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

struct Fixture {
    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};

    Fixture() { ampify::PublishFontRoles(""); }
    ~Fixture() { ampify::PublishFontRoles(""); }

    std::unique_ptr<ampify::ScriptExtension> Make(const std::string& id, const std::string& main) {
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("fonts-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[]})";
        std::ofstream(dir / "manifest.json") << manifest;
        std::ofstream(dir / "main.js") << main;
        ampify::ManifestResult result = ampify::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, host, journal);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

}  // namespace

TEST_CASE(Fonts_GiveTheWindowsRampBeforeAnyThemeIsPublished) {
    Fixture f;
    auto element = f.Make("ramp", R"(
        const title = Ampify.fonts.style("title");
        Ampify.log("title " + title.role + " " + title.family + " " + title.weight + " " + title.size + " " + title.fromTheme);
        Ampify.log("roles " + Object.keys(Ampify.fonts.roles()).join(","));)");
    CHECK(element->Start());
    CHECK(f.Logged("[ramp] title title Segoe UI Variable Display 600 28 false"));
    CHECK(f.Logged("[ramp] roles caption,body,body-strong,subtitle,title,title-large,display"));
}

TEST_CASE(Fonts_FollowWhatTheThemeServicePublishes) {
    Fixture f;
    ampify::theme::Theme theme;
    theme.fonts["title"].family = "Inter";
    theme.fonts["title"].size = "30";
    theme.fonts["lyrics"].family = "Georgia";
    ampify::PublishFontRoles(ampify::theme::FontRolesJson(&theme));

    auto element = f.Make("themed", R"(
        const title = Ampify.fonts.style("Title");
        Ampify.log("title " + title.family + " " + title.weight + " " + title.size + " " + title.fromTheme);
        Ampify.log("text " + Ampify.fonts.style("text").role + " " + Ampify.fonts.style("text").family);
        Ampify.log("own " + Ampify.fonts.roles().lyrics.family);
        Ampify.log("unknown " + Ampify.fonts.style("nothing"));
        try { Ampify.fonts.style(""); } catch (e) { Ampify.log("empty " + e.name); })");
    CHECK(element->Start());
    CHECK(f.Logged("[themed] title Inter 600 30 true"));
    CHECK(f.Logged("[themed] text body Segoe UI Variable Text"));
    CHECK(f.Logged("[themed] own Georgia"));
    CHECK(f.Logged("[themed] unknown null"));
    CHECK(f.Logged("[themed] empty TypeError"));

    ampify::PublishFontRoles(ampify::theme::FontRolesJson(nullptr));  // the theme removed
    auto after = f.Make("after", R"(Ampify.log("title " + Ampify.fonts.style("title").family);)");
    CHECK(after->Start());
    CHECK(f.Logged("[after] title Segoe UI Variable Display"));
}
