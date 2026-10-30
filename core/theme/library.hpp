#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace amk::theme {

// The theme folders under `root`, one sub-folder per theme, sorted by name. A folder with none of the theme
// files (colors.ini, resources.xaml, user.css) is left out.
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

}  // namespace amk::theme
