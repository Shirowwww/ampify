#include <algorithm>
#include <filesystem>
#include <map>
#include <string>

#include "../src/core/settings_catalog.hpp"
#include "../src/core/theme/files.hpp"
#include "../src/core/theme/retired.hpp"
#include "../src/core/theme/theme.hpp"
#include "test.hpp"

namespace th = ampify::theme;

namespace {

class Files final : public th::FileSource {
public:
    std::map<std::string, std::string> files;

    std::optional<std::string> Read(std::string_view path) override {
        auto it = files.find(std::string(path));
        return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    bool Exists(std::string_view path) override { return files.count(std::string(path)) > 0; }
};

constexpr const char* kPlacingCss = R"css(
@setting bar-position = Bottom {
    .playbar { opacity: 0.5; }
}
)css";

std::string Manifest(const std::string& extra) {
    return R"({"id": "test", "name": "Test", "version": "1.0.0", "kind": "theme", "author": "Shirowwww",
               "description": "A test.", "kitApi": "1.0")" + extra + "}";
}

th::Theme Load(Files& files, const std::string& playbar = "") {
    th::LoadOptions options;
    options.playbar = playbar;
    return th::LoadTheme("test", "C:/themes/test", files, options);
}

std::string Place(const th::Theme& theme) {
    auto it = theme.settings.find("bar-position");
    return it == theme.settings.end() ? "MISSING" : it->second;
}

ampify::Settings SettingsOf(std::initializer_list<std::pair<const char*, const char*>> values) {
    ampify::Settings settings;
    for (const auto& [key, value] : values) settings.Set(key, value);
    return settings;
}

}  // namespace

TEST_CASE(PlaybarPlace_TheChosenPlaceWinsOverTheThemes) {
    CHECK(th::PlaybarPlace("bottom", std::string("top")) == "Bottom");
    CHECK(th::PlaybarPlace("Centred", std::nullopt) == "Centred");
}

TEST_CASE(PlaybarPlace_LeftToTheThemeTakesItsPlaceOrTheTop) {
    CHECK(th::PlaybarPlace("theme", std::string("bottom")) == "Bottom");
    CHECK(th::PlaybarPlace("", std::string("centred")) == "Centred");
    CHECK(th::PlaybarPlace("theme", std::nullopt) == "Top");
    CHECK(th::PlaybarPlace("sideways", std::nullopt) == "Top");
}

TEST_CASE(PlaybarPlace_EveryThemeReadsItAsASetting) {
    Files files;
    files.files["manifest.json"] = Manifest(R"(, "playbar": "bottom")");
    files.files["user.css"] = kPlacingCss;
    CHECK(Place(Load(files)) == "Bottom");
    CHECK(Place(Load(files, "top")) == "Top");
    CHECK(Load(files).variables.at("setting-bar-position") == "Bottom");
}

TEST_CASE(PlaybarPlace_AnOldThemesOwnSettingStandsUntilOneIsChosen) {
    Files files;
    files.files["manifest.json"] = Manifest(R"(, "settings": [{"key": "bar-position", "label": "Bar", "type": "choice",
        "options": ["Top", "Bottom", "Centred"], "default": "Centred"}])");
    files.files["user.css"] = kPlacingCss;
    th::Theme theme = Load(files);
    CHECK(Place(theme) == "Centred");
    CHECK(theme.problems.empty());
    CHECK(Place(Load(files, "bottom")) == "Bottom");
}

TEST_CASE(PlaybarPlace_ThemeThatPlacesTheBarGetsNoLayoutOfAmpifys) {
    Files files;
    files.files["manifest.json"] = Manifest("");
    files.files["user.css"] = kPlacingCss;
    th::Theme theme = Load(files, "bottom");
    CHECK(theme.placesPlaybar);
    th::LoadOptions options;
    options.playbar = "bottom";
    CHECK(!th::LoadPlaybarLayout(&theme, options).has_value());
}

TEST_CASE(PlaybarPlace_ThemeThatDoesNotPlaceItGetsAmpifysBottomBar) {
    Files files;
    files.files["manifest.json"] = Manifest("");
    files.files["user.css"] = ".playbar { opacity: 0.5; }";
    th::LoadOptions options;
    options.playbar = "centred";
    th::Theme theme = th::LoadTheme("test", "C:/themes/test", files, options);
    CHECK(!theme.placesPlaybar);
    std::optional<th::Theme> layout = th::LoadPlaybarLayout(&theme, options);
    CHECK(layout.has_value());
    if (!layout) return;
    CHECK(layout->usable);
    CHECK(layout->problems.empty());
    bool moved = false;  // centre-bar moves the progress line under the buttons
    for (const th::Assignment& assignment : layout->assignments) moved = moved || assignment.property == "MoveTo";
    CHECK(moved);
}

TEST_CASE(PlaybarPlace_NoLayoutWhileTheBarStaysAtTheTop) {
    th::LoadOptions options;
    CHECK(!th::LoadPlaybarLayout(nullptr, options).has_value());
    options.playbar = "top";
    CHECK(!th::LoadPlaybarLayout(nullptr, options).has_value());
}

TEST_CASE(PlaybarPlace_WithNoThemeTheChosenPlaceBringsTheLayout) {
    th::LoadOptions options;
    options.playbar = "bottom";
    std::optional<th::Theme> layout = th::LoadPlaybarLayout(nullptr, options);
    CHECK(layout.has_value() && layout->usable && !layout->assignments.empty());
}

TEST_CASE(PlaybarPlace_ASnippetSeesTheThemesPlace) {
    Files themeFiles;
    themeFiles.files["manifest.json"] = Manifest(R"(, "playbar": "centred")");
    themeFiles.files["user.css"] = kPlacingCss;
    th::Theme theme = Load(themeFiles);
    Files snippetFiles;
    snippetFiles.files["user.css"] = kPlacingCss;
    th::Theme snippet = th::LoadSnippet("s", "C:/snippets/s", snippetFiles, &theme, {});
    CHECK(Place(snippet) == "Centred");
    CHECK(snippet.problems.empty());
}

TEST_CASE(PlaybarPlace_BundledThemesLoadAtEveryPlace) {
    const std::filesystem::path root = std::filesystem::path(AMPIFY_SOURCE_DIR) / "themes";
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const std::string name = entry.path().filename().string();
        if (name == "snippets") continue;
        for (const char* place : {"top", "bottom", "centred"}) {
            th::FolderFiles files(entry.path());
            th::LoadOptions options;
            options.playbar = place;
            th::Theme theme = th::LoadTheme(name, entry.path().generic_string(), files, options);
            CHECK(theme.usable);
            for (const std::string& problem : theme.problems) {
                if (problem.find("bar-position") != std::string::npos) {
                    ampify::test::ReportFailure(__FILE__, __LINE__, name + " " + place + ": " + problem);
                }
            }
        }
    }
}

TEST_CASE(RetiredSettings_TheBottomBarSnippetBecomesThePlace) {
    ampify::Settings settings = SettingsOf(
        {{"snippets.bottom-playbar.enabled", "1"}, {"snippets.bottom-playbar.centred", "true"}, {"theme.active", "glass"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(settings.GetString(ampify::kPlaybarKey, "") == "centred");
    CHECK(!settings.Get("snippets.bottom-playbar.enabled"));
    CHECK(!settings.Get("snippets.bottom-playbar.centred"));
}

TEST_CASE(RetiredSettings_AThemesOwnPlaceCarriesOverOnlyWhenChosen) {
    ampify::Settings moved = SettingsOf({{"theme.active", "palettes"}, {"themes.palettes.bar-position", "Bottom"}});
    CHECK(th::MoveRetiredSettings(moved));
    CHECK(moved.GetString(ampify::kPlaybarKey, "") == "bottom");
    CHECK(!moved.Get("themes.palettes.bar-position"));

    // Glass's default: the theme's own place already says so.
    ampify::Settings kept = SettingsOf({{"theme.active", "glass"}, {"themes.glass.bar-position", "Bottom"}});
    CHECK(th::MoveRetiredSettings(kept));
    CHECK(!kept.Get(ampify::kPlaybarKey));
    CHECK(!kept.Get("themes.glass.bar-position"));
}

// A theme of someone else's still declares bar-position, and the engine reads it while apple.playbar is "theme".
TEST_CASE(RetiredSettings_AnotherAuthorsThemeKeepsItsOwnPlace) {
    ampify::Settings settings = SettingsOf(
        {{"apple.playbar", "theme"}, {"theme.active", "nocturne"}, {"themes.nocturne.bar-position", "Bottom"}});
    CHECK(!th::MoveRetiredSettings(settings));
    CHECK(settings.GetString("themes.nocturne.bar-position", "") == "Bottom");
}

// Greenroom's and Palettes' own badge setting went: Declutter's badges and explicit settings take over when it was on.
TEST_CASE(RetiredSettings_AThemesHiddenBadgesMoveToTheSnippets) {
    ampify::Settings settings = SettingsOf({{"theme.active", "palettes"}, {"themes.palettes.hide-badges", "true"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(!settings.Get("themes.palettes.hide-badges"));
    CHECK(settings.GetBool("snippets.declutter.enabled", false));
    CHECK(settings.GetBool("snippets.declutter.badges", false));
    CHECK(settings.GetBool("snippets.declutter.explicit", false));  // Hide explicit is part of Declutter

    ampify::Settings off = SettingsOf({{"theme.active", "greenroom"}, {"themes.greenroom.hide-badges", "false"}});
    CHECK(th::MoveRetiredSettings(off));
    CHECK(!off.Get("themes.greenroom.hide-badges"));
    CHECK(!off.Get("snippets.declutter.enabled"));
    CHECK(!off.Get("snippets.declutter.explicit"));
}

TEST_CASE(RetiredSettings_AChosenPlaceStays) {
    ampify::Settings settings = SettingsOf({{"apple.playbar", "top"}, {"snippets.bottom-playbar.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(settings.GetString(ampify::kPlaybarKey, "") == "top");
}

TEST_CASE(RetiredSettings_EmberBecomesArtworkWithItsBarAndAccent) {
    ampify::Settings settings = SettingsOf({{"theme.active", "ember"},
                                            {"themes.ember.accent", "Artwork"},
                                            {"themes.ember.veil", "0.95"},
                                            {"themes.ember.gap", "12"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(settings.GetString("theme.active", "") == "artwork");
    CHECK(settings.GetString(ampify::kPlaybarKey, "") == "bottom");  // Ember's bar was at the bottom
    CHECK(settings.GetString("themes.artwork.art-accent", "") == "true");
    CHECK(settings.GetString("themes.artwork.veil", "") == "0.9");
    CHECK(settings.GetString("themes.artwork.gap", "") == "12");
    CHECK(!settings.Get("themes.ember.accent"));
}

TEST_CASE(RetiredSettings_HighContrastGoesBackToNoTheme) {
    ampify::Settings settings = SettingsOf({{"theme.active", "high-contrast"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(ampify::ActiveThemeId(settings).empty());
}

TEST_CASE(RetiredSettings_AnInactiveMergedThemeLeavesItsHeirAlone) {
    ampify::Settings settings = SettingsOf({{"theme.active", "none"},
                                            {"themes.ember.veil", "0.6"},
                                            {"themes.artwork.veil", "0.8"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(settings.GetString("themes.artwork.veil", "") == "0.8");
    CHECK(!settings.Get("themes.ember.veil"));
}

TEST_CASE(RetiredSettings_EveryRetiredElementHasItsFolder) {
    std::vector<std::string> folders = th::RetiredFolders();
    CHECK(folders.size() == th::RetiredElements().size());
    CHECK(std::find(folders.begin(), folders.end(), "snippets/bottom-playbar") != folders.end());
    CHECK(std::find(folders.begin(), folders.end(), "themes/ember") != folders.end());
}

TEST_CASE(RetiredSettings_NothingToMoveChangesNothing) {
    ampify::Settings settings = SettingsOf({{"theme.active", "glass"}, {"themes.glass.tint", "Artwork"},
                                            {"snippets.album-colors.moving-background", "false"}});
    CHECK(!th::MoveRetiredSettings(settings));
}

// 1.3.3: Glass's moving artwork, on by default, is Artwork colours' moving background under every theme.
TEST_CASE(RetiredSettings_GlassMovingArtworkBecomesTheSnippetsMovingBackground) {
    ampify::Settings unset = SettingsOf({{"theme.active", "glass"}});
    CHECK(th::MoveRetiredSettings(unset));
    CHECK(unset.GetString("snippets.album-colors.enabled", "") == "1");
    CHECK(unset.GetString("snippets.album-colors.moving-background", "") == "true");
    // The snippet was off: its pages and glow stay as they were, off.
    CHECK(unset.GetString("snippets.album-colors.reach", "") == "Off");
    CHECK(unset.GetString("snippets.album-colors.glow", "") == "Off");
    // Turned off afterwards, it stays off at the next start.
    unset.Set("snippets.album-colors.moving-background", "false");
    CHECK(!th::MoveRetiredSettings(unset));

    // Already in use: its choices stay.
    ampify::Settings used = SettingsOf({{"theme.active", "artwork"}, {"themes.glass.moving-artwork", "true"},
                                        {"snippets.album-colors.enabled", "1"},
                                        {"snippets.album-colors.glow", "Display"}});
    CHECK(th::MoveRetiredSettings(used));
    CHECK(used.GetString("snippets.album-colors.glow", "") == "Display");
    CHECK(used.GetString("snippets.album-colors.moving-background", "") == "true");
    CHECK(!used.Get("themes.glass.moving-artwork"));

    // Turned off in Glass, or not the active theme and never set: nothing moves to the snippet.
    ampify::Settings off = SettingsOf({{"theme.active", "glass"}, {"themes.glass.moving-artwork", "false"}});
    CHECK(th::MoveRetiredSettings(off));
    CHECK(!off.Get("snippets.album-colors.enabled"));
    ampify::Settings other = SettingsOf({{"theme.active", "cinema"}});
    CHECK(!th::MoveRetiredSettings(other));
}
