#include "element.hpp"

#include <algorithm>

#include "../core/text_util.hpp"

namespace ampify {

void CheckAllowedKeys(const Json& object, std::initializer_list<std::string_view> allowed,
                      const std::string& where, std::vector<std::string>& errors) {
    if (!object.IsObject()) return;
    for (const auto& member : *object.AsObject()) {
        const std::string& key = member.first;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            errors.push_back((where.empty() ? key : where + "." + key) + ": unknown field");
        }
    }
}

std::string FolderName(const std::filesystem::path& folder) {
    std::filesystem::path clean = std::filesystem::absolute(folder).lexically_normal();
    if (!clean.has_filename()) clean = clean.parent_path();
    return clean.filename().string();
}

std::vector<std::string> CheckElementFolder(const Manifest& manifest, const std::filesystem::path& folder) {
    std::vector<std::string> errors;
    std::error_code ec;
    if (manifest.preview) {
        // A missing file is reported by the manifest's rules: file_size then fails and nothing more is said.
        std::uintmax_t size = std::filesystem::file_size(folder / *manifest.preview, ec);
        if (!ec && size > kMaxPreviewBytes) {
            errors.push_back("preview: file larger than 2 MB: " + *manifest.preview);
        }
        ec.clear();
    }
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
    std::optional<std::string> text = ReadWholeFile(folder / "manifest.json");
    if (!text) {
        result.errors.push_back("manifest.json missing from the folder");
        return result;
    }
    ManifestResult parsed = ParseManifest(*text, FolderName(folder), kKitApiMajor, [&](const std::string& file) {
        std::error_code ec;
        return std::filesystem::is_regular_file(folder / file, ec);
    }, ManifestMode::Author);
    result.manifest = std::move(parsed.manifest);
    result.errors = std::move(parsed.errors);
    result.warnings = std::move(parsed.warnings);
    if (result.manifest) {
        for (std::string& message : CheckElementFolder(*result.manifest, folder)) {
            result.errors.push_back(std::move(message));
        }
    }
    return result;
}

}  // namespace ampify
