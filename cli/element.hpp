#pragma once

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace amk {

// Major version of the extension API this kit implements (docs/extensions/API.md, section 9).
inline constexpr int kKitApiMajor = 1;

// Fields that a manifest.json and an entry of catalog.json share.
struct ElementInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string kind;  // extension, theme, app or snippet
    std::string author;
    std::string description;
    std::string kitApi;
    std::optional<std::string> homepage;
    std::optional<std::string> license;
    std::vector<std::string> permissions;
};

// What a manifest.json declares, as docs/extensions/MANIFEST.md describes it.
struct ElementManifest : ElementInfo {
    std::optional<std::string> main;
    std::optional<std::string> colors;
    std::optional<std::string> defaultScheme;
    std::optional<std::string> resources;
    std::optional<std::string> icon;
    std::vector<std::string> appVersions;
    // Processes where the element runs: "ui" (AppleMusic.exe), "engine" (AMPLibraryAgent.exe). Empty means both.
    std::vector<std::string> processes;
    // How main runs: "script" (the default) or "module" (ES module, with relative imports inside the folder).
    std::optional<std::string> type;
};

// The checks below mirror schemas/*.schema.json. They never touch the disk.
bool IsValidId(std::string_view text);
bool IsValidVersion(std::string_view text);
bool IsValidKind(std::string_view text);
bool IsValidPermission(std::string_view text);
bool IsValidScheme(std::string_view text);
bool IsHttpsUrl(std::string_view text);

// Parses manifest text. On failure returns nullopt and appends one French message per problem to `errors`.
std::optional<ElementManifest> ParseManifest(std::string_view text, std::vector<std::string>& errors);

// Parses the fields every element has. `where` prefixes each message ("extensions[2]"), empty at the top level.
bool ReadElementInfo(const json::Value& object, const std::string& where, ElementInfo& info,
                     std::vector<std::string>& errors);

// Rejects members that the schema does not list. `allowed` is the complete list of member names.
void CheckAllowedKeys(const json::Value& object, std::initializer_list<std::string_view> allowed,
                      const std::string& where, std::vector<std::string>& errors);

// Rules a schema cannot express: the id is the folder name, the files that the kind needs exist, and no symbolic
// link is in the folder (the kernel would refuse to import through it).
std::vector<std::string> CheckElementFolder(const ElementManifest& manifest, const std::filesystem::path& folder);

struct FolderValidation {
    std::optional<ElementManifest> manifest;
    std::vector<std::string> errors;

    bool ok() const { return manifest.has_value() && errors.empty(); }
};

// Reads <folder>/manifest.json, then runs the schema and folder checks.
FolderValidation ValidateElementFolder(const std::filesystem::path& folder);

// The last name of a folder path, so "dossier/" and "dossier" both give "dossier".
std::string FolderName(const std::filesystem::path& folder);

// Number of characters in UTF-8 text, the unit the schema uses for minLength and maxLength.
size_t CharacterCount(std::string_view text);

}  // namespace amk
