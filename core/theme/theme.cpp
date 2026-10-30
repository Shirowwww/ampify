#include "theme.hpp"

#include <cctype>
#include <tuple>

#include "fonts.hpp"
#include "ini.hpp"

namespace amk::theme {

namespace {

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
        theme.problems.push_back("colors.ini : schéma « " + wanted + " » absent, le premier est utilisé");
    }
    for (const Ini::Section& section : ini.Sections()) {
        if (!section.name.empty()) return &section;
    }
    return nullptr;
}

void LoadColors(FileSource& files, const std::string& wanted, bool applied, Theme& theme) {
    std::optional<std::string> text = files.Read("colors.ini");
    if (!text) return;

    Ini ini;
    for (const std::string& problem : ini.Parse(*text)) theme.problems.push_back("colors.ini " + problem);
    for (const Ini::Section& section : ini.Sections()) {
        if (!section.name.empty()) theme.schemes.push_back(section.name);
    }

    const Ini::Section* scheme = PickScheme(ini, wanted, theme);
    if (!scheme) {
        theme.problems.push_back("colors.ini : aucun schéma (section [Base] par exemple)");
        return;
    }
    theme.scheme = scheme->name;

    for (const Ini::Entry& entry : scheme->entries) {
        std::optional<Argb> color = ParseColor(entry.value);
        if (!color) {
            theme.problems.push_back("colors.ini ligne " + std::to_string(entry.line) + " : couleur « " + entry.value +
                                     " » illisible");
            continue;
        }
        if (!theme.palette.Define(entry.key, *color)) {
            theme.problems.push_back("colors.ini ligne " + std::to_string(entry.line) + " : « " + entry.key +
                                     " » défini deux fois, ignoré");
            continue;
        }
        if (applied) theme.resources.push_back({entry.key, {ValueKind::Color, ToXamlColor(*color)}});
    }
}

// Returns false when the theme must be refused. Refusing is the rule the API sets: a bad key breaks the theme.
bool LoadDictionary(FileSource& files, const LoadOptions& options, Theme& theme) {
    std::optional<std::string> xaml = files.Read("resources.xaml");
    if (!xaml) return true;

    if (xaml->find("<ResourceDictionary") == std::string::npos) {
        theme.problems.push_back("resources.xaml : pas de <ResourceDictionary>, thème refusé");
        return false;
    }

    if (XamlKeys(*xaml).empty()) {
        // No key to check: implicit styles only, or an empty dictionary such as the one `amk new snippet` writes.
    } else if (!options.allowedKeys) {
        theme.problems.push_back("resources.xaml : liste des clés autorisées absente, vérification non faite");
    } else {
        bool allAllowed = true;
        for (const std::string& key : XamlKeys(*xaml)) {
            if (options.allowedKeys->count(key)) continue;
            theme.problems.push_back("resources.xaml : clé « " + key + " » hors de la liste autorisée, thème refusé");
            allAllowed = false;
        }
        if (!allAllowed) return false;
    }

    theme.resourceDictionary = std::move(*xaml);
    return true;
}

std::string LowerText(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

std::string AtLine(const std::string& source, int line) {
    return source + " ligne " + std::to_string(line);
}

// What the rules of one file need besides the theme they fill.
struct RuleSource {
    std::string file = "user.css";  // for the journal and the problems
    const Theme* base = nullptr;    // the active theme, for a snippet: its font roles fill what the snippet leaves out
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
        theme.problems.push_back(AtLine(source.file, line) + " : rôle de police sans nom (--font-<rôle>)");
        return;
    }

    Conversion conversion = ConvertValue(*FindProperty(css), value, context);
    if (!conversion.value) {
        theme.problems.push_back(AtLine(source.file, line) + " : --" + name + " : " + conversion.problem + ", ignorée");
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
                theme.problems.push_back(AtLine(source.file, declaration.line) + " : « " + declaration.property +
                                         " » dans :root, seules les variables --nom sont lues");
                continue;
            }
            std::string problem;
            std::optional<std::string> value = SubstituteVariables(declaration.value, theme.variables, &problem);
            if (!value) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + " : " + problem + ", ignorée");
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
        Assignment assignment{selector, SpecificityOf(selector), std::string(spec.xaml), value, declaration.important,
                              order, declaration.line};
        assignment.source = source.file;
        theme.assignments.push_back(std::move(assignment));
    }
}

// `font-role: title` sets the family, the weight and the size of the role at once.
bool AddFontRole(const Rule& rule, const Declaration& declaration, const std::string& value, int order,
                 const RuleSource& source, bool applied, Theme& theme) {
    std::optional<FontStyle> style = ResolveFontRole(std::vector<const Theme*>{&theme, source.base}, value);
    if (!style) {
        theme.problems.push_back(AtLine(source.file, declaration.line) + " : rôle de police « " + value +
                                 " » inconnu, ignorée");
        return false;
    }
    if (!applied) return true;
    AddAssignment(rule, *FindProperty("font-family"), {ValueKind::Text, style->family}, declaration, order, source, theme);
    AddAssignment(rule, *FindProperty("font-weight"), {ValueKind::Number, style->weight}, declaration, order, source, theme);
    AddAssignment(rule, *FindProperty("font-size"), {ValueKind::Length, style->size}, declaration, order, source, theme);
    return true;
}

void LoadRules(FileSource& files, const ValueContext& context, Theme& theme, bool applied, const RuleSource& source) {
    std::optional<std::string> css = files.Read("user.css");
    if (!css) return;

    Stylesheet sheet = ParseStylesheet(*css);
    for (const std::string& problem : sheet.problems) theme.problems.push_back(source.file + " " + problem);

    // Variables first: as in CSS, a rule may use a variable that a :root block defines further down.
    LoadVariables(sheet, context, source, theme);

    int order = 0;
    for (const Rule& rule : sheet.rules) {
        if (rule.root) continue;
        for (const Declaration& declaration : rule.declarations) {
            if (declaration.property.starts_with("--")) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + " : variable « " +
                                         declaration.property + " » hors de :root, ignorée");
                continue;
            }

            std::string problem;
            std::optional<std::string> raw = SubstituteVariables(declaration.value, theme.variables, &problem);
            if (!raw) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + " : " + problem + ", ignorée");
                continue;
            }

            if (LowerText(declaration.property) == "font-role") {
                if (AddFontRole(rule, declaration, *raw, order, source, applied, theme)) order++;
                continue;
            }

            const PropertySpec* spec = FindProperty(declaration.property);
            if (!spec) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + " : propriété « " +
                                         declaration.property + " » inconnue, ignorée");
                continue;
            }

            Conversion conversion = ConvertValue(*spec, *raw, context);
            if (!conversion.value) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + " : " + conversion.problem +
                                         ", ignorée");
                continue;
            }

            if (applied) AddAssignment(rule, *spec, *conversion.value, declaration, order, source, theme);
            order++;
        }
    }
}

}  // namespace

Theme LoadTheme(const std::string& id, const std::string& folder, FileSource& files, const LoadOptions& options) {
    Theme theme;
    theme.id = id;
    theme.name = id;

    LoadColors(files, options.scheme, options.parts.colors, theme);

    // A part that is off is still checked, so the problems show up before the part is turned on.
    bool dictionaryOk = LoadDictionary(files, options, theme);
    bool hasRules = files.Exists("user.css");

    bool isTheme = !theme.schemes.empty() || files.Exists("resources.xaml") || hasRules;
    if (!isTheme) {
        theme.problems.push_back("aucun fichier de thème (colors.ini, resources.xaml ou user.css) : pas un thème");
        return theme;
    }

    theme.usable = dictionaryOk;
    if (!dictionaryOk) return theme;

    if (!options.parts.resources) theme.resourceDictionary.clear();
    ValueContext context{theme.palette, files, folder, options.parts.assets};
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
        snippet.problems.push_back("colors.ini ignoré : un snippet utilise les couleurs du thème actif ($nom)");
    }

    bool dictionaryOk = LoadDictionary(files, options, snippet);
    bool isSnippet = files.Exists("resources.xaml") || files.Exists("user.css");
    if (!isSnippet) {
        snippet.problems.push_back("aucun fichier de snippet (resources.xaml ou user.css) : pas un snippet");
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
    std::optional<std::string> text = files.Read("colors.ini");
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
            if (problem) *problem = "var( sans parenthèse fermante";
            return std::nullopt;
        }

        std::string_view inside = std::string_view(text).substr(start + 4, close - start - 4);
        size_t comma = inside.find(',');
        std::string name = LowerText(Trim(inside.substr(0, comma)));
        if (!name.starts_with("--") || name.size() == 2) {
            if (problem) *problem = "var() attend un nom --variable";
            return std::nullopt;
        }

        std::string replacement;
        auto it = variables.find(name.substr(2));
        if (it != variables.end()) {
            replacement = it->second;
        } else if (comma != std::string_view::npos) {
            replacement = std::string(Trim(inside.substr(comma + 1)));
        } else {
            if (problem) *problem = "variable « " + name + " » non définie";
            return std::nullopt;
        }
        text = text.substr(0, start) + replacement + text.substr(close + 1);
    }
    if (problem) *problem = "trop de var() imbriqués";
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

}  // namespace amk::theme
