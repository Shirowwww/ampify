#include "theme.hpp"

#include <cctype>
#include <set>
#include <tuple>
#include <utility>

#include "fonts.hpp"
#include "ini.hpp"
#include "spicetify.hpp"

namespace ampify::theme {

namespace {

std::string LowerText(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

std::string_view Trim(std::string_view text) {
    size_t first = 0;
    size_t last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) first++;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) last--;
    return text.substr(first, last - first);
}

// Picks the scheme to use: the requested one (case-insensitive), or the first section when none is requested.
const Ini::Section* PickScheme(const Ini& ini, const std::string& wanted, Theme& theme) {
    if (!wanted.empty()) {
        if (const Ini::Section* section = ini.FindSection(wanted)) return section;
        theme.problems.push_back("colors.ini: scheme \"" + wanted + "\" not found, the first one is used");
    }
    for (const Ini::Section& section : ini.Sections()) {
        if (!section.name.empty()) return &section;
    }
    return nullptr;
}

// The colours file: colors.ini, or color.ini as Spicetify names it. Empty name when there is none.
std::pair<std::string, std::optional<std::string>> ReadColorsFile(FileSource& files) {
    for (const char* name : {"colors.ini", "color.ini"}) {
        if (std::optional<std::string> text = files.Read(name)) return {name, std::move(text)};
    }
    return {};
}

// A Spicetify colour is written in lowercase (main, main-elevated, main_fg); an app brush in PascalCase.
bool IsSpicetifyName(std::string_view key) {
    for (char c : key) {
        if (std::isupper(static_cast<unsigned char>(c))) return false;
    }
    return !key.empty();
}

// Light or dark, from the background the scheme gives: Spicetify's main, or the app's page background.
std::optional<bool> DarkFromPalette(const Palette& palette) {
    for (const char* key : {"main", "SolidBackgroundFillColorBaseBrush", "ApplicationPageBackgroundThemeBrush"}) {
        if (std::optional<Argb> color = palette.Find(key)) {
            double luminance = 0.2126 * color->r + 0.7152 * color->g + 0.0722 * color->b;
            return luminance < 128.0;
        }
    }
    return std::nullopt;
}

// A Spicetify colour: a $name, a --spice-name variable as in Spicetify, and the app brushes that play its part
// (SpicetifyResources) unless the scheme names them itself. Returns the Spicetify name.
std::string DefineSpicetifyColor(const Ini::Entry& entry, Argb color, Theme& theme) {
    std::string key = SpicetifyKey(entry.key);
    if (key.empty()) key = LowerText(entry.key);  // a colour of the theme's own, such as accent
    if (!theme.palette.Define(key, color)) {
        theme.problems.push_back("colors.ini line " + std::to_string(entry.line) + ": \"" + entry.key +
                                 "\" defined twice, ignored");
        return {};
    }
    if (LowerText(entry.key) != key) theme.palette.Define(LowerText(entry.key), color);  // main_fg stays usable too
    theme.variables["spice-" + key] = ToXamlColor(color);
    return key;
}

std::set<std::string> LoadColors(FileSource& files, const std::string& wanted, bool applied, Theme& theme) {
    std::set<std::string> spicetify;
    auto [file, text] = ReadColorsFile(files);
    if (!text) return spicetify;

    Ini ini;
    for (const std::string& problem : ini.Parse(*text)) theme.problems.push_back(file + " " + problem);
    for (const Ini::Section& section : ini.Sections()) {
        if (!section.name.empty()) theme.schemes.push_back(section.name);
    }

    const Ini::Section* scheme = PickScheme(ini, wanted, theme);
    if (!scheme) {
        theme.problems.push_back(file + ": no scheme (a [Base] section, for example)");
        return spicetify;
    }
    theme.scheme = scheme->name;

    for (const Ini::Entry& entry : scheme->entries) {
        std::optional<Argb> color = ParseColor(entry.value);
        if (!color) {
            theme.problems.push_back(file + " line " + std::to_string(entry.line) + ": color \"" + entry.value +
                                     "\" is unreadable");
            continue;
        }
        if (IsSpicetifyName(entry.key)) {
            std::string key = DefineSpicetifyColor(entry, *color, theme);
            if (!key.empty()) spicetify.insert(key);
            continue;
        }
        if (!theme.palette.Define(entry.key, *color)) {
            theme.problems.push_back(file + " line " + std::to_string(entry.line) + ": \"" + entry.key +
                                     "\" defined twice, ignored");
            continue;
        }
        if (applied) theme.resources.push_back({entry.key, {ValueKind::Color, ToXamlColor(*color)}});
    }

    // The app brushes that play the part of a Spicetify colour, unless the scheme gives them itself.
    for (const SpicetifyResource& mapping : SpicetifyResources()) {
        std::optional<Argb> color = spicetify.count(mapping.spicetify) ? theme.palette.Find(mapping.spicetify) : std::nullopt;
        if (!color || theme.palette.Find(mapping.resource)) continue;
        theme.palette.Define(mapping.resource, *color);
        if (applied) theme.resources.push_back({mapping.resource, {ValueKind::Color, ToXamlColor(*color)}});
    }
    theme.dark = DarkFromPalette(theme.palette);
    return spicetify;
}

// Returns false when the theme must be refused. Refusing is the rule the API sets: a bad key breaks the theme.
bool LoadDictionary(FileSource& files, const LoadOptions& options, Theme& theme) {
    std::optional<std::string> xaml = files.Read("resources.xaml");
    if (!xaml) return true;

    if (xaml->find("<ResourceDictionary") == std::string::npos) {
        theme.problems.push_back("resources.xaml: no <ResourceDictionary>, theme refused");
        return false;
    }

    if (XamlKeys(*xaml).empty()) {
        // No key to check: implicit styles only, or an empty dictionary such as the one `ampify new snippet` writes.
    } else if (!options.allowedKeys) {
        theme.problems.push_back("resources.xaml: list of allowed keys missing, check not done");
    } else {
        bool allAllowed = true;
        for (const std::string& key : XamlKeys(*xaml)) {
            if (options.allowedKeys->count(key)) continue;
            theme.problems.push_back("resources.xaml: key \"" + key + "\" is not in the allowed list, theme refused");
            allAllowed = false;
        }
        if (!allAllowed) return false;
    }

    theme.resourceDictionary = std::move(*xaml);
    return true;
}

std::string AtLine(const std::string& source, int line) {
    return source + " line " + std::to_string(line);
}

// What the rules of one file need besides the theme they fill.
struct RuleSource {
    std::string file = "user.css";  // for the log and the problems
    const Theme* base = nullptr;    // the active theme, for a snippet: its font roles fill what the snippet leaves out
    int layer = 0;                  // -1 for the rules translated from color.ini: the theme's user.css wins over them
};

// `--font-title`, `--font-title-weight`, `--font-title-size`: checks the value and records the role part.
void DefineFontPart(const std::string& name, const std::string& value, int line, const ValueContext& context,
                    const RuleSource& source, Theme& theme) {
    std::string role = name.substr(5);  // after "font-"
    const char* css = "font-family";
    std::string FontRole::*part = &FontRole::family;
    for (auto [suffix, property, member] : {std::tuple{"-weight", "font-weight", &FontRole::weight},
                                            std::tuple{"-size", "font-size", &FontRole::size}}) {
        std::string_view end(suffix);
        if (role.size() > end.size() && role.ends_with(end)) {
            role.resize(role.size() - end.size());
            css = property;
            part = member;
            break;
        }
    }
    if (role.empty()) {
        theme.problems.push_back(AtLine(source.file, line) + ": font role without a name (--font-<role>)");
        return;
    }

    Conversion conversion = ConvertValue(*FindProperty(css), value, context);
    if (!conversion.value) {
        theme.problems.push_back(AtLine(source.file, line) + ": --" + name + ": " + conversion.problem + ", ignored");
        return;
    }
    theme.fonts[CanonicalFontRole(role)].*part = conversion.value->text;
}

// The declarations of the :root blocks, in file order. A variable can use the ones defined before it.
void LoadVariables(const Stylesheet& sheet, const ValueContext& context, const RuleSource& source, Theme& theme) {
    for (const Rule& rule : sheet.rules) {
        if (!rule.root) continue;
        for (const Declaration& declaration : rule.declarations) {
            if (!declaration.property.starts_with("--") || declaration.property.size() == 2) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + ": \"" + declaration.property +
                                         "\" inside :root, only --name variables are read");
                continue;
            }
            std::string problem;
            std::optional<std::string> value = SubstituteVariables(declaration.value, theme.variables, &problem);
            if (!value) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + ": " + problem + ", ignored");
                continue;
            }
            std::string name = LowerText(std::string_view(declaration.property).substr(2));
            theme.variables[name] = *value;
            if (name.starts_with("font-")) DefineFontPart(name, *value, declaration.line, context, source, theme);
        }
    }
}

void AddAssignment(const Rule& rule, const PropertySpec& spec, const Value& value, const Declaration& declaration,
                   int order, const RuleSource& source, Theme& theme) {
    for (const Selector& selector : rule.selectors) {
        // A move is not a value that can come and go with the pointer: the other items would shift each time.
        if (selector.hover && spec.kind == PropertyKind::Order) {
            theme.problems.push_back(AtLine(source.file, declaration.line) + ": \"" + declaration.property +
                                     "\" is not possible under :hover (" + selector.text + "), ignored");
            continue;
        }
        Assignment assignment{selector, SpecificityOf(selector), std::string(spec.xaml), value, declaration.important,
                              order, declaration.line};
        assignment.source = source.file;
        assignment.layer = source.layer;
        theme.assignments.push_back(std::move(assignment));
    }
}

// `font-role: title` sets the family, the weight and the size of the role at once.
bool AddFontRole(const Rule& rule, const Declaration& declaration, const std::string& value, int order,
                 const RuleSource& source, bool applied, Theme& theme) {
    std::optional<FontStyle> style = ResolveFontRole(std::vector<const Theme*>{&theme, source.base}, value);
    if (!style) {
        theme.problems.push_back(AtLine(source.file, declaration.line) + ": font role \"" + value +
                                 "\" is unknown, ignored");
        return false;
    }
    if (!applied) return true;
    AddAssignment(rule, *FindProperty("font-family"), {ValueKind::Text, style->family}, declaration, order, source, theme);
    AddAssignment(rule, *FindProperty("font-weight"), {ValueKind::Number, style->weight}, declaration, order, source, theme);
    AddAssignment(rule, *FindProperty("font-size"), {ValueKind::Length, style->size}, declaration, order, source, theme);
    return true;
}

void LoadRuleText(const std::string& css, const ValueContext& context, Theme& theme, bool applied,
                  const RuleSource& source) {
    Stylesheet sheet = ParseStylesheet(css);
    for (const std::string& problem : sheet.problems) theme.problems.push_back(source.file + " " + problem);

    // Variables first: as in CSS, a rule may use a variable that a :root block defines further down.
    LoadVariables(sheet, context, source, theme);

    int order = 0;
    for (const Rule& rule : sheet.rules) {
        if (rule.root) continue;
        for (const Declaration& declaration : rule.declarations) {
            if (declaration.property.starts_with("--")) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + ": variable \"" +
                                         declaration.property + "\" outside :root, ignored");
                continue;
            }

            std::string problem;
            std::optional<std::string> raw = SubstituteVariables(declaration.value, theme.variables, &problem);
            if (!raw) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + ": " + problem + ", ignored");
                continue;
            }

            if (LowerText(declaration.property) == "font-role") {
                if (AddFontRole(rule, declaration, *raw, order, source, applied, theme)) order++;
                continue;
            }

            const PropertySpec* spec = FindProperty(declaration.property);
            if (!spec) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + ": property \"" +
                                         declaration.property + "\" is unknown, ignored");
                continue;
            }

            Conversion conversion = ConvertValue(*spec, *raw, context);
            if (!conversion.value) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + ": " + conversion.problem +
                                         ", ignored");
                continue;
            }

            if (applied) AddAssignment(rule, *spec, *conversion.value, declaration, order, source, theme);
            order++;
        }
    }
}

void LoadRules(FileSource& files, const ValueContext& context, Theme& theme, bool applied, const RuleSource& source) {
    if (std::optional<std::string> css = files.Read("user.css")) LoadRuleText(*css, context, theme, applied, source);
}

}  // namespace

Theme LoadTheme(const std::string& id, const std::string& folder, FileSource& files, const LoadOptions& options) {
    Theme theme;
    theme.id = id;
    theme.name = id;

    std::set<std::string> spicetify = LoadColors(files, options.scheme, options.parts.colors, theme);

    // A part that is off is still checked, so the problems show up before the part is turned on.
    bool dictionaryOk = LoadDictionary(files, options, theme);
    bool hasRules = files.Exists("user.css");

    bool isTheme = !theme.schemes.empty() || files.Exists("resources.xaml") || hasRules;
    if (!isTheme) {
        theme.problems.push_back("no theme file (colors.ini, resources.xaml or user.css): not a theme");
        return theme;
    }

    theme.usable = dictionaryOk;
    if (!dictionaryOk) return theme;

    if (!options.parts.resources) theme.resourceDictionary.clear();
    ValueContext context{theme.palette, files, folder, options.parts.assets};
    // Spicetify's colours paint Apple Music's own elements first; the theme's user.css comes on top.
    if (std::string translated = SpicetifyStylesheet(spicetify); !translated.empty()) {
        RuleSource source;
        source.file = "color.ini (traduction Spicetify)";
        source.layer = -1;
        LoadRuleText(translated, context, theme, options.parts.colors, source);
    }
    LoadRules(files, context, theme, options.parts.resources, RuleSource{});
    return theme;
}

Theme LoadSnippet(const std::string& id, const std::string& folder, FileSource& files, const Theme* base,
                  const LoadOptions& options) {
    Theme snippet;
    snippet.id = id;
    snippet.name = id;
    if (base) {
        snippet.palette = base->palette;
        snippet.variables = base->variables;
    }

    if (files.Exists("colors.ini")) {
        snippet.problems.push_back("colors.ini ignored: a snippet uses the colors of the active theme ($name)");
    }

    bool dictionaryOk = LoadDictionary(files, options, snippet);
    bool isSnippet = files.Exists("resources.xaml") || files.Exists("user.css");
    if (!isSnippet) {
        snippet.problems.push_back("no snippet file (resources.xaml or user.css): not a snippet");
        return snippet;
    }
    snippet.usable = dictionaryOk;
    if (!dictionaryOk) return snippet;
    if (IsEmptyDictionary(snippet.resourceDictionary)) snippet.resourceDictionary.clear();

    ValueContext context{snippet.palette, files, folder, true};
    RuleSource source;
    source.file = "snippets/" + id + "/user.css";
    source.base = base;
    LoadRules(files, context, snippet, true, source);
    return snippet;
}

std::vector<std::string> ReadSchemes(FileSource& files) {
    std::vector<std::string> schemes;
    std::optional<std::string> text = ReadColorsFile(files).second;
    if (!text) return schemes;

    Ini ini;
    ini.Parse(*text);
    for (const Ini::Section& section : ini.Sections()) {
        if (!section.name.empty()) schemes.push_back(section.name);
    }
    return schemes;
}

std::optional<std::string> SubstituteVariables(std::string_view value, const std::map<std::string, std::string>& variables,
                                               std::string* problem) {
    std::string text(value);
    // Each pass replaces the first var(...). A fallback may hold another var(), so a few passes may be needed; the
    // limit stops a value that would grow forever.
    for (int pass = 0; pass < 32; pass++) {
        std::string lowered = LowerText(text);
        size_t start = lowered.find("var(");
        if (start == std::string::npos) return text;

        int depth = 0;
        size_t close = std::string::npos;
        for (size_t i = start + 3; i < text.size(); i++) {
            if (text[i] == '(') depth++;
            if (text[i] == ')' && --depth == 0) {
                close = i;
                break;
            }
        }
        if (close == std::string::npos) {
            if (problem) *problem = "var( without a closing parenthesis";
            return std::nullopt;
        }

        std::string_view inside = std::string_view(text).substr(start + 4, close - start - 4);
        size_t comma = inside.find(',');
        std::string name = LowerText(Trim(inside.substr(0, comma)));
        if (!name.starts_with("--") || name.size() == 2) {
            if (problem) *problem = "var() needs a --variable name";
            return std::nullopt;
        }

        std::string replacement;
        auto it = variables.find(name.substr(2));
        if (it != variables.end()) {
            replacement = it->second;
        } else if (comma != std::string_view::npos) {
            replacement = std::string(Trim(inside.substr(comma + 1)));
        } else {
            if (problem) *problem = "variable \"" + name + "\" is not defined";
            return std::nullopt;
        }
        text = text.substr(0, start) + replacement + text.substr(close + 1);
    }
    if (problem) *problem = "too many nested var()";
    return std::nullopt;
}

bool IsEmptyDictionary(std::string_view xaml) {
    size_t open = xaml.find("<ResourceDictionary");
    if (open == std::string_view::npos) return Trim(xaml).empty();
    size_t end = xaml.find('>', open);
    if (end == std::string_view::npos) return false;
    if (end > 0 && xaml[end - 1] == '/') return true;

    size_t close = xaml.rfind("</ResourceDictionary>");
    if (close == std::string_view::npos || close < end) return false;
    std::string body(xaml.substr(end + 1, close - end - 1));
    // Comments do not count as entries.
    for (size_t comment = body.find("<!--"); comment != std::string::npos; comment = body.find("<!--")) {
        size_t stop = body.find("-->", comment);
        body.erase(comment, stop == std::string::npos ? std::string::npos : stop + 3 - comment);
    }
    return Trim(body).empty();
}

std::vector<std::string> XamlKeys(std::string_view xaml) {
    std::vector<std::string> keys;
    const std::string_view marker = "x:Key=";
    size_t pos = 0;
    while ((pos = xaml.find(marker, pos)) != std::string_view::npos) {
        pos += marker.size();
        if (pos >= xaml.size()) break;

        char quote = xaml[pos];
        if (quote != '"' && quote != '\'') continue;

        size_t end = xaml.find(quote, pos + 1);
        if (end == std::string_view::npos) break;
        keys.emplace_back(xaml.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    return keys;
}

}  // namespace ampify::theme
