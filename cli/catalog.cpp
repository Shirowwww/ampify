#include "catalog.hpp"

#include <algorithm>
#include <set>

#include "../core/build_signature.hpp"
#include "../core/version_list.hpp"
#include "sha256.hpp"

namespace amk {

namespace {

constexpr std::string_view kFilePrefix = "file://";

bool IsFileUrl(std::string_view url) {
    return url.size() > kFilePrefix.size() && url.substr(0, kFilePrefix.size()) == kFilePrefix &&
           url.find_first_of(" \t\r\n\"") == std::string_view::npos;
}

std::string EntryLabel(size_t index) {
    return "extensions[" + std::to_string(index) + "]";
}

}  // namespace

bool IsAllowedDownloadUrl(std::string_view url, bool localCatalog) {
    if (IsHttpsUrl(url)) return true;
    return localCatalog && IsFileUrl(url);
}

std::optional<Catalog> ParseCatalog(std::string_view text, bool localCatalog, std::vector<std::string>& errors) {
    std::string syntaxError;
    std::optional<json::Value> document = json::Parse(text, syntaxError);
    if (!document) {
        errors.push_back("JSON invalide : " + syntaxError);
        return std::nullopt;
    }
    if (!document->IsObject()) {
        errors.push_back("le catalogue doit être un objet JSON");
        return std::nullopt;
    }

    size_t before = errors.size();
    CheckAllowedKeys(*document, {"$schema", "catalogVersion", "extensions", "versions"}, "", errors);

    const json::Value* version = document->Find("catalogVersion");
    if (version == nullptr) {
        errors.push_back("catalogVersion : champ obligatoire absent");
    } else if (!version->IsNumber() || version->AsNumber() != 1) {
        errors.push_back("catalogVersion : seule la version 1 est connue");
    }

    const json::Value* entries = document->Find("extensions");
    if (entries == nullptr) {
        errors.push_back("extensions : champ obligatoire absent");
    } else if (!entries->IsArray()) {
        errors.push_back("extensions : liste attendue");
    }

    Catalog catalog;
    if (const json::Value* pin = document->Find("versions")) {
        if (!pin->IsObject()) {
            errors.push_back("versions : objet attendu");
        } else {
            CheckAllowedKeys(*pin, {"url", "sha256"}, "versions", errors);
            const json::Value* url = pin->Find("url");
            const json::Value* sha = pin->Find("sha256");
            if (url == nullptr || !url->IsString() || !IsAllowedDownloadUrl(url->AsString(), localCatalog)) {
                errors.push_back("versions.url : adresse https:// attendue");
            } else {
                catalog.versionsUrl = url->AsString();
            }
            if (sha == nullptr || !sha->IsString() || !IsSha256Hex(sha->AsString())) {
                errors.push_back("versions.sha256 : 64 caractères hexadécimaux minuscules attendus");
            } else {
                catalog.versionsSha256 = sha->AsString();
            }
        }
    }

    std::set<std::string> ids;
    if (entries != nullptr && entries->IsArray()) {
        for (size_t index = 0; index < entries->Items().size(); index++) {
            const json::Value& item = entries->Items()[index];
            std::string where = EntryLabel(index);
            if (!item.IsObject()) {
                errors.push_back(where + " : objet attendu");
                continue;
            }

            size_t entryBefore = errors.size();
            CheckAllowedKeys(item,
                             {"id", "name", "version", "kind", "author", "description", "kitApi", "url", "sha256",
                              "homepage", "license", "permissions"},
                             where, errors);

            CatalogEntry entry;
            ReadElementInfo(item, where, entry, errors);

            const json::Value* url = item.Find("url");
            if (url == nullptr) {
                errors.push_back(where + ".url : champ obligatoire absent");
            } else if (!url->IsString() || !IsAllowedDownloadUrl(url->AsString(), localCatalog)) {
                errors.push_back(where + ".url : adresse https:// attendue");
            } else {
                entry.url = url->AsString();
            }

            const json::Value* sha = item.Find("sha256");
            if (sha == nullptr) {
                errors.push_back(where + ".sha256 : champ obligatoire absent");
            } else if (!sha->IsString() || !IsSha256Hex(sha->AsString())) {
                errors.push_back(where + ".sha256 : 64 caractères hexadécimaux minuscules attendus");
            } else {
                entry.sha256 = sha->AsString();
            }

            if (!entry.id.empty() && !ids.insert(entry.id).second) {
                errors.push_back(where + ".id : identifiant déjà présent dans le catalogue : " + entry.id);
            }
            if (errors.size() == entryBefore) catalog.entries.push_back(std::move(entry));
        }
    }

    if (errors.size() != before) return std::nullopt;
    return catalog;
}

std::optional<VersionsCatalog> ParseVersionsCatalog(std::string_view text, std::vector<std::string>& errors) {
    std::string syntaxError;
    std::optional<json::Value> document = json::Parse(text, syntaxError);
    if (!document) {
        errors.push_back("JSON invalide : " + syntaxError);
        return std::nullopt;
    }
    if (!document->IsObject()) {
        errors.push_back("la liste des versions doit être un objet JSON");
        return std::nullopt;
    }

    size_t before = errors.size();
    CheckAllowedKeys(*document, {"$schema", "versionsFormat", "playerDll", "versions"}, "", errors);
    const json::Value* format = document->Find("versionsFormat");
    if (format == nullptr || !format->IsNumber() || format->AsNumber() != 1) {
        errors.push_back("versionsFormat : seule la version 1 est connue");
    }
    const json::Value* dll = document->Find("playerDll");
    if (dll == nullptr || !dll->IsString() || dll->AsString() != kPlayerDllName) {
        errors.push_back("playerDll : " + std::string(kPlayerDllName) + " attendu");
    }

    VersionsCatalog catalog;
    const json::Value* versions = document->Find("versions");
    if (versions == nullptr || !versions->IsArray()) {
        errors.push_back("versions : liste attendue");
        return std::nullopt;
    }
    std::set<std::string> seen;
    for (size_t index = 0; index < versions->Items().size(); index++) {
        const json::Value& item = versions->Items()[index];
        std::string where = "versions[" + std::to_string(index) + "]";
        if (!item.IsObject()) {
            errors.push_back(where + " : objet attendu");
            continue;
        }
        CheckAllowedKeys(item, {"version", "tested", "exports", "notes"}, where, errors);

        TestedVersion tested;
        const json::Value* version = item.Find("version");
        if (version == nullptr || !version->IsString() || !IsPackageVersionText(version->AsString())) {
            errors.push_back(where + ".version : version de paquet attendue (quatre nombres, comme 1.1540.23042.0)");
        } else {
            tested.version = version->AsString();
            if (!seen.insert(tested.version).second) errors.push_back(where + ".version : déjà listée");
        }

        const json::Value* exports = item.Find("exports");
        if (exports == nullptr || !exports->IsArray() || exports->Items().empty()) {
            errors.push_back(where + ".exports : liste non vide attendue");
        } else {
            for (const json::Value& name : exports->Items()) {
                if (!name.IsString() || name.AsString().empty() ||
                    name.AsString().find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
                        std::string::npos) {
                    errors.push_back(where + ".exports : nom d'export invalide");
                    break;
                }
                tested.exports.push_back(name.AsString());
            }
        }
        catalog.versions.push_back(std::move(tested));
    }

    if (errors.size() != before) return std::nullopt;
    return catalog;
}

std::vector<std::string> UsableVersions(const VersionsCatalog& catalog, std::span<const std::string_view> required,
                                        std::vector<std::string>& rejected) {
    std::vector<std::string> usable;
    for (const TestedVersion& tested : catalog.versions) {
        std::string missing;
        for (std::string_view name : required) {
            if (std::find(tested.exports.begin(), tested.exports.end(), name) == tested.exports.end()) {
                missing = std::string(name);
                break;
            }
        }
        if (missing.empty()) {
            usable.push_back(tested.version);
        } else {
            rejected.push_back(tested.version + " (export non vérifié : " + missing + ")");
        }
    }
    return usable;
}

const CatalogEntry* FindCatalogEntry(const Catalog& catalog, std::string_view id) {
    auto found = std::find_if(catalog.entries.begin(), catalog.entries.end(),
                              [id](const CatalogEntry& entry) { return entry.id == id; });
    return found == catalog.entries.end() ? nullptr : &*found;
}

}  // namespace amk
