#include "release.hpp"

#include "../core/json.hpp"
#include "sha256.hpp"

namespace ampify {

std::optional<ReleaseInfo> ParseRelease(std::string_view text, std::string& error) {
    std::string syntaxError;
    std::optional<Json> document = Json::Parse(text, &syntaxError, Json::DuplicateKeys::Refuse);
    if (!document || !document->IsObject()) {
        error = "unreadable release answer" + (syntaxError.empty() ? std::string() : ": " + syntaxError);
        return std::nullopt;
    }

    const Json* tag = document->Find("tag_name");
    if (tag == nullptr || !tag->IsString() || tag->AsString()->empty()) {
        error = "release without tag_name";
        return std::nullopt;
    }

    ReleaseInfo info;
    info.tag = *tag->AsString();
    info.version = info.tag;
    if (!info.version.empty() && (info.version.front() == 'v' || info.version.front() == 'V')) {
        info.version.erase(0, 1);
    }

    const Json* assets = document->Find("assets");
    if (assets != nullptr && assets->IsArray()) {
        for (const Json& asset : *assets->AsArray()) {
            const Json* name = asset.Find("name");
            const Json* url = asset.Find("browser_download_url");
            if (name && name->IsString() && url && url->IsString()) {
                info.assets[*name->AsString()] = *url->AsString();
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

}  // namespace ampify
