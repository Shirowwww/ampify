#include "version_list.hpp"

#include <algorithm>
#include <fstream>

#include "text_util.hpp"

namespace ampify {

namespace {

// A versions.txt is a few lines. A bigger file is not one, and is not read.
constexpr std::streamsize kMaxFileSize = 64 * 1024;

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

}  // namespace

bool IsPackageVersionText(std::string_view text) {
    int groups = 0;
    size_t digits = 0;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            if (++digits > 9) return false;
        } else if (c == '.') {
            if (digits == 0) return false;
            groups++;
            digits = 0;
        } else {
            return false;
        }
    }
    return digits > 0 && groups == 3;
}

std::optional<std::vector<std::string>> ParseVersionList(std::string_view text) {
    text = WithoutBom(text);

    std::vector<std::string> versions;
    bool header = false;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = Trim(text.substr(start, end - start));
        start = end + 1;

        if (!header) {
            if (line.empty()) continue;
            if (line != kVersionListHeader) return std::nullopt;
            header = true;
            continue;
        }
        if (line.empty() || line.front() == '#') continue;
        if (!IsPackageVersionText(line)) return std::nullopt;
        if (std::find(versions.begin(), versions.end(), line) == versions.end()) versions.emplace_back(line);
    }
    if (!header) return std::nullopt;
    return versions;
}

std::string FormatVersionList(const std::vector<std::string>& versions, std::string_view comment) {
    std::string text(kVersionListHeader);
    text += "\n";
    if (!comment.empty()) text += "# " + std::string(comment) + "\n";
    for (const std::string& version : versions) text += version + "\n";
    return text;
}

std::vector<std::string> LoadVersionList(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::string text(static_cast<size_t>(kMaxFileSize) + 1, '\0');
    in.read(text.data(), kMaxFileSize + 1);
    if (in.gcount() > kMaxFileSize) return {};
    text.resize(static_cast<size_t>(in.gcount()));
    std::optional<std::vector<std::string>> versions = ParseVersionList(text);
    return versions ? *versions : std::vector<std::string>{};
}

}  // namespace ampify
