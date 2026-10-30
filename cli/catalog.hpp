#pragma once

#include <optional>
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
};

// A download address the kit accepts: https, or a file:// path when the catalog itself is local (development only).
bool IsAllowedDownloadUrl(std::string_view url, bool localCatalog);

// Parses catalog.json (schemas/catalog.schema.json). `localCatalog` is true when the file was read from disk.
// On failure returns nullopt and appends one French message per problem to `errors`.
std::optional<Catalog> ParseCatalog(std::string_view text, bool localCatalog, std::vector<std::string>& errors);

const CatalogEntry* FindCatalogEntry(const Catalog& catalog, std::string_view id);

}  // namespace amk
