#pragma once

// The list of extra tested Apple Music builds that `ampify versions update` writes to <kit>\versions.txt. It comes from
// versions.json, published with the catalogue (ampify-catalog), so a build tested after this kit was compiled can be allowed
// without a new kit release. It only adds names to the version gate: the export signature of AVFoundationCF.dll is
// still checked on every launch (src/core/build_signature.hpp), and an unknown build still leaves the kernel idle.
//
// File format, plain text:
//   ampify-versions 1             first line, required
//   # comment                  ignored, as are blank lines
//   1.1600.12345.0             one package version per line: four numbers separated by dots
//
// A file with any other line is malformed and ignored as a whole: a half-read list is never used.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

inline constexpr std::string_view kVersionListHeader = "ampify-versions 1";
inline constexpr std::string_view kVersionListFile = "versions.txt";

// True for a package version such as "1.1540.23042.0": four groups of 1 to 9 digits.
bool IsPackageVersionText(std::string_view text);

// The versions in `text`, in file order, without duplicates. Nullopt when the text is malformed.
std::optional<std::vector<std::string>> ParseVersionList(std::string_view text);

// The text of versions.txt for these versions, with the header and a comment line.
std::string FormatVersionList(const std::vector<std::string>& versions, std::string_view comment = {});

// Reads <file>. A missing, unreadable or malformed file gives an empty list.
std::vector<std::string> LoadVersionList(const std::filesystem::path& file);

}  // namespace ampify
