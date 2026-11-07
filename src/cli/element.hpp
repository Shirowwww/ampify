#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../core/json.hpp"
#include "../core/manifest.hpp"
#include "release_facts.hpp"

namespace ampify {

// Largest preview image the Marketplace page is asked to load.
inline constexpr std::uintmax_t kMaxPreviewBytes = 2 * 1024 * 1024;

// Rejects members that the schema does not list. `allowed` is the complete list of member names.
void CheckAllowedKeys(const Json& object, std::initializer_list<std::string_view> allowed,
                      const std::string& where, std::vector<std::string>& errors);

// Rules of a folder that the manifest cannot tell: a preview image is at most kMaxPreviewBytes, and no symbolic link
// is in the folder (the kernel would refuse to import through it).
std::vector<std::string> CheckElementFolder(const Manifest& manifest, const std::filesystem::path& folder);

struct FolderValidation {
    std::optional<Manifest> manifest;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    bool ok() const { return manifest.has_value() && errors.empty(); }
};

// Reads <folder>/manifest.json under the authoring rules (ManifestMode::Author), then runs the folder checks.
FolderValidation ValidateElementFolder(const std::filesystem::path& folder);

// The last name of a folder path, so "folder/" and "folder" both give "folder".
std::string FolderName(const std::filesystem::path& folder);

}  // namespace ampify
