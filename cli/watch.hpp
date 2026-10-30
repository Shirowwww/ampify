#pragma once

#include <string>

#include "kit.hpp"

namespace amk {

// A digest of the settings file and of every file under the element folders, from their names, sizes and
// modification times. It changes when the user edits a theme, an extension or the settings, and not otherwise.
std::string KitFingerprint(const KitLayout& kit);

// The same digest for one folder tree, with paths relative to that folder. `amk dev` uses it on the author's folder.
std::string FolderFingerprint(const std::filesystem::path& folder);

}  // namespace amk
