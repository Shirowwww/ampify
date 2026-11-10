#pragma once
// What theme.cpp, theme_colours.cpp and theme_derived.cpp share: theme.cpp reads a theme's files and rules.

#include "theme.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>
#include "../json.hpp"
#include "../manifest.hpp"
#include "../text_util.hpp"
#include "builtins.hpp"
#include "colour_keys.hpp"
#include "css_values.hpp"
#include "fonts.hpp"
#include "glass.hpp"
#include "ini.hpp"
#include "release_facts.hpp"
#include "scheme_choice.hpp"
#include "spicetify.hpp"

namespace ampify::theme::detail {

constexpr const char* kAccent = "AccentFillColorDefaultBrush";

// Where the colours of a scheme go: the palette, which $name and the derived rules read, and the app's brushes when
// the colours part is on. Both change together.
struct Colours {
    Theme& theme;
    bool applied;

    bool Define(std::string_view key, Argb color) {
        if (!theme.palette.Define(key, color)) return false;
        AddBrush(key, color);
        return true;
    }
    void Set(std::string_view key, Argb color) {
        theme.palette.Set(key, color);
        AddBrush(key, color);
    }

private:
    void AddBrush(std::string_view key, Argb color) {
        if (applied) theme.resources.push_back({std::string(key), {ValueKind::Color, ToXamlColor(color)}});
    }
};

// What the rules of one file need besides the theme they fill.
struct RuleSource {
    std::string file = "user.css";  // for the log and the problems
    const Theme* base = nullptr;    // the active theme, for a snippet: its font roles fill what the snippet leaves out
    int layer = 0;                  // -1 for the rules translated from color.ini: the theme's user.css wins over them
    bool applied = true;            // false when its part is off: read for its problems only
};

// Their rules have a source of their own, so that a composed theme can work the room out again for all its layers.
constexpr const char* kFixedSlotSource = "Ampify (room in a slot Apple fixes)";

// The files of a theme that has none.
class NoFiles final : public FileSource {
public:
    std::optional<std::string> Read(std::string_view) override { return std::nullopt; }
    bool Exists(std::string_view) override { return false; }
};

// The colours file: colors.ini, or color.ini as Spicetify names it. Empty name when there is none.
std::pair<std::string, std::optional<std::string>> ReadColorsFile(FileSource& files);

// The colours a theme leaves to Ampify, once its own resources.xaml is known.
void DefineDerivedColors(Colours& colours);

std::set<std::string> LoadColors(FileSource& files, const std::string& wanted, Colours& colours);

// theme.accent over the scheme: its accent and Spicetify's button and playback bar take the chosen colour. The colours a
// scheme gives along with its own accent (hover and pressed accents, text on the accent, links) would clash with it:
// they go, and Ampify derives them from the chosen accent (DefineDerivedColors). Keys of the theme's resources.xaml
// stay: Ampify does not rewrite a theme's own XAML.
void ChooseAccent(Colours& colours, Argb accent);

// Returns false when the theme must be refused. Refusing is the rule the API sets: a bad key breaks the theme.
bool LoadDictionary(FileSource& files, const LoadOptions& options, Theme& theme);

void LoadRuleText(const std::string& css, const ValueContext& context, Theme& theme, const RuleSource& source);

bool Sets(const Theme& theme, std::string_view selector, std::string_view property);

// The room a slot needs for its decors and its track, moved down so the track's bottom stays where Apple's is.
std::string RoomInFixedSlots(const Theme& theme, const Theme* layout = nullptr);

std::string SmoothProgressLine(const Theme& theme);
std::string SuggestionsAgainstTheBox(const Theme& theme);

// Rules a theme's own choices call for, below its user.css so that a theme which sets the same thing keeps its value.
void AddDerivedRules(const ValueContext& context, Theme& theme, const ThemeParts& parts);

}  // namespace ampify::theme::detail
