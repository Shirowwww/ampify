#include "release.hpp"

#include <cstdint>
#include <vector>

#include "json.hpp"
#include "sha256.hpp"

namespace amk {

namespace {

std::vector<std::uint64_t> NumericParts(std::string_view version) {
    if (!version.empty() && (version.front() == 'v' || version.front() == 'V')) version.remove_prefix(1);
    std::vector<std::uint64_t> parts;
    std::uint64_t current = 0;
    bool any = false;
    for (char c : version) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + static_cast<std::uint64_t>(c - '0');
            any = true;
        } else {
            parts.push_back(current);
            current = 0;
            any = false;
        }
    }
    if (any || version.empty() || version.back() == '.') parts.push_back(current);
    return parts;
}

}  // namespace

std::optional<ReleaseInfo> ParseRelease(std::string_view text, std::string& error) {
    std::string syntaxError;
    std::optional<json::Value> document = json::Parse(text, syntaxError);
    if (!document || !document->IsObject()) {
        error = "réponse de la release illisible" + (syntaxError.empty() ? std::string() : " : " + syntaxError);
        return std::nullopt;
    }

    const json::Value* tag = document->Find("tag_name");
    if (tag == nullptr || !tag->IsString() || tag->AsString().empty()) {
        error = "release sans tag_name";
        return std::nullopt;
    }

    ReleaseInfo info;
    info.tag = tag->AsString();
    info.version = info.tag;
    if (!info.version.empty() && (info.version.front() == 'v' || info.version.front() == 'V')) {
        info.version.erase(0, 1);
    }

    const json::Value* assets = document->Find("assets");
    if (assets != nullptr && assets->IsArray()) {
        for (const json::Value& asset : assets->Items()) {
            const json::Value* name = asset.Find("name");
            const json::Value* url = asset.Find("browser_download_url");
            if (name && name->IsString() && url && url->IsString()) {
                info.assets[name->AsString()] = url->AsString();
            }
        }
    }
    return info;
}

std::map<std::string, std::string> ParseChecksums(std::string_view text) {
    std::map<std::string, std::string> sums;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        size_t space = line.find(' ');
        if (space == std::string_view::npos) continue;

        std::string_view hash = line.substr(0, space);
        std::string_view name = line.substr(space + 1);
        // sha256sum writes two spaces (text mode) or a space and '*' (binary mode) before the name.
        while (!name.empty() && (name.front() == ' ' || name.front() == '*')) name.remove_prefix(1);
        if (!IsSha256Hex(hash) || name.empty()) continue;
        sums[std::string(name)] = std::string(hash);
    }
    return sums;
}

int CompareVersions(std::string_view left, std::string_view right) {
    std::vector<std::uint64_t> a = NumericParts(left);
    std::vector<std::uint64_t> b = NumericParts(right);
    size_t count = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < count; i++) {
        std::uint64_t x = i < a.size() ? a[i] : 0;
        std::uint64_t y = i < b.size() ? b[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

}  // namespace amk
