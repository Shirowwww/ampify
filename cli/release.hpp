#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace amk {

// The part of a GitHub release that `amk update` reads: its tag, and its assets by file name.
struct ReleaseInfo {
    std::string tag;      // "v0.2.0"
    std::string version;  // "0.2.0"
    std::map<std::string, std::string> assets;  // file name -> download address
};

// Reads the JSON of `GET /repos/<owner>/<repo>/releases/latest`. On failure returns nullopt and writes the reason.
std::optional<ReleaseInfo> ParseRelease(std::string_view text, std::string& error);

// Reads a SHA256SUMS file (`<hex>  <name>` per line, `*` before the name allowed). Keys are file names.
std::map<std::string, std::string> ParseChecksums(std::string_view text);

// -1, 0 or 1. Compares numeric components, so 0.10.0 is newer than 0.9.0. A leading "v" is ignored.
int CompareReleaseVersions(std::string_view left, std::string_view right);

}  // namespace amk
