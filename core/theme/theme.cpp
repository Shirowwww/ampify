#include "theme.hpp"

#include "ini.hpp"

namespace amk::theme {

namespace {

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

    if (!options.allowedKeys) {
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

void LoadRules(FileSource& files, const ValueContext& context, Theme& theme, bool applied) {
    std::optional<std::string> css = files.Read("user.css");
    if (!css) return;

    Stylesheet sheet = ParseStylesheet(*css);
    for (const std::string& problem : sheet.problems) theme.problems.push_back("user.css " + problem);

    int order = 0;
    for (const Rule& rule : sheet.rules) {
        for (const Declaration& declaration : rule.declarations) {
            const PropertySpec* spec = FindProperty(declaration.property);
            if (!spec) {
                theme.problems.push_back("user.css ligne " + std::to_string(declaration.line) + " : propriété « " +
                                        declaration.property + " » inconnue, ignorée");
                continue;
            }

            Conversion conversion = ConvertValue(*spec, declaration.value, context);
            if (!conversion.value) {
                theme.problems.push_back("user.css ligne " + std::to_string(declaration.line) + " : " +
                                        conversion.problem + ", ignorée");
                continue;
            }

            if (!applied) {
                order++;
                continue;
            }
            for (const Selector& selector : rule.selectors) {
                theme.assignments.push_back({selector, SpecificityOf(selector), std::string(spec->xaml),
                                             *conversion.value, declaration.important, order, declaration.line});
            }
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
    LoadRules(files, context, theme, options.parts.resources);
    return theme;
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
