#include "catalog.hpp"

#include <algorithm>
#include <set>

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
    CheckAllowedKeys(*document, {"$schema", "catalogVersion", "extensions"}, "", errors);

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

const CatalogEntry* FindCatalogEntry(const Catalog& catalog, std::string_view id) {
    auto found = std::find_if(catalog.entries.begin(), catalog.entries.end(),
                              [id](const CatalogEntry& entry) { return entry.id == id; });
    return found == catalog.entries.end() ? nullptr : &*found;
}

}  // namespace amk
