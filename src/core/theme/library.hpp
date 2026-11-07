#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "files.hpp"

namespace ampify::theme {

// True when `folder` holds at least one of the files `names`, such as kThemeFiles.
bool HoldsAnyOf(const std::filesystem::path& folder, std::span<const char* const> names);

// The sub-folders of `root` that hold one of the files `names`, by folder name, sorted.
std::vector<std::string> ListFoldersHolding(const std::filesystem::path& root, std::span<const char* const> names);

// The theme folders under `root`: those holding one of kThemeFiles.
std::vector<std::string> ListThemeFolders(const std::filesystem::path& root);

// A text that changes when a file under `folder` is added, removed, resized or saved. Two equal texts mean nothing changed.
std::string FolderSignature(const std::filesystem::path& folder);

// Compares a signature with the one from the previous Poll. The first Poll only records it and returns false.
class ChangeWatcher {
public:
    explicit ChangeWatcher(std::function<std::string()> signature);

    bool Poll();

private:
    std::function<std::string()> m_signature;
    std::optional<std::string> m_last;
};

}  // namespace ampify::theme
