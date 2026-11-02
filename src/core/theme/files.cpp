#include "files.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#include "../text_util.hpp"

namespace ampify::theme {

bool IsSafeRelativePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\') return false;
    if (path.find(':') != std::string_view::npos || path.find('\\') != std::string_view::npos) return false;

    // Reject any ".." segment, however it is spelled.
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        if (path.substr(start, end - start) == "..") return false;
        start = end + 1;
    }
    return true;
}

FolderFiles::FolderFiles(std::filesystem::path folder) : m_folder(std::move(folder)) {}

std::optional<std::string> FolderFiles::Read(std::string_view path) {
    if (!IsSafeRelativePath(path)) return std::nullopt;
    return ReadWholeFile(Resolve(path));
}

bool FolderFiles::Exists(std::string_view path) {
    if (!IsSafeRelativePath(path)) return false;

    std::error_code error;
    return std::filesystem::is_regular_file(Resolve(path), error);
}

std::filesystem::path FolderFiles::Resolve(std::string_view path) const {
    return m_folder / std::filesystem::path(std::string(path));
}

}  // namespace ampify::theme
