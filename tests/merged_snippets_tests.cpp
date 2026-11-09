#include <filesystem>
#include <string>
#include <utility>

#include "../src/core/settings.hpp"
#include "../src/core/theme/retired.hpp"
#include "test.hpp"

namespace th = ampify::theme;

namespace {

ampify::Settings SettingsOf(std::initializer_list<std::pair<const char*, const char*>> values) {
    ampify::Settings settings;
    for (const auto& [key, value] : values) settings.Set(key, value);
    return settings;
}

std::string Value(const ampify::Settings& settings, const char* key) { return settings.GetString(key, "(none)"); }

}  // namespace

TEST_CASE(MergedSnippets_HideExplicitBecomesDeclutterWithTheMarkHidden) {
    ampify::Settings settings = SettingsOf({{"snippets.hide-explicit.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(Value(settings, "snippets.declutter.enabled") == "1");
    CHECK(Value(settings, "snippets.declutter.explicit") == "true");
    CHECK(!settings.Get("snippets.hide-explicit.enabled"));
}

TEST_CASE(MergedSnippets_SquareCornersBecomeTheSquareShapeOfCorners) {
    ampify::Settings settings = SettingsOf({{"snippets.square-corners.enabled", "1"}, {"snippets.round-corners.radius", "10"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(Value(settings, "snippets.round-corners.enabled") == "1");
    CHECK(Value(settings, "snippets.round-corners.shape") == "Square");
    CHECK(Value(settings, "snippets.round-corners.radius") == "10");  // the heir's own setting stays
}

TEST_CASE(MergedSnippets_HideButtonsKeepsItsDefaultsAndItsChoices) {
    ampify::Settings byDefault = SettingsOf({{"snippets.hide-buttons.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(byDefault));
    CHECK(Value(byDefault, "snippets.declutter.download-button") == "true");  // Hide buttons' own default
    CHECK(Value(byDefault, "snippets.declutter.lyrics-button") == "(none)");

    ampify::Settings chosen = SettingsOf({{"snippets.hide-buttons.enabled", "1"},
                                          {"snippets.hide-buttons.download", "false"},
                                          {"snippets.hide-buttons.lyrics", "true"}});
    CHECK(th::MoveRetiredSettings(chosen));
    CHECK(Value(chosen, "snippets.declutter.download-button") == "false");
    CHECK(Value(chosen, "snippets.declutter.lyrics-button") == "true");
}

TEST_CASE(MergedSnippets_TheThreeLyricsSnippetsBecomeOneStyle) {
    ampify::Settings all = SettingsOf({{"snippets.centred-lyrics.enabled", "1"},
                                       {"snippets.lyrics-spotlight.enabled", "1"},
                                       {"snippets.dark-lyrics.enabled", "1"},
                                       {"snippets.dark-lyrics.style", "Artwork"}});
    CHECK(th::MoveRetiredSettings(all));
    CHECK(Value(all, "snippets.lyrics-style.enabled") == "1");
    CHECK(Value(all, "snippets.lyrics-style.align") == "Centred");  // centred wins over the others' left
    CHECK(Value(all, "snippets.lyrics-style.spotlight") == "true");
    CHECK(Value(all, "snippets.lyrics-style.background") == "Artwork");

    ampify::Settings spotlight = SettingsOf({{"snippets.lyrics-spotlight.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(spotlight));
    CHECK(Value(spotlight, "snippets.lyrics-style.align") == "Left");  // as Apple had them
    CHECK(Value(spotlight, "snippets.lyrics-style.background") == "(none)");
}

TEST_CASE(MergedSnippets_AMascotAloneKeepsApplesLineThickness) {
    ampify::Settings mascot = SettingsOf({{"snippets.progress-mascot.enabled", "1"}, {"snippets.progress-mascot.mascot", "Note"}});
    CHECK(th::MoveRetiredSettings(mascot));
    CHECK(Value(mascot, "snippets.progress-line.custom-thickness") == "false");
    CHECK(Value(mascot, "snippets.progress-line.mascot") == "Note");

    ampify::Settings both = SettingsOf({{"snippets.progress-mascot.enabled", "1"},
                                        {"snippets.progress-thickness.enabled", "1"},
                                        {"snippets.progress-thickness.thickness", "4"}});
    CHECK(th::MoveRetiredSettings(both));
    CHECK(Value(both, "snippets.progress-line.custom-thickness") == "true");
    CHECK(Value(both, "snippets.progress-line.thickness") == "4");
    CHECK(Value(both, "snippets.progress-line.mascot") == "Comet");  // the mascot's own default
}

TEST_CASE(MergedSnippets_SidebarIconsAloneKeepApplesRows) {
    ampify::Settings icons = SettingsOf({{"snippets.sidebar-icons.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(icons));
    CHECK(Value(icons, "snippets.sidebar-style.compact") == "false");
    CHECK(Value(icons, "snippets.sidebar-style.icons") == "Round");

    ampify::Settings both = SettingsOf({{"snippets.sidebar-icons.enabled", "1"},
                                        {"snippets.sidebar-icons.shape", "Square"},
                                        {"snippets.compact-sidebar.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(both));
    CHECK(Value(both, "snippets.sidebar-style.compact") == "true");
    CHECK(Value(both, "snippets.sidebar-style.icons") == "Square");
}

TEST_CASE(MergedSnippets_ASnippetSwitchedOffLeavesItsHeirAlone) {
    ampify::Settings settings = SettingsOf({{"snippets.compact-sidebar.enabled", "0"}, {"snippets.hover-row-actions.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(!settings.Get("snippets.sidebar-style.enabled"));
    CHECK(!settings.Get("snippets.compact-sidebar.enabled"));
    CHECK(!settings.Get("snippets.hover-row-actions.enabled"));  // retired: its keys go
    CHECK(!th::MoveRetiredSettings(settings));  // once moved, nothing more to do
}

// Every heir ships in the repository, and none of the elements retired or merged still does.
TEST_CASE(MergedSnippets_TheHeirsShipAndTheMergedElementsDoNot) {
    const std::filesystem::path themes = std::filesystem::path(AMPIFY_SOURCE_DIR) / "themes";
    for (const th::RetiredElement& element : th::RetiredElements()) {
        // The table holds retired themes too (Ember, High contrast): each kind has its own folder.
        const std::filesystem::path folder = element.kind == ampify::ElementKind::Snippet ? themes / "snippets" : themes;
        CHECK(!std::filesystem::exists(folder / std::string(element.id)));
        if (!element.into.empty()) CHECK(std::filesystem::exists(folder / std::string(element.into) / "manifest.json"));
    }
}

TEST_CASE(Retired_IsRetiredNamesOnlyTheRetiredElementOfItsKind) {
    // The Marketplace drops these from a catalog published before 1.3.0.
    CHECK(th::IsRetired(ampify::ElementKind::Theme, "ember"));
    CHECK(th::IsRetired(ampify::ElementKind::Snippet, "hide-buttons"));
    CHECK(!th::IsRetired(ampify::ElementKind::Snippet, "ember"));
    CHECK(!th::IsRetired(ampify::ElementKind::Theme, "artwork"));
}
