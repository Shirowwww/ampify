#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

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
};

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

// The parts of a theme that are applied, as in docs/extensions/api.md section 2 (theme.parts.*).
//   colors     the colours of colors.ini replace the app's brushes
//   resources  resources.xaml and the rules of user.css (Spicetify's inject_css)
//   assets     images in url(...) (Spicetify's overwrite_assets)
//   script     main.js runs in AppleMusic.exe while the theme is the active one (the loader starts it)
struct ThemeParts {
    bool colors = true;
    bool resources = true;
    bool assets = true;
    bool script = true;
};

// What the loader needs beyond the files themselves.
struct LoadOptions {
    std::string scheme;                          // the [section] of colors.ini to use; empty takes the first one
    const std::set<std::string>* allowedKeys = nullptr;  // resource keys that resources.xaml may define; null skips the check
    ThemeParts parts;                            // a part that is off is read for its problems only, and not applied
};

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
    // Dark or light, from the background colour of the scheme (Spicetify's main, or the page brush). Unset when the
    // scheme gives no background: its name decides then (IsDarkScheme).
    std::optional<bool> dark;

    // False when the folder is not a theme, or when resources.xaml breaks the allowed list. An unusable theme is never applied.
    bool usable = false;

    Palette palette;
    std::vector<ResourceOverride> resources;
    std::string resourceDictionary;  // the XAML of resources.xaml, merged into the app's resources; empty when none
    std::vector<Assignment> assignments;

    // The :root variables of user.css, by lowercase name without the leading "--", already resolved.
    std::map<std::string, std::string> variables;
    // The font roles the theme sets itself, by canonical role name (see fonts.hpp).
    std::map<std::string, FontRole> fonts;

    // French messages with the file and the line. Problems that do not stop the theme are listed here too.
    std::vector<std::string> problems;
};

// Reads and checks a theme. `folder` is the absolute path, used to resolve image paths.
Theme LoadTheme(const std::string& id, const std::string& folder, FileSource& files, const LoadOptions& options = {});

// Reads and checks a snippet: a folder with resources.xaml, user.css, or both (docs/themes/format.md, "Snippets").
// Its rules see the colours ($name), the variables and the font roles of `base`, the active theme, which may be null.
// colors.ini is not read in a snippet. A key outside the allowed list refuses the snippet, not the theme. The parts
// of `options` do not apply to snippets: a snippet is switched on and off by itself (snippets.<id>.enabled).
Theme LoadSnippet(const std::string& id, const std::string& folder, FileSource& files, const Theme* base,
                  const LoadOptions& options = {});

// The section names of colors.ini, in file order, without loading the theme. Empty when the file is missing.
std::vector<std::string> ReadSchemes(FileSource& files);

// Replaces each `var(--name)` or `var(--name, fallback)` of `value` with the variable (names are case-insensitive).
// Nothing when a variable is missing and has no fallback; `problem` then says which one.
std::optional<std::string> SubstituteVariables(std::string_view value, const std::map<std::string, std::string>& variables,
                                               std::string* problem = nullptr);

// True for a dictionary with no entry at all, such as `<ResourceDictionary/>`: merging it would change nothing.
bool IsEmptyDictionary(std::string_view xaml);

// Every x:Key="..." value in a XAML document, in order. Used to check resources.xaml against the allowed list.
std::vector<std::string> XamlKeys(std::string_view xaml);

}  // namespace ampify::theme
