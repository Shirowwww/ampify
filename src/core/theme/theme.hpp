#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "animation.hpp"
#include "color.hpp"
#include "css.hpp"
#include "files.hpp"
#include "properties.hpp"
#include "selector.hpp"
#include "tree.hpp"

namespace ampify::theme {

// One declaration for one selector, with its value already converted. The engine applies these.
struct Assignment {
    Selector selector;     // selector.hover: a :hover rule, applied while the pointer is over the element
    Specificity specificity;
    std::string property;  // XAML name, such as "Background"
    Value value;
    bool important = false;
    int order = 0;         // position in the stylesheet: a later declaration wins at equal weight
    int line = 0;
    int layer = 0;         // 0 for the theme, then one per snippet in id order, then sidebar.json and home.json
    std::string source = "user.css";  // the file, for the log: user.css, snippets/<id>/user.css, sidebar.json
    std::optional<MediaQuery> media;  // applies only while the window matches it (@media)
};

// True when `candidate` should replace `current` for the same element and property. !important first, then the layer
// (a snippet beats the theme, sidebar.json beats both), then specificity, then the place in the file.
inline bool Beats(const Assignment& candidate, const Assignment& current) {
    if (candidate.important != current.important) return candidate.important;
    if (candidate.layer != current.layer) return candidate.layer > current.layer;
    if (candidate.specificity != current.specificity) return current.specificity < candidate.specificity;
    return candidate.order > current.order;
}

// One font role (title, body...), as user.css sets it with --font-<role>, --font-<role>-weight, --font-<role>-size.
// Each part is the converted value (FontFamily, FontWeight, FontSize), empty when the theme does not set it.
struct FontRole {
    std::string family;
    std::string weight;
    std::string size;
};

// A brush written to an application resource, such as an entry of the app's dictionaries.
struct ResourceOverride {
    std::string key;
    Value value;
};

// The parts of a theme that are applied, as in docs/themes/format.md, "Theme parts" (theme.parts.*).
//   colors     the colours of colors.ini replace the app's brushes
//   resources  resources.xaml and the rules of user.css (Spicetify's inject_css)
//   assets     images in url(...) (Spicetify's overwrite_assets)
//   script     main.js runs in AppleMusic.exe while the theme is the active one (the loader starts it)
struct ThemeParts {
    bool colors = true;
    bool resources = true;
    bool assets = true;
    bool script = true;

    bool operator==(const ThemeParts&) const = default;
};

// How much glass the themes may ask of the graphics card (theme.glass, see glass.hpp): every load applies it.
enum class GlassLevel { Full, Balanced, Eco };

// What the window shows behind the panels a theme leaves see-through: Windows 11's Mica (the desktop's colours, faint),
// its darker Mica Alt, or the blur of the windows behind (acrylic).
enum class WindowMaterial { Mica, MicaAlt, Acrylic };

// The material of a --window-material value (mica | mica-alt | acrylic), ignoring case and spaces. None for `none`,
// an empty value or an unknown one: Apple Music's own window then stays.
std::optional<WindowMaterial> WindowMaterialOf(std::string_view value);

// What the loader needs beyond the files themselves.
struct LoadOptions {
    std::string scheme;                          // the [section] of colors.ini to use; empty takes the first one
    // Resource keys that resources.xaml may define (resource-keys.txt); null allows the known colour keys.
    const std::set<std::string>* allowedKeys = nullptr;
    ThemeParts parts;                            // a part that is off is read for its problems only, and not applied
    // The values settings.txt holds for the theme's declared settings (themes.<id>.<key>), by key. A missing or
    // unfit value takes the manifest's default.
    std::map<std::string, std::string> settings;
    // Colours of the artwork playing, as variables art-1, art-2, art-3 and art-text (ArtworkVariables in
    // color_extract.hpp). Empty before the first artwork: var(--art-1, fallback) then takes its fallback.
    std::map<std::string, std::string> artwork;
    // theme.accent: the accent the user chose, over the scheme's. What Ampify derives from the accent follows it.
    std::optional<Argb> accent;
    GlassLevel glass = GlassLevel::Full;
    // apple.playbar: top, bottom or centred for every theme; theme (or empty) leaves the place to the theme's manifest.
    std::string playbar;
};

// The id of the theme LoadAccentOnly makes: no theme folder can have it.
inline constexpr const char* kAccentOnlyId = "(accent)";

// A theme folder, read and checked. Layout, as in docs/extensions/api.md section 6.1:
//   colors.ini      one [section] per colour scheme, such as [Base] or [Dark]; `Key = hex` (with or without #)
//                   replaces the brush `Key` of the app. The same names can be used as $Key in user.css.
//   resources.xaml  a resource dictionary; it may only define keys from the allowed list, or the whole theme is refused
//   user.css        rules for elements, see css.hpp (extra layer, optional)
//   images/, templates/, fonts/   files that the rules refer to with url(...)
// A folder needs at least one of the three files to be a theme.
struct Theme {
    std::string id;  // the folder name, which is what settings store
    std::string name;
    std::string scheme;               // the scheme in use, empty when colors.ini is missing
    std::vector<std::string> schemes;  // every scheme found in colors.ini, in file order
    // Dark or light, from the background colour of the scheme (Spicetify's main, or the page brush), else from the
    // scheme's name (DarkByName). Unset when neither tells: Apple Music keeps its own mode, @media takes it as light.
    std::optional<bool> dark;
    GlassLevel glass = GlassLevel::Full;  // the level its values were lightened for

    // False when the folder is not a theme, or when resources.xaml breaks the allowed list. An unusable theme is never applied.
    bool usable = false;

    Palette palette;
    std::vector<ResourceOverride> resources;
    std::string resourceDictionary;  // the XAML of resources.xaml, merged into the app's resources; empty when none
    std::vector<Assignment> assignments;

    // The :root variables of user.css, by lowercase name without the leading "--", already resolved. Each declared
    // setting is one too, as setting-<key>.
    std::map<std::string, std::string> variables;
    // The value of each setting the manifest declares, by key (true or false for a toggle), for the @setting blocks.
    // bar-position is always there: Top, Bottom or Centred (PlaybarPlace).
    std::map<std::string, std::string> settings;
    // True when a rule of its user.css depends on @setting bar-position: the theme places the playback bar itself.
    bool placesPlaybar = false;
    // The font roles the theme sets itself, by canonical role name (see fonts.hpp).
    std::map<std::string, FontRole> fonts;
    // The @keyframes blocks of user.css by name, which `animation` runs. A snippet also sees those of the active theme.
    KeyframeSets keyframes;

    // Messages with the file and the line. Problems that do not stop the theme are listed here too.
    std::vector<std::string> problems;

    // The window material the theme asks for in :root (--window-material), as its glass level allows it.
    std::optional<WindowMaterial> Material() const;
};

// Reads and checks a theme. `folder` is the absolute path, used to resolve image paths. Each of the three loaders below
// lightens its result for options.glass (ApplyGlassLevel), which the result keeps in `glass`.
Theme LoadTheme(const std::string& id, const std::string& folder, FileSource& files, const LoadOptions& options = {});

// The place of the playback bar, as the @setting bar-position blocks name it: Top, Bottom or Centred. apple.playbar
// (`chosen`) when it names one, else the theme's own (`theirs`: its manifest's playbar), else Top.
std::string PlaybarPlace(std::string_view chosen, const std::optional<std::string>& theirs);

// Ampify's own bottom bar (ampify:bottom-bar, and centre-bar when centred), as a snippet that applies before the
// others: while the bar is away from the top under no theme, or under a theme that does not place it itself
// (Theme::placesPlaybar). Nothing otherwise.
std::optional<Theme> LoadPlaybarLayout(const Theme* base, const LoadOptions& options);

// With no theme active, the accent of theme.accent alone (options.accent) on Apple Music's look, with what Ampify
// derives from it: the controls, links, selected rows and progress line take it.
Theme LoadAccentOnly(const LoadOptions& options);

// Reads and checks a snippet: a folder with resources.xaml, user.css, or both (docs/themes/format.md, "Snippets").
// Its rules see the colours ($name), the variables and the font roles of `base`, the active theme, which may be null.
// colors.ini is not read in a snippet. A key outside the allowed list refuses the snippet, not the theme. The parts
// of `options` do not apply to snippets: a snippet is switched on and off by itself (snippets.<id>.enabled).
Theme LoadSnippet(const std::string& id, const std::string& folder, FileSource& files, const Theme* base,
                  const LoadOptions& options = {});

// Works out again, for all the layers of a composed theme together, the room Apple's fixed slots (the progress line)
// need: each theme and snippet derived its own, and one's height could win while another's shift moved the line.
// `layout`, Ampify's own bar layout when one applies, tells where the bar is under no theme.
void ShareRoomInFixedSlots(Theme& composed, const Theme* layout = nullptr);

// Under every theme and snippet, the playback bar's toggles keep to the right end of their cell, a little apart,
// unless the composed theme places them itself.
void KeepTogglesAtTheRightEnd(Theme& composed);

// Under every theme, the veil over the playback bar's artwork keeps the artwork's size (its icon small and centred).
void KeepLcdVeilInItsArtwork(Theme& composed);

// The section names of colors.ini, in file order, without loading the theme. Empty when the file is missing.
std::vector<std::string> ReadSchemes(FileSource& files);

// The scheme manifest.json names as the theme's default (`defaultScheme`). Empty when the manifest has none.
std::string ReadDefaultScheme(FileSource& files);

// True for a dictionary with no entry at all, such as `<ResourceDictionary/>`: merging it would change nothing.
bool IsEmptyDictionary(std::string_view xaml);

// Every x:Key="..." value in a XAML document, in order. Used to check resources.xaml against the allowed list.
std::vector<std::string> XamlKeys(std::string_view xaml);

}  // namespace ampify::theme
