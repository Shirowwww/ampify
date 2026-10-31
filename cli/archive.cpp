#include "archive.hpp"

#include <system_error>

namespace ampify {

bool IsSafeArchiveEntry(std::string_view entry, std::string& reason) {
    if (entry.empty()) {
        reason = "empty entry";
        return false;
    }
    if (entry.front() == '/') {
        reason = "absolute path: " + std::string(entry);
        return false;
    }
    for (char c : entry) {
        if (c == '\\' || c == ':') {
            reason = "forbidden character in the path: " + std::string(entry);
            return false;
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            reason = "control character in the path";
            return false;
        }
    }

    // Walk the components. A trailing '/' marks a folder entry and is the only empty component allowed.
    size_t start = 0;
    while (start < entry.size()) {
        size_t slash = entry.find('/', start);
        size_t end = slash == std::string_view::npos ? entry.size() : slash;
        std::string_view part = entry.substr(start, end - start);
        if (part == "..") {
            reason = "path climbs out of the folder: " + std::string(entry);
            return false;
        }
        if (part == "." || (part.empty() && end != entry.size())) {
            reason = "invalid path component: " + std::string(entry);
            return false;
        }
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return true;
}

std::string CheckArchiveListing(const std::vector<std::string>& entries) {
    for (const std::string& entry : entries) {
        std::string reason;
        if (!IsSafeArchiveEntry(entry, reason)) return "archive refused: " + reason;
    }
    return {};
}

bool ContainsLink(const std::filesystem::path& folder) {
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_symlink(ec)) return true;
    }
    return false;
}

}  // namespace ampify
