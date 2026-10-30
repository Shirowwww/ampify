#include "library.hpp"

#include <algorithm>
#include <system_error>

namespace amk::theme {

namespace {

bool HasThemeFile(const std::filesystem::path& folder) {
    std::error_code error;
    for (const char* name : {"colors.ini", "resources.xaml", "user.css"}) {
        if (std::filesystem::is_regular_file(folder / name, error)) return true;
    }
    return false;
}

}  // namespace

std::vector<std::string> ListThemeFolders(const std::filesystem::path& root) {
    std::vector<std::string> folders;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) return folders;

    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (!entry.is_directory(error) || !HasThemeFile(entry.path())) continue;
        folders.push_back(entry.path().filename().string());
    }
    std::sort(folders.begin(), folders.end());
    return folders;
}

std::string FolderSignature(const std::filesystem::path& folder) {
    std::string signature;
    std::error_code error;
    auto options = std::filesystem::directory_options::skip_permission_denied;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, options, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error)) continue;

        auto written = it->last_write_time(error).time_since_epoch().count();
        auto size = it->file_size(error);
        signature += it->path().lexically_relative(folder).generic_string();
        signature += '|' + std::to_string(size) + '|' + std::to_string(written) + '\n';
    }
    return signature;
}

ChangeWatcher::ChangeWatcher(std::function<std::string()> signature) : m_signature(std::move(signature)) {}

bool ChangeWatcher::Poll() {
    std::string current = m_signature();
    bool changed = m_last && *m_last != current;
    m_last = std::move(current);
    return changed;
}

}  // namespace amk::theme
