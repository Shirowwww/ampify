#include "manifest.hpp"

#include <algorithm>
#include <regex>
#include <set>

#include "json.hpp"

namespace ampify {

namespace {

// Same list as schemas/manifest.schema.json. Keep the two in step.
const std::set<std::string_view> kFixedPermissions = {
    "player.read",   "player.control", "audio.analyze", "audio.gain",    "audio.eq",    "storage",     "notifications",
    "ipc.discord",   "midi.input",     "library.read",  "library.write", "services.read", "ui.keyboard",
    "ui.topbar",     "ui.playbar",     "ui.menu",       "ui.popup",      "ui.panel",    "ui.page",
    "ui.window",     "ui.tray",       "fs.output",   "kit.manage",
};

const std::set<std::string_view> kKnownFields = {
    "$schema", "id", "name", "version", "kind", "author", "description", "kitApi", "homepage", "license",
    "main", "colors", "defaultScheme", "resources", "icon", "permissions", "appVersions", "processes", "type",
};

// Length in Unicode code points, so that "é" counts as one character.
size_t CodePoints(const std::string& text) {
    return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

bool Matches(const std::string& text, const std::regex& pattern) { return std::regex_match(text, pattern); }

const std::regex kIdPattern("^[a-z0-9][a-z0-9-]{1,39}$");
const std::regex kVersionPattern("^[0-9]+\\.[0-9]+\\.[0-9]+$");
const std::regex kKitApiPattern("^[0-9]+\\.[0-9]+$");
const std::regex kAuthorPattern("^[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?$");
const std::regex kHttpsPattern("^https://[^\\s]+$");
const std::regex kJsFilePattern("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}\\.js$");
const std::regex kIniFilePattern("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}\\.ini$");
const std::regex kXamlFilePattern("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}\\.xaml$");
const std::regex kSvgFilePattern("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}\\.svg$");
const std::regex kSchemeNamePattern("^[A-Za-z0-9_-]{1,32}$");
const std::regex kAppVersionPattern("^[0-9]+(\\.[0-9]+){1,3}$");
const std::regex kHostPattern("^[a-z0-9.-]{1,253}$");

}  // namespace

bool IsKnownPermission(std::string_view permission) {
    if (kFixedPermissions.count(permission)) return true;
    constexpr std::string_view kNetwork = "network:";
    if (permission.substr(0, kNetwork.size()) != kNetwork) return false;
    return std::regex_match(std::string(permission.substr(kNetwork.size())), kHostPattern);
}

std::string_view ToString(ElementKind kind) {
    switch (kind) {
        case ElementKind::Extension: return "extension";
        case ElementKind::Theme: return "theme";
        case ElementKind::App: return "app";
        case ElementKind::Snippet: return "snippet";
    }
    return "extension";
}

bool Manifest::HasPermission(std::string_view permission) const {
    return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
}

bool Manifest::RunsIn(std::string_view process) const {
    return std::find(processes.begin(), processes.end(), process) != processes.end();
}

std::vector<std::string> Manifest::NetworkHosts() const {
    std::vector<std::string> hosts;
    constexpr std::string_view kNetwork = "network:";
    for (const std::string& permission : permissions) {
        if (permission.compare(0, kNetwork.size(), kNetwork) == 0) hosts.push_back(permission.substr(kNetwork.size()));
    }
    return hosts;
}

ManifestResult ParseManifest(std::string_view text, std::string_view folderName, int supportedKitApiMajor,
                             const std::function<bool(const std::string&)>& hasFile) {
    ManifestResult result;
    std::string parseError;
    std::optional<Json> root = Json::Parse(text, &parseError);
    if (!root || !root->IsObject()) {
        result.errors.push_back("manifest.json n'est pas un objet JSON valide" +
                                (parseError.empty() ? std::string() : " : " + parseError));
        return result;
    }

    for (const auto& [key, value] : *root->AsObject()) {
        (void)value;
        if (!kKnownFields.count(key)) result.warnings.push_back("champ inconnu ignoré : " + key);
    }

    Manifest m;
    auto text_of = [&](const char* key, bool required) -> std::optional<std::string> {
        const Json* field = root->Find(key);
        if (!field) {
            if (required) result.errors.push_back(std::string("champ obligatoire absent : ") + key);
            return std::nullopt;
        }
        const std::string* s = field->AsString();
        if (!s) {
            result.errors.push_back(std::string("le champ ") + key + " doit être une chaîne");
            return std::nullopt;
        }
        return *s;
    };
    auto check = [&](const char* key, const std::optional<std::string>& value, const std::regex& pattern,
                     const char* rule) {
        if (value && !Matches(*value, pattern)) {
            result.errors.push_back(std::string("le champ ") + key + " doit respecter : " + rule);
        }
    };

    std::optional<std::string> id = text_of("id", true);
    if (id) {
        m.id = *id;
        check("id", id, kIdPattern, "[a-z0-9][a-z0-9-]{1,39}");
        if (*id != folderName) result.errors.push_back("id \"" + *id + "\" doit être le nom du dossier");
    }

    std::optional<std::string> name = text_of("name", true);
    if (name) {
        m.name = *name;
        if (CodePoints(*name) < 1 || CodePoints(*name) > 60) result.errors.push_back("name doit faire de 1 à 60 caractères");
    }

    std::optional<std::string> version = text_of("version", true);
    if (version) {
        m.version = *version;
        check("version", version, kVersionPattern, "MAJEUR.MINEUR.CORRECTIF");
    }

    if (std::optional<std::string> kind = text_of("kind", true)) {
        if (*kind == "extension") m.kind = ElementKind::Extension;
        else if (*kind == "theme") m.kind = ElementKind::Theme;
        else if (*kind == "app") m.kind = ElementKind::App;
        else if (*kind == "snippet") m.kind = ElementKind::Snippet;
        else result.errors.push_back("kind doit valoir extension, theme, app ou snippet");
    }

    std::optional<std::string> author = text_of("author", true);
    if (author) {
        m.author = *author;
        check("author", author, kAuthorPattern, "identifiant GitHub");
    }

    std::optional<std::string> description = text_of("description", true);
    if (description) {
        m.description = *description;
        if (CodePoints(*description) < 1 || CodePoints(*description) > 300) {
            result.errors.push_back("description doit faire de 1 à 300 caractères");
        }
    }

    std::optional<std::string> kitApi = text_of("kitApi", true);
    if (kitApi) {
        m.kitApi = *kitApi;
        if (!Matches(*kitApi, kKitApiPattern)) {
            result.errors.push_back("kitApi doit valoir MAJEUR.MINEUR, par exemple 1.0");
        } else if (std::stoi(kitApi->substr(0, kitApi->find('.'))) != supportedKitApiMajor) {
            result.errors.push_back("kitApi " + *kitApi + " : version majeure non prise en charge (attendue " +
                                    std::to_string(supportedKitApiMajor) + ")");
        }
    }

    m.homepage = text_of("homepage", false);
    check("homepage", m.homepage, kHttpsPattern, "une URL https://");
    m.license = text_of("license", false);
    if (m.license && (CodePoints(*m.license) < 1 || CodePoints(*m.license) > 64)) {
        result.errors.push_back("license doit faire de 1 à 64 caractères");
    }

    m.main = text_of("main", false);
    check("main", m.main, kJsFilePattern, "un fichier .js à la racine");
    if (std::optional<std::string> type = text_of("type", false)) {
        if (*type == "module") m.module = true;
        else if (*type != "script") result.errors.push_back("type doit valoir script ou module");
    }
    m.colors = text_of("colors", false);
    check("colors", m.colors, kIniFilePattern, "un fichier .ini à la racine");
    m.defaultScheme = text_of("defaultScheme", false);
    check("defaultScheme", m.defaultScheme, kSchemeNamePattern, "1 à 32 caractères sans espace");
    m.resources = text_of("resources", false);
    check("resources", m.resources, kXamlFilePattern, "un fichier .xaml à la racine");
    m.icon = text_of("icon", false);
    check("icon", m.icon, kSvgFilePattern, "un fichier .svg à la racine");

    if (const Json* list = root->Find("permissions")) {
        if (!list->AsArray()) {
            result.errors.push_back("permissions doit être une liste");
        } else if (list->AsArray()->size() > 32) {
            result.errors.push_back("permissions : 32 au plus");
        } else {
            for (const Json& item : *list->AsArray()) {
                const std::string* permission = item.AsString();
                if (!permission || !IsKnownPermission(*permission)) {
                    result.errors.push_back("permission inconnue : " + (permission ? *permission : std::string("?")));
                } else if (m.HasPermission(*permission)) {
                    result.errors.push_back("permission en double : " + *permission);
                } else {
                    m.permissions.push_back(*permission);
                }
            }
        }
    }

    if (const Json* list = root->Find("appVersions")) {
        if (!list->AsArray()) {
            result.errors.push_back("appVersions doit être une liste");
        } else if (list->AsArray()->size() > 16) {
            result.errors.push_back("appVersions : 16 au plus");
        } else {
            for (const Json& item : *list->AsArray()) {
                const std::string* v = item.AsString();
                if (!v || !Matches(*v, kAppVersionPattern)) {
                    result.errors.push_back("appVersions : version invalide");
                } else {
                    m.appVersions.push_back(*v);
                }
            }
        }
    }

    if (const Json* list = root->Find("processes")) {
        m.processes.clear();
        if (!list->AsArray() || list->AsArray()->empty() || list->AsArray()->size() > 2) {
            result.errors.push_back("processes doit être une liste d'une ou deux valeurs : ui, engine");
        } else {
            for (const Json& item : *list->AsArray()) {
                const std::string* process = item.AsString();
                if (!process || (*process != "ui" && *process != "engine")) {
                    result.errors.push_back("processes : valeur inconnue (ui ou engine)");
                } else if (std::find(m.processes.begin(), m.processes.end(), *process) != m.processes.end()) {
                    result.errors.push_back("processes : valeur en double : " + *process);
                } else {
                    m.processes.push_back(*process);
                }
            }
        }
    }

    // Rules that depend on the kind, from the table in docs/extensions/MANIFEST.md.
    auto require_file = [&](const char* key, const std::optional<std::string>& file) {
        if (file && hasFile && !hasFile(*file)) result.errors.push_back(std::string("fichier absent : ") + *file + " (" + key + ")");
    };
    switch (m.kind) {
        case ElementKind::Extension:
        case ElementKind::App:
            if (!m.main) result.errors.push_back("main est obligatoire pour une extension ou une app");
            break;
        case ElementKind::Theme:
            if (!m.colors && !m.resources) result.errors.push_back("un thème doit avoir colors, resources, ou les deux");
            break;
        case ElementKind::Snippet:
            if (!m.resources) result.errors.push_back("resources est obligatoire pour un snippet");
            if (m.main) result.errors.push_back("main n'est pas autorisé pour un snippet");
            break;
    }
    require_file("main", m.main);
    require_file("colors", m.colors);
    require_file("resources", m.resources);
    require_file("icon", m.icon);

    if (result.errors.empty()) result.manifest = std::move(m);
    return result;
}

}  // namespace ampify
