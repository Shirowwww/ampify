#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

// True when an archive entry stays inside the folder it is extracted to: relative, no "..", no drive letter,
// no backslash, no control character. `reason` says what is wrong when this returns false.
bool IsSafeArchiveEntry(std::string_view entry, std::string& reason);

// The first problem in a listing, or empty when every entry is safe.
std::string CheckArchiveListing(const std::vector<std::string>& entries);

// True when a symbolic link or a junction lies anywhere under `folder`. Archives may carry links to
// files outside the folder, so an extracted tree with one is refused.
bool ContainsLink(const std::filesystem::path& folder);

}  // namespace ampify
