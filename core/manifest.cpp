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
    "ui.window",     "ui.sidebar",    "ui.tray",       "fs.output",   "kit.manage",
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
        result.errors.push_back("manifest.json is not a valid JSON object" +
                                (parseError.empty() ? std::string() : ": " + parseError));
        return result;
    }

    for (const auto& [key, value] : *root->AsObject()) {
        (void)value;
        if (!kKnownFields.count(key)) result.warnings.push_back("unknown field ignored: " + key);
    }

    Manifest m;
    auto text_of = [&](const char* key, bool required) -> std::optional<std::string> {
        const Json* field = root->Find(key);
        if (!field) {
            if (required) result.errors.push_back(std::string("required field missing: ") + key);
            return std::nullopt;
        }
        const std::string* s = field->AsString();
        if (!s) {
            result.errors.push_back(std::string("field ") + key + " must be a string");
            return std::nullopt;
        }
        return *s;
    };
    auto check = [&](const char* key, const std::optional<std::string>& value, const std::regex& pattern,
                     const char* rule) {
        if (value && !Matches(*value, pattern)) {
            result.errors.push_back(std::string("field ") + key + " must be: " + rule);
        }
    };

    std::optional<std::string> id = text_of("id", true);
    if (id) {
        m.id = *id;
        check("id", id, kIdPattern, "[a-z0-9][a-z0-9-]{1,39}");
        if (*id != folderName) result.errors.push_back("id \"" + *id + "\" must be the folder name");
    }

    std::optional<std::string> name = text_of("name", true);
    if (name) {
        m.name = *name;
        if (CodePoints(*name) < 1 || CodePoints(*name) > 60) result.errors.push_back("name must be 1 to 60 characters");
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
        else result.errors.push_back("kind must be extension, theme, app or snippet");
    }

    std::optional<std::string> author = text_of("author", true);
    if (author) {
        m.author = *author;
        check("author", author, kAuthorPattern, "a GitHub username");
    }

    std::optional<std::string> description = text_of("description", true);
    if (description) {
        m.description = *description;
        if (CodePoints(*description) < 1 || CodePoints(*description) > 300) {
            result.errors.push_back("description must be 1 to 300 characters");
        }
    }

    std::optional<std::string> kitApi = text_of("kitApi", true);
    if (kitApi) {
        m.kitApi = *kitApi;
        if (!Matches(*kitApi, kKitApiPattern)) {
            result.errors.push_back("kitApi must be MAJOR.MINOR, for example 1.0");
        } else if (std::stoi(kitApi->substr(0, kitApi->find('.'))) != supportedKitApiMajor) {
            result.errors.push_back("kitApi " + *kitApi + ": major version not supported (expected " +
                                    std::to_string(supportedKitApiMajor) + ")");
        }
    }

    m.homepage = text_of("homepage", false);
    check("homepage", m.homepage, kHttpsPattern, "an https:// URL");
    m.license = text_of("license", false);
    if (m.license && (CodePoints(*m.license) < 1 || CodePoints(*m.license) > 64)) {
        result.errors.push_back("license must be 1 to 64 characters");
    }

    m.main = text_of("main", false);
    check("main", m.main, kJsFilePattern, "a .js file at the root");
    if (std::optional<std::string> type = text_of("type", false)) {
        if (*type == "module") m.module = true;
        else if (*type != "script") result.errors.push_back("type must be script or module");
    }
    m.colors = text_of("colors", false);
    check("colors", m.colors, kIniFilePattern, "an .ini file at the root");
    m.defaultScheme = text_of("defaultScheme", false);
    check("defaultScheme", m.defaultScheme, kSchemeNamePattern, "1 to 32 characters without spaces");
    m.resources = text_of("resources", false);
    check("resources", m.resources, kXamlFilePattern, "a .xaml file at the root");
    m.icon = text_of("icon", false);
    check("icon", m.icon, kSvgFilePattern, "an .svg file at the root");

    if (const Json* list = root->Find("permissions")) {
        if (!list->AsArray()) {
            result.errors.push_back("permissions must be a list");
        } else if (list->AsArray()->size() > 32) {
            result.errors.push_back("permissions: 32 at most");
        } else {
            for (const Json& item : *list->AsArray()) {
                const std::string* permission = item.AsString();
                if (!permission || !IsKnownPermission(*permission)) {
                    result.errors.push_back("unknown permission: " + (permission ? *permission : std::string("?")));
                } else if (m.HasPermission(*permission)) {
                    result.errors.push_back("duplicate permission: " + *permission);
                } else {
                    m.permissions.push_back(*permission);
                }
            }
        }
    }

    if (const Json* list = root->Find("appVersions")) {
        if (!list->AsArray()) {
            result.errors.push_back("appVersions must be a list");
        } else if (list->AsArray()->size() > 16) {
            result.errors.push_back("appVersions: 16 at most");
        } else {
            for (const Json& item : *list->AsArray()) {
                const std::string* v = item.AsString();
                if (!v || !Matches(*v, kAppVersionPattern)) {
                    result.errors.push_back("appVersions: invalid version");
                } else {
                    m.appVersions.push_back(*v);
                }
            }
        }
    }

    if (const Json* list = root->Find("processes")) {
        m.processes.clear();
        if (!list->AsArray() || list->AsArray()->empty() || list->AsArray()->size() > 2) {
            result.errors.push_back("processes must be a list of one or two values: ui, engine");
        } else {
            for (const Json& item : *list->AsArray()) {
                const std::string* process = item.AsString();
                if (!process || (*process != "ui" && *process != "engine")) {
                    result.errors.push_back("processes: unknown value (ui or engine)");
                } else if (std::find(m.processes.begin(), m.processes.end(), *process) != m.processes.end()) {
                    result.errors.push_back("processes: duplicate value: " + *process);
                } else {
                    m.processes.push_back(*process);
                }
            }
        }
    }

    // Rules that depend on the kind, from the table in docs/extensions/manifest.md.
    auto require_file = [&](const char* key, const std::optional<std::string>& file) {
        if (file && hasFile && !hasFile(*file)) result.errors.push_back(std::string("file missing: ") + *file + " (" + key + ")");
    };
    switch (m.kind) {
        case ElementKind::Extension:
        case ElementKind::App:
            if (!m.main) result.errors.push_back("main is required for an extension or an app");
            break;
        case ElementKind::Theme:
            if (!m.colors && !m.resources) result.errors.push_back("a theme needs colors, resources, or both");
            break;
        case ElementKind::Snippet:
            if (!m.resources) result.errors.push_back("resources is required for a snippet");
            if (m.main) result.errors.push_back("main is not allowed for a snippet");
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
