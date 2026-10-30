#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace amk::theme {

// True for a path that stays inside the theme folder: relative, no drive letter, no "..", no backslash tricks.
bool IsSafeRelativePath(std::string_view path);

// Where the theme files come from. The tests use memory; the kernel uses the disk.
class FileSource {
public:
    virtual ~FileSource() = default;

    // `path` is relative to the theme folder, with '/' separators. Nothing when the file is missing or unsafe.
    virtual std::optional<std::string> Read(std::string_view path) = 0;
    virtual bool Exists(std::string_view path) = 0;
};

// Reads a theme folder on disk.
class FolderFiles final : public FileSource {
public:
    explicit FolderFiles(std::filesystem::path folder);

    std::optional<std::string> Read(std::string_view path) override;
    bool Exists(std::string_view path) override;

private:
    std::filesystem::path Resolve(std::string_view path) const;

    std::filesystem::path m_folder;
};

}  // namespace amk::theme
