#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "element.hpp"

namespace amk {

// One installable element. `url` is a zip archive whose root is the element folder; `sha256` pins its content.
struct CatalogEntry : ElementInfo {
    std::string url;
    std::string sha256;
};

struct Catalog {
    std::vector<CatalogEntry> entries;
    // Optional `versions` object: where catalog/versions.json is published, and the SHA-256 that pins it.
    std::string versionsUrl;
    std::string versionsSha256;
};

// One Apple Music build listed in catalog/versions.json (schemas/versions.schema.json): its package version, and
// the exports of AVFoundationCF.dll that were checked present on it.
struct TestedVersion {
    std::string version;  // "1.1540.23042.0"
    std::vector<std::string> exports;
};

struct VersionsCatalog {
    std::vector<TestedVersion> versions;
};

// A download address the kit accepts: https, or a file:// path when the catalog itself is local (development only).
bool IsAllowedDownloadUrl(std::string_view url, bool localCatalog);

// Parses catalog.json (schemas/catalog.schema.json). `localCatalog` is true when the file was read from disk.
// On failure returns nullopt and appends one French message per problem to `errors`.
std::optional<Catalog> ParseCatalog(std::string_view text, bool localCatalog, std::vector<std::string>& errors);

const CatalogEntry* FindCatalogEntry(const Catalog& catalog, std::string_view id);

// Parses catalog/versions.json. On failure returns nullopt and appends one French message per problem to `errors`.
std::optional<VersionsCatalog> ParseVersionsCatalog(std::string_view text, std::vector<std::string>& errors);

// The versions whose checked exports include every name of `required` (the exports this kit needs). A version
// checked against fewer names is not trusted by this kit, and goes to `rejected` with the first missing name.
std::vector<std::string> UsableVersions(const VersionsCatalog& catalog, std::span<const std::string_view> required,
                                        std::vector<std::string>& rejected);

}  // namespace amk
