#include "catalog.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "../core/build_signature.hpp"
#include "../core/text_util.hpp"
#include "../core/version_list.hpp"
#include "sha256.hpp"

namespace ampify {

namespace {

constexpr std::string_view kFilePrefix = "file://";

bool IsFileUrl(std::string_view url) {
    return url.size() > kFilePrefix.size() && url.substr(0, kFilePrefix.size()) == kFilePrefix &&
           url.find_first_of(" \t\r\n\"") == std::string_view::npos;
}

std::string EntryLabel(size_t index) {
    return "extensions[" + std::to_string(index) + "]";
}

// The https address of a card image: a .png, .jpg or .jpeg.
bool IsPreviewUrl(std::string_view url) {
    if (!IsHttpsAddress(url, kMaxAddressLength)) return false;
    constexpr size_t kScheme = std::string_view("https://").size();
    for (std::string_view extension : {".png", ".jpg", ".jpeg"}) {
        if (url.size() > kScheme + extension.size() && url.ends_with(extension)) return true;
    }
    return false;
}

// A permission only an element of kKitElements may hold, such as kit.manage, which runs ampify itself: only the
// Marketplace that ships with the kit has it, never a download.
void RefuseFirstPartyPermissions(const ElementInfo& element, const std::string& where,
                                 std::vector<std::string>& errors) {
    for (const std::string& name : element.permissions) {
        const PermissionInfo* permission = FindPermission(name);
        if (permission && permission->firstPartyOnly) {
            errors.push_back(where + ".permissions: " + name + " is reserved for Ampify's Marketplace");
        }
    }
}

}  // namespace

std::string UrlHost(std::string_view url) {
    constexpr std::string_view kScheme = "https://";
    if (url.substr(0, kScheme.size()) != kScheme) return {};
    std::string_view rest = url.substr(kScheme.size());
    std::string host(rest.substr(0, rest.find_first_of("/?#\\")));
    if (host.find('@') != std::string::npos) return {};
    return ToLowerAscii(host);
}

bool IsAllowedDownloadUrl(std::string_view url, bool localCatalog) {
    if (IsHttpsAddress(url, kMaxAddressLength)) return true;
    return localCatalog && IsFileUrl(url);
}

std::optional<Catalog> ParseCatalog(std::string_view text, bool localCatalog, std::vector<std::string>& errors) {
    std::string syntaxError;
    std::optional<Json> document = Json::Parse(text, &syntaxError, Json::DuplicateKeys::Refuse);
    if (!document) {
        errors.push_back("invalid JSON: " + syntaxError);
        return std::nullopt;
    }
    if (!document->IsObject()) {
        errors.push_back("the catalog must be a JSON object");
        return std::nullopt;
    }

    size_t before = errors.size();
    CheckAllowedKeys(*document, {"$schema", "catalogVersion", "extensions", "versions"}, "", errors);

    const Json* version = document->Find("catalogVersion");
    if (version == nullptr) {
        errors.push_back("catalogVersion: required field missing");
    } else if (!version->IsNumber() || *version->AsNumber() != 1) {
        errors.push_back("catalogVersion: only version 1 is known");
    }

    const Json* entries = document->Find("extensions");
    if (entries == nullptr) {
        errors.push_back("extensions: required field missing");
    } else if (!entries->IsArray()) {
        errors.push_back("extensions: list expected");
    }

    Catalog catalog;
    if (const Json* pin = document->Find("versions")) {
        if (!pin->IsObject()) {
            errors.push_back("versions: object expected");
        } else {
            CheckAllowedKeys(*pin, {"url", "sha256"}, "versions", errors);
            const Json* url = pin->Find("url");
            const Json* sha = pin->Find("sha256");
            if (url == nullptr || !url->IsString() || !IsAllowedDownloadUrl(*url->AsString(), localCatalog)) {
                errors.push_back("versions.url: https:// address expected");
            } else {
                catalog.versionsUrl = *url->AsString();
            }
            if (sha == nullptr || !sha->IsString() || !IsSha256Hex(*sha->AsString())) {
                errors.push_back("versions.sha256: 64 lowercase hexadecimal characters expected");
            } else {
                catalog.versionsSha256 = *sha->AsString();
            }
        }
    }

    std::set<std::string> ids;
    if (entries != nullptr && entries->IsArray()) {
        for (size_t index = 0; index < entries->AsArray()->size(); index++) {
            const Json& item = (*entries->AsArray())[index];
            std::string where = EntryLabel(index);
            if (!item.IsObject()) {
                errors.push_back(where + ": object expected");
                continue;
            }

            // Fields this version does not know are left aside, so a catalog that gains one keeps working here.
            size_t entryBefore = errors.size();
            CatalogEntry entry;
            ReadElementInfo(item, where, ManifestMode::Author, kKitApiMajor, entry, errors);
            RefuseFirstPartyPermissions(entry, where, errors);

            const Json* url = item.Find("url");
            if (url == nullptr) {
                errors.push_back(where + ".url: required field missing");
            } else if (!url->IsString() || !IsAllowedDownloadUrl(*url->AsString(), localCatalog)) {
                errors.push_back(where + ".url: https:// address expected");
            } else {
                entry.url = *url->AsString();
            }

            if (const Json* preview = item.Find("preview")) {
                if (!preview->IsString() || !IsPreviewUrl(*preview->AsString())) {
                    errors.push_back(where + ".preview: https:// address of a .png, .jpg or .jpeg image expected");
                } else {
                    entry.preview = *preview->AsString();
                }
            }

            const Json* sha = item.Find("sha256");
            if (sha == nullptr) {
                errors.push_back(where + ".sha256: required field missing");
            } else if (!sha->IsString() || !IsSha256Hex(*sha->AsString())) {
                errors.push_back(where + ".sha256: 64 lowercase hexadecimal characters expected");
            } else {
                entry.sha256 = *sha->AsString();
            }

            if (!entry.id.empty() && !ids.insert(entry.id).second) {
                errors.push_back(where + ".id: id already in the catalog: " + entry.id);
            }
            if (errors.size() == entryBefore) catalog.entries.push_back(std::move(entry));
        }
    }

    if (errors.size() != before) return std::nullopt;
    return catalog;
}

std::optional<VersionsCatalog> ParseVersionsCatalog(std::string_view text, std::vector<std::string>& errors) {
    std::string syntaxError;
    std::optional<Json> document = Json::Parse(text, &syntaxError, Json::DuplicateKeys::Refuse);
    if (!document) {
        errors.push_back("invalid JSON: " + syntaxError);
        return std::nullopt;
    }
    if (!document->IsObject()) {
        errors.push_back("the list of versions must be a JSON object");
        return std::nullopt;
    }

    size_t before = errors.size();
    CheckAllowedKeys(*document, {"$schema", "versionsFormat", "playerDll", "versions"}, "", errors);
    const Json* format = document->Find("versionsFormat");
    if (format == nullptr || !format->IsNumber() || *format->AsNumber() != 1) {
        errors.push_back("versionsFormat: only version 1 is known");
    }
    const Json* dll = document->Find("playerDll");
    if (dll == nullptr || !dll->IsString() || *dll->AsString() != kPlayerDllName) {
        errors.push_back("playerDll: " + std::string(kPlayerDllName) + " expected");
    }

    VersionsCatalog catalog;
    const Json* versions = document->Find("versions");
    if (versions == nullptr || !versions->IsArray()) {
        errors.push_back("versions: list expected");
        return std::nullopt;
    }
    std::set<std::string> seen;
    for (size_t index = 0; index < versions->AsArray()->size(); index++) {
        const Json& item = (*versions->AsArray())[index];
        std::string where = "versions[" + std::to_string(index) + "]";
        if (!item.IsObject()) {
            errors.push_back(where + ": object expected");
            continue;
        }
        CheckAllowedKeys(item, {"version", "tested", "exports", "notes"}, where, errors);

        TestedVersion tested;
        const Json* version = item.Find("version");
        if (version == nullptr || !version->IsString() || !IsPackageVersionText(*version->AsString())) {
            errors.push_back(where + ".version: package version expected (four numbers, like 1.1540.23042.0)");
        } else {
            tested.version = *version->AsString();
            if (!seen.insert(tested.version).second) errors.push_back(where + ".version: already listed");
        }

        const Json* exports = item.Find("exports");
        if (exports == nullptr || !exports->IsArray() || exports->AsArray()->empty()) {
            errors.push_back(where + ".exports: non-empty list expected");
        } else {
            for (const Json& name : *exports->AsArray()) {
                if (!name.IsString() || name.AsString()->empty() ||
                    name.AsString()->find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
                        std::string::npos) {
                    errors.push_back(where + ".exports: invalid export name");
                    break;
                }
                tested.exports.push_back(*name.AsString());
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
            rejected.push_back(tested.version + " (export not verified: " + missing + ")");
        }
    }
    return usable;
}

const CatalogEntry* FindCatalogEntry(const Catalog& catalog, std::string_view id) {
    auto found = std::find_if(catalog.entries.begin(), catalog.entries.end(),
                              [id](const CatalogEntry& entry) { return entry.id == id; });
    return found == catalog.entries.end() ? nullptr : &*found;
}

std::string CheckArchiveManifest(const ElementInfo& manifest, const CatalogEntry& entry) {
    if (manifest.id != entry.id || manifest.version != entry.version || manifest.kind != entry.kind) {
        return "the manifest in the archive does not match the catalog";
    }
    // The kernel grants what the manifest asks for, and the user agreed to what the entry showed.
    std::set<std::string> asked(manifest.permissions.begin(), manifest.permissions.end());
    if (asked != std::set<std::string>(entry.permissions.begin(), entry.permissions.end())) {
        return "the permissions in the archive differ from the catalog's";
    }
    std::vector<std::string> errors;
    RefuseFirstPartyPermissions(manifest, "manifest", errors);
    return errors.empty() ? std::string() : errors.front();
}

}  // namespace ampify
