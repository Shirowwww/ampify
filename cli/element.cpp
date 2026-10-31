#include "element.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace ampify {

namespace {

// One string rule: the same pattern, length limits and French hint as the JSON schemas.
struct TextRule {
    const char* pattern;  // ECMAScript, must match the whole value
    size_t minLength;     // in characters, 0 for none
    size_t maxLength;     // in characters, 0 for none
    const char* hint;     // what a valid value looks like, shown after the field name
};

constexpr TextRule kIdRule{R"re([a-z0-9][a-z0-9-]{1,39})re", 0, 0,
                           "lowercase letters, digits and hyphens, 2 to 40 characters"};
constexpr TextRule kNameRule{R"re([\s\S]*)re", 1, 60, "1 to 60 characters"};
constexpr TextRule kVersionRule{R"re([0-9]+\.[0-9]+\.[0-9]+)re", 0, 0, "format MAJOR.MINOR.PATCH"};
constexpr TextRule kKindRule{R"re(extension|theme|app|snippet)re", 0, 0,
                             "extension, theme, app or snippet"};
constexpr TextRule kAuthorRule{R"re([A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?)re", 0, 0,
                               "GitHub username"};
constexpr TextRule kDescriptionRule{R"re([\s\S]*)re", 1, 300, "1 to 300 characters"};
constexpr TextRule kKitApiRule{R"re([0-9]+\.[0-9]+)re", 0, 0, "format MAJOR.MINOR"};
constexpr TextRule kHttpsRule{R"re(https://\S+)re", 0, 512, "https:// address"};
constexpr TextRule kLicenseRule{R"re([\s\S]*)re", 1, 64, "1 to 64 characters"};
constexpr TextRule kMainRule{R"re([A-Za-z0-9][A-Za-z0-9._-]{0,63}\.js)re", 0, 0,
                             "a .js file name, at the root of the folder"};
constexpr TextRule kColorsRule{R"re([A-Za-z0-9][A-Za-z0-9._-]{0,63}\.ini)re", 0, 0,
                               "a .ini file name, at the root of the folder"};
constexpr TextRule kSchemeRule{R"re([A-Za-z0-9_-]{1,32})re", 0, 0, "1 to 32 letters, digits, _ or -"};
constexpr TextRule kResourcesRule{R"re([A-Za-z0-9][A-Za-z0-9._-]{0,63}\.xaml)re", 0, 0,
                                  "a .xaml file name, at the root of the folder"};
constexpr TextRule kIconRule{R"re([A-Za-z0-9][A-Za-z0-9._-]{0,63}\.svg)re", 0, 0,
                             "a .svg file name, at the root of the folder"};
constexpr TextRule kPermissionRule{
    R"re((player\.read|player\.control|audio\.analyze|audio\.gain|audio\.eq|storage|notifications|ipc\.discord|midi\.input|library\.read|library\.write|services\.read|ui\.keyboard|ui\.topbar|ui\.playbar|ui\.sidebar|ui\.menu|ui\.popup|ui\.fullscreen|ui\.panel|ui\.page|ui\.window|ui\.tray|fs\.output|kit\.manage|network:[a-z0-9.-]{1,253}))re",
    0, 0, "a permission from docs/extensions/api.md, section 4"};
constexpr TextRule kTypeRule{R"re(script|module)re", 0, 0, "script or module"};
constexpr TextRule kProcessRule{R"re(ui|engine)re", 0, 0, "ui or engine"};
constexpr TextRule kAppVersionRule{R"re([0-9]+(\.[0-9]+){1,3})re", 0, 0, "Apple Music version, for example 1.1540.23042.0"};

constexpr size_t kMaxPermissions = 32;
constexpr size_t kMaxAppVersions = 16;
constexpr size_t kMaxProcesses = 2;

std::string Label(const std::string& where, std::string_view key) {
    return where.empty() ? std::string(key) : where + "." + std::string(key);
}

bool Matches(std::string_view value, const TextRule& rule) {
    size_t length = CharacterCount(value);
    if (length < rule.minLength) return false;
    if (rule.maxLength != 0 && length > rule.maxLength) return false;
    // Built once per call: manifests are small, and a static cache would make the rules order-dependent.
    std::regex pattern(rule.pattern, std::regex::ECMAScript);
    return std::regex_match(value.begin(), value.end(), pattern);
}

// Reads a string member. Returns nullopt when it is absent (and optional) or invalid, recording why.
std::optional<std::string> ReadText(const json::Value& object, std::string_view key, const std::string& where,
                                    const TextRule& rule, bool required, std::vector<std::string>& errors) {
    std::string label = Label(where, key);
    const json::Value* member = object.Find(key);
    if (member == nullptr) {
        if (required) errors.push_back(label + ": required field missing");
        return std::nullopt;
    }
    if (!member->IsString()) {
        errors.push_back(label + ": text expected");
        return std::nullopt;
    }
    if (!Matches(member->AsString(), rule)) {
        errors.push_back(label + ": invalid value (" + rule.hint + ")");
        return std::nullopt;
    }
    return member->AsString();
}

// Reads an array of strings: unique items, each matching the rule, at most `maxItems`.
std::vector<std::string> ReadList(const json::Value& object, std::string_view key, const std::string& where,
                                  const TextRule& rule, size_t maxItems, std::vector<std::string>& errors) {
    std::vector<std::string> result;
    const json::Value* member = object.Find(key);
    if (member == nullptr) return result;

    std::string label = Label(where, key);
    if (!member->IsArray()) {
        errors.push_back(label + ": list expected");
        return result;
    }
    if (member->Items().size() > maxItems) {
        errors.push_back(label + ": at most " + std::to_string(maxItems) + " items");
    }

    std::set<std::string> seen;
    for (size_t i = 0; i < member->Items().size(); i++) {
        const json::Value& item = member->Items()[i];
        std::string itemLabel = label + "[" + std::to_string(i) + "]";
        if (!item.IsString()) {
            errors.push_back(itemLabel + ": text expected");
            continue;
        }
        if (!Matches(item.AsString(), rule)) {
            errors.push_back(itemLabel + ": invalid value (" + rule.hint + ")");
            continue;
        }
        if (!seen.insert(item.AsString()).second) {
            errors.push_back(itemLabel + ": duplicate value: " + item.AsString());
            continue;
        }
        result.push_back(item.AsString());
    }
    return result;
}

std::optional<std::string> ReadOptionalText(const json::Value& object, std::string_view key,
                                            const std::string& where, const TextRule& rule,
                                            std::vector<std::string>& errors) {
    return ReadText(object, key, where, rule, false, errors);
}

}  // namespace

bool IsValidId(std::string_view text) {
    return Matches(text, kIdRule);
}

bool IsValidVersion(std::string_view text) {
    return Matches(text, kVersionRule);
}

bool IsValidKind(std::string_view text) {
    return Matches(text, kKindRule);
}

bool IsValidPermission(std::string_view text) {
    return Matches(text, kPermissionRule);
}

bool IsValidScheme(std::string_view text) {
    return Matches(text, kSchemeRule);
}

bool IsHttpsUrl(std::string_view text) {
    return Matches(text, kHttpsRule);
}

size_t CharacterCount(std::string_view text) {
    size_t count = 0;
    for (unsigned char byte : text) {
        // Every byte that is not a UTF-8 continuation byte starts a new character.
        if ((byte & 0xC0) != 0x80) count++;
    }
    return count;
}

void CheckAllowedKeys(const json::Value& object, std::initializer_list<std::string_view> allowed,
                      const std::string& where, std::vector<std::string>& errors) {
    if (!object.IsObject()) return;
    for (const std::string& key : object.Keys()) {
        bool known = false;
        for (std::string_view name : allowed) {
            if (key == name) {
                known = true;
                break;
            }
        }
        if (!known) errors.push_back(Label(where, key) + ": unknown field");
    }
}

bool ReadElementInfo(const json::Value& object, const std::string& where, ElementInfo& info,
                     std::vector<std::string>& errors) {
    size_t before = errors.size();
    info.id = ReadText(object, "id", where, kIdRule, true, errors).value_or("");
    info.name = ReadText(object, "name", where, kNameRule, true, errors).value_or("");
    info.version = ReadText(object, "version", where, kVersionRule, true, errors).value_or("");
    info.kind = ReadText(object, "kind", where, kKindRule, true, errors).value_or("");
    info.author = ReadText(object, "author", where, kAuthorRule, true, errors).value_or("");
    info.description = ReadText(object, "description", where, kDescriptionRule, true, errors).value_or("");
    info.kitApi = ReadText(object, "kitApi", where, kKitApiRule, true, errors).value_or("");
    info.homepage = ReadOptionalText(object, "homepage", where, kHttpsRule, errors);
    info.license = ReadOptionalText(object, "license", where, kLicenseRule, errors);
    info.permissions = ReadList(object, "permissions", where, kPermissionRule, kMaxPermissions, errors);

    if (!info.kitApi.empty()) {
        // The pattern already guarantees digits before the dot. A huge major is simply not supported.
        std::string major = info.kitApi.substr(0, info.kitApi.find('.'));
        if (major != std::to_string(kKitApiMajor)) {
            errors.push_back(Label(where, "kitApi") + ": unsupported API version (expected " +
                             std::to_string(kKitApiMajor) + ".x)");
        }
    }
    return errors.size() == before;
}

std::optional<ElementManifest> ParseManifest(std::string_view text, std::vector<std::string>& errors) {
    std::string syntaxError;
    std::optional<json::Value> document = json::Parse(text, syntaxError);
    if (!document) {
        errors.push_back("invalid JSON: " + syntaxError);
        return std::nullopt;
    }
    if (!document->IsObject()) {
        errors.push_back("the manifest must be a JSON object");
        return std::nullopt;
    }

    size_t before = errors.size();
    CheckAllowedKeys(*document,
                     {"$schema", "id", "name", "version", "kind", "author", "description", "kitApi", "homepage",
                      "license", "main", "colors", "defaultScheme", "resources", "icon", "permissions",
                      "appVersions", "processes", "type"},
                     "", errors);

    ElementManifest manifest;
    ReadElementInfo(*document, "", manifest, errors);
    manifest.main = ReadOptionalText(*document, "main", "", kMainRule, errors);
    manifest.colors = ReadOptionalText(*document, "colors", "", kColorsRule, errors);
    manifest.defaultScheme = ReadOptionalText(*document, "defaultScheme", "", kSchemeRule, errors);
    manifest.resources = ReadOptionalText(*document, "resources", "", kResourcesRule, errors);
    manifest.icon = ReadOptionalText(*document, "icon", "", kIconRule, errors);
    manifest.type = ReadOptionalText(*document, "type", "", kTypeRule, errors);
    manifest.appVersions = ReadList(*document, "appVersions", "", kAppVersionRule, kMaxAppVersions, errors);
    manifest.processes = ReadList(*document, "processes", "", kProcessRule, kMaxProcesses, errors);
    if (document->Find("processes") != nullptr && manifest.processes.empty()) {
        errors.push_back("processes: at least one process, ui or engine");
    }

    if (errors.size() != before) return std::nullopt;
    return manifest;
}

std::string FolderName(const std::filesystem::path& folder) {
    std::filesystem::path clean = std::filesystem::absolute(folder).lexically_normal();
    if (!clean.has_filename()) clean = clean.parent_path();
    return clean.filename().string();
}

std::vector<std::string> CheckElementFolder(const ElementManifest& manifest, const std::filesystem::path& folder) {
    std::vector<std::string> errors;
    std::string folderName = FolderName(folder);
    if (manifest.id != folderName) {
        errors.push_back("id: must be the folder name (" + folderName + ")");
    }

    const std::string& kind = manifest.kind;
    if ((kind == "extension" || kind == "app") && !manifest.main) {
        errors.push_back("main: required for an " + kind);
    }
    if (kind == "theme" && !manifest.colors && !manifest.resources) {
        errors.push_back("colors or resources: at least one of the two is required for a theme");
    }
    if (kind == "snippet") {
        if (!manifest.resources) errors.push_back("resources: required for a snippet");
        if (manifest.main) errors.push_back("main: not allowed for a snippet");
    }

    const std::pair<const char*, const std::optional<std::string>*> files[] = {
        {"main", &manifest.main},
        {"colors", &manifest.colors},
        {"resources", &manifest.resources},
        {"icon", &manifest.icon},
    };
    for (const auto& [key, file] : files) {
        if (!file->has_value()) continue;
        if (!std::filesystem::is_regular_file(folder / **file)) {
            errors.push_back(std::string(key) + ": file missing from the folder: " + **file);
        }
    }

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_symlink(ec)) continue;
        std::string relative = it->path().lexically_relative(folder).generic_string();
        errors.push_back("symbolic link not allowed in the element: " + relative);
    }
    return errors;
}

FolderValidation ValidateElementFolder(const std::filesystem::path& folder) {
    FolderValidation result;
    std::filesystem::path manifestPath = folder / "manifest.json";

    std::ifstream in(manifestPath, std::ios::binary);
    if (!in) {
        result.errors.push_back("manifest.json missing from the folder");
        return result;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();

    result.manifest = ParseManifest(buffer.str(), result.errors);
    if (result.manifest) {
        for (std::string& message : CheckElementFolder(*result.manifest, folder)) {
            result.errors.push_back(std::move(message));
        }
    }
    return result;
}

}  // namespace ampify
