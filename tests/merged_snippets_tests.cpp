#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/settings.hpp"
#include "../src/core/theme/retired.hpp"
#include "release_facts.hpp"
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

TEST_CASE(MergedSnippets_CoverAmbienceAloneGlowsWithoutThePageWash) {
    ampify::Settings settings = SettingsOf({{"snippets.cover-ambience.enabled", "1"}, {"snippets.cover-ambience.reach", "Bar"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(Value(settings, "snippets.album-colors.enabled") == "1");
    CHECK(Value(settings, "snippets.album-colors.reach") == "Off");
    CHECK(Value(settings, "snippets.album-colors.glow") == "Bar");
}

TEST_CASE(MergedSnippets_AnAlbumColorsUserKeepsTheWashAndGainsTheGlow) {
    ampify::Settings settings = SettingsOf({{"snippets.album-colors.enabled", "1"},
                                            {"snippets.album-colors.reach", "Page"},
                                            {"snippets.cover-ambience.enabled", "1"},
                                            {"snippets.animated-artwork.enabled", "1"},
                                            {"snippets.animated-artwork.strength", "0.6"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(Value(settings, "snippets.album-colors.reach") == "Page");  // whenNew rows leave a heir in use alone
    CHECK(Value(settings, "snippets.album-colors.glow") == "Display");
    CHECK(Value(settings, "snippets.album-colors.moving-background") == "true");
    CHECK(Value(settings, "snippets.album-colors.background-strength") == "0.6");
}

TEST_CASE(MergedSnippets_HoverSearchAloneHidesNothingElse) {
    ampify::Settings settings = SettingsOf({{"snippets.hover-search.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(settings));
    CHECK(Value(settings, "snippets.sidebar-style.enabled") == "1");
    CHECK(Value(settings, "snippets.sidebar-style.search-on-hover") == "true");
    CHECK(Value(settings, "snippets.sidebar-style.compact") == "false");  // Apple's rows, as they were

    ampify::Settings both = SettingsOf({{"snippets.sidebar-style.enabled", "1"}, {"snippets.hover-search.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(both));
    CHECK(Value(both, "snippets.sidebar-style.compact") == "(none)");
    CHECK(Value(both, "snippets.sidebar-style.search-on-hover") == "true");
}

TEST_CASE(MergedSnippets_PlaybarTimesKeepsApplesLineAndItsChoice) {
    ampify::Settings alone = SettingsOf({{"snippets.playbar-times.enabled", "1"}, {"snippets.playbar-times.show", "Always"}});
    CHECK(th::MoveRetiredSettings(alone));
    CHECK(Value(alone, "snippets.progress-line.enabled") == "1");
    CHECK(Value(alone, "snippets.progress-line.custom-thickness") == "false");
    CHECK(Value(alone, "snippets.progress-line.times") == "Always");

    ampify::Settings withThickness = SettingsOf({{"snippets.playbar-times.enabled", "1"},
                                                 {"snippets.progress-thickness.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(withThickness));
    CHECK(Value(withThickness, "snippets.progress-line.custom-thickness") == "true");  // the thickness snippet wins
    CHECK(Value(withThickness, "snippets.progress-line.times") == "Under the pointer");
}

TEST_CASE(MergedSnippets_FocusedLyricsBecomesTheFocusOfLyricsStyle) {
    ampify::Settings alone = SettingsOf({{"snippets.focused-lyrics.enabled", "1"}, {"snippets.focused-lyrics.strength", "Strong"}});
    CHECK(th::MoveRetiredSettings(alone));
    CHECK(Value(alone, "snippets.lyrics-style.enabled") == "1");
    CHECK(Value(alone, "snippets.lyrics-style.align") == "Left");  // as Apple had them
    CHECK(Value(alone, "snippets.lyrics-style.focus") == "Strong");

    ampify::Settings centred = SettingsOf({{"snippets.focused-lyrics.enabled", "1"}, {"snippets.centred-lyrics.enabled", "1"}});
    CHECK(th::MoveRetiredSettings(centred));
    CHECK(Value(centred, "snippets.lyrics-style.align") == "Centred");
    CHECK(Value(centred, "snippets.lyrics-style.focus") == "Medium");
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

namespace {

// A kit holding only these elements.
th::InstalledCheck KitWith(std::initializer_list<ampify::ElementRef> present) {
    std::vector<ampify::ElementRef> elements(present);
    return [elements](const ampify::ElementRef& element) {
        return std::find(elements.begin(), elements.end(), element) != elements.end();
    };
}

const ampify::ElementRef kLyricsStyle{ampify::ElementKind::Snippet, "lyrics-style"};

}  // namespace

TEST_CASE(Retired_ASnippetInUseWaitsForItsReplacementWhileItIsMissing) {
    ampify::Settings settings =
        SettingsOf({{"snippets.centred-lyrics.enabled", "1"}, {"snippets.hide-explicit.enabled", "0"}});
    std::vector<const th::RetiredElement*> waiting = th::AwaitingReplacement(settings, KitWith({}));
    CHECK(waiting.size() == 1);
    CHECK(waiting.front()->id == "centred-lyrics");

    CHECK(th::MoveRetiredSettings(settings, waiting));  // the snippet switched off still goes
    CHECK(Value(settings, "snippets.centred-lyrics.enabled") == "1");
    CHECK(!settings.Get("snippets.lyrics-style.enabled"));
    CHECK(!settings.Get("snippets.hide-explicit.enabled"));
    std::vector<std::string> folders = th::RetiredFolders(waiting);
    CHECK(std::find(folders.begin(), folders.end(), "snippets/centred-lyrics") == folders.end());
    CHECK(std::find(folders.begin(), folders.end(), "snippets/hide-explicit") != folders.end());
}

TEST_CASE(Retired_OnceItsReplacementIsInstalledTheSnippetMoves) {
    ampify::Settings settings = SettingsOf({{"snippets.centred-lyrics.enabled", "1"}});
    std::vector<const th::RetiredElement*> waiting = th::AwaitingReplacement(settings, KitWith({kLyricsStyle}));
    CHECK(waiting.empty());
    CHECK(th::MoveRetiredSettings(settings, waiting));
    CHECK(Value(settings, "snippets.lyrics-style.enabled") == "1");
    CHECK(Value(settings, "snippets.lyrics-style.align") == "Centred");
}

TEST_CASE(Retired_TheReplacementsToInstallAreThoseInUseAndMissing) {
    ampify::Settings before = SettingsOf({{"snippets.dark-lyrics.enabled", "1"},
                                          {"snippets.centred-lyrics.enabled", "1"},
                                          {"snippets.compact-sidebar.enabled", "0"},
                                          {"theme.active", "ember"}});
    std::vector<ampify::ElementRef> wanted = th::ReplacementsToInstall(before, KitWith({}));
    CHECK(wanted.size() == 2);  // lyrics-style once, though two snippets wait for it; not sidebar-style
    CHECK(std::find(wanted.begin(), wanted.end(), kLyricsStyle) != wanted.end());
    const ampify::ElementRef artwork{ampify::ElementKind::Theme, "artwork"};
    CHECK(std::find(wanted.begin(), wanted.end(), artwork) != wanted.end());
    CHECK(th::ReplacementsToInstall(before, KitWith({kLyricsStyle, artwork})).empty());

    // Moved by a version that did not install it: switched on, but not in the kit.
    ampify::Settings moved = SettingsOf({{"snippets.lyrics-style.enabled", "1"}});
    CHECK(th::ReplacementsToInstall(moved, KitWith({})) == std::vector<ampify::ElementRef>{kLyricsStyle});
    CHECK(th::ReplacementsToInstall(SettingsOf({{"snippets.lyrics-style.enabled", "0"}}), KitWith({})).empty());
}

TEST_CASE(Retired_TheOldDefaultCatalogFollowsTheNewOneAndAChosenOneStays) {
    const char* old = "https://raw.githubusercontent.com/Shirowwww/ampify-catalog/main/catalog.json";
    ampify::Settings saved = SettingsOf({{"catalog.url", old}});
    CHECK(th::MoveRetiredSettings(saved));
    CHECK(Value(saved, "catalog.url") == std::string(ampify::kDefaultCatalogUrl));
    CHECK(Value(saved, "catalog.url") != old);

    ampify::Settings chosen = SettingsOf({{"catalog.url", "https://example.org/catalog.json"}});
    CHECK(!th::MoveRetiredSettings(chosen));
    CHECK(Value(chosen, "catalog.url") == "https://example.org/catalog.json");
    ampify::Settings none;
    CHECK(!th::MoveRetiredSettings(none));
}

TEST_CASE(Retired_IsRetiredNamesOnlyTheRetiredElementOfItsKind) {
    // The Marketplace drops these from a catalog published before 1.3.0.
    CHECK(th::IsRetired(ampify::ElementKind::Theme, "ember"));
    CHECK(th::IsRetired(ampify::ElementKind::Snippet, "hide-buttons"));
    CHECK(!th::IsRetired(ampify::ElementKind::Snippet, "ember"));
    CHECK(!th::IsRetired(ampify::ElementKind::Theme, "artwork"));
}
