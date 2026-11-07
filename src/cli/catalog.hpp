#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "element.hpp"

namespace ampify {

// One installable element. `url` is a zip archive whose root is the element folder; `sha256` pins its content.
struct CatalogEntry : ElementInfo {
    std::string url;
    std::string sha256;
    std::optional<std::string> preview;  // https address of the card image
};

struct Catalog {
    std::vector<CatalogEntry> entries;
    // Optional `versions` object: where versions.json (ampify-catalog) is published, and the SHA-256 that pins it.
    std::string versionsUrl;
    std::string versionsSha256;
    // Address the catalog was read from. Empty for a local file. Set by LoadCatalog, not by ParseCatalog.
    std::string sourceUrl;
};

// One Apple Music build listed in versions.json of ampify-catalog (schemas/versions.schema.json): its package version, and
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

// Host (and port) of an https address, in lowercase. Empty for anything else, or when the address has a user part.
std::string UrlHost(std::string_view url);

// Parses catalog.json (schemas/catalog.schema.json). `localCatalog` is true when the file was read from disk.
// On failure returns nullopt and appends one message per problem to `errors`.
std::optional<Catalog> ParseCatalog(std::string_view text, bool localCatalog, std::vector<std::string>& errors);

const CatalogEntry* FindCatalogEntry(const Catalog& catalog, std::string_view id);

// Why the manifest found in an entry's archive cannot be installed, or empty: it must be the element the entry showed
// (id, version, kind and the same permissions, in any order), with no permission reserved for Ampify's Marketplace.
std::string CheckArchiveManifest(const ElementInfo& manifest, const CatalogEntry& entry);

// Parses versions.json. On failure returns nullopt and appends one message per problem to `errors`.
std::optional<VersionsCatalog> ParseVersionsCatalog(std::string_view text, std::vector<std::string>& errors);

// The versions whose checked exports include every name of `required` (the exports this kit needs). A version
// checked against fewer names is not trusted by this kit, and goes to `rejected` with the first missing name.
std::vector<std::string> UsableVersions(const VersionsCatalog& catalog, std::span<const std::string_view> required,
                                        std::vector<std::string>& rejected);

}  // namespace ampify
