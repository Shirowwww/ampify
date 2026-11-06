#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ampify {

// The part of a GitHub release that `ampify update` reads: its tag, and its assets by file name.
struct ReleaseInfo {
    std::string tag;      // "v0.2.0"
    std::string version;  // "0.2.0"
    std::map<std::string, std::string> assets;  // file name -> download address
};

// Reads the JSON of `GET /repos/<owner>/<repo>/releases/latest`. On failure returns nullopt and writes the reason.
std::optional<ReleaseInfo> ParseRelease(std::string_view text, std::string& error);

// Reads a SHA256SUMS file (`<hex>  <name>` per line, `*` before the name allowed). Keys are file names.
std::map<std::string, std::string> ParseChecksums(std::string_view text);

}  // namespace ampify
