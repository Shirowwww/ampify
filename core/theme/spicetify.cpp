#include "spicetify.hpp"

#include <array>
#include <cctype>
#include <utility>

namespace amk::theme {

namespace {

// The keys of Spicetify 2's color.ini (spicetify.app/docs/development/themes).
constexpr std::array<std::string_view, 24> kKeys = {
    "text",       "subtext",       "main",         "main-elevated",  "main-transition",    "highlight",
    "highlight-elevated", "sidebar", "player",     "card",           "shadow",             "selected-row",
    "button",     "button-active", "button-disabled", "tab-active",  "notification",       "notification-error",
    "misc",       "nav-active",    "nav-active-text", "play-button", "play-button-active", "playback-bar"};

// Spicetify 1 names, still found in old themes, and the current key that plays their part.
constexpr std::array<std::pair<std::string_view, std::string_view>, 14> kAliases = {{
    {"main_fg", "text"},
    {"secondary_fg", "subtext"},
    {"main_bg", "main"},
    {"sidebar_and_player_bg", "sidebar"},
    {"cover_overlay_and_shadow", "shadow"},
    {"indicator_fg_and_button_bg", "button"},
    {"pressing_fg", "button-active"},
    {"slider_bg", "playback-bar"},
    {"sidebar_indicator_and_hover_button_bg", "highlight"},
    {"scrollbar_fg_and_selected_row_bg", "selected-row"},
    {"pressing_button_bg", "button-active"},
    {"selected_button", "button-active"},
    {"miscellaneous_bg", "misc"},
    {"miscellaneous_hover_bg", "highlight-elevated"},
}};

// One rule of the translation: the first key of `keys` the theme defines paints `property` of `selectors`. The
// names come from the inspector on Apple Music 1.1792 (knowledge/apple-music-xaml-tree.md). Text in the pages is left
// to TextFillColorPrimaryBrush (see SpicetifyResources): a rule there would also paint the captions on artwork.
struct Rule {
    std::string_view selectors;
    std::string_view property;
    std::array<std::string_view, 2> keys;
};

constexpr std::array<Rule, 7> kRules = {{
    {"ContentPage,\nSplitView#ContentSplitView > Grid > Grid#ContentRoot,\nGrid#PageRoot,\nNavigationView#NavView",
     "background", {"main", ""}},
    {"Grid#PaneRoot", "background", {"sidebar", ""}},
    {"BackDrop", "background", {"player", "sidebar"}},
    {"NowPlayingControl#LCD > Grid#LCD", "background", {"card", "main-elevated"}},
    {"Grid#PaneRoot TextBlock,\nTickerTapeControl#TrackTitleTickerTape", "color", {"text", ""}},
    {"TickerTapeControl#ArtistTitleTickerTape,\nTextBlock#CurrentTime,\nTextBlock#Duration", "color", {"subtext", ""}},
    {"ScrubberControl#LCDScrubber", "color", {"playback-bar", "button"}},
}};

std::string Lower(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

}  // namespace

std::string SpicetifyKey(std::string_view key) {
    std::string lower = Lower(key);
    for (std::string_view known : kKeys) {
        if (lower == known) return lower;
    }
    for (const auto& [old, current] : kAliases) {
        if (lower == old) return std::string(current);
    }
    return {};
}

const std::vector<SpicetifyResource>& SpicetifyResources() {
    static const std::vector<SpicetifyResource> resources = {
        {"text", "TextFillColorPrimaryBrush"},
        {"subtext", "TextFillColorSecondaryBrush"},
        {"main", "SolidBackgroundFillColorBaseBrush"},
        {"main", "ApplicationPageBackgroundThemeBrush"},
        {"card", "CardBackgroundFillColorDefaultBrush"},
        {"main-elevated", "ControlFillColorDefaultBrush"},
        {"button", "AccentFillColorDefaultBrush"},
        {"button-active", "AccentFillColorSecondaryBrush"},
        {"button-disabled", "AccentFillColorDisabledBrush"},
        {"highlight", "SubtleFillColorSecondaryBrush"},
        {"selected-row", "SubtleFillColorTertiaryBrush"},
        {"notification", "SystemFillColorAttentionBrush"},
        {"notification-error", "SystemFillColorCriticalBrush"},
    };
    return resources;
}

std::string SpicetifyStylesheet(const std::set<std::string>& defined) {
    std::string css;
    for (const Rule& rule : kRules) {
        for (std::string_view key : rule.keys) {
            if (key.empty() || !defined.count(std::string(key))) continue;
            css += std::string(rule.selectors) + " {\n    " + std::string(rule.property) + ": $" + std::string(key) + ";\n}\n";
            break;
        }
    }
    return css;
}

}  // namespace amk::theme
