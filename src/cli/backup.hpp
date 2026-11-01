#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <filesystem>

#include "kit.hpp"

namespace ampify {

// A snapshot of what the user configured: settings.txt and the four element folders, copied into
// backups\<yyyymmdd-hhmmss>\. Binaries are never part of it, and the Apple Music package is never read.
// Returns the snapshot's name.
std::optional<std::string> CreateBackup(const KitLayout& kit, std::string& error);

// Snapshot names, oldest first. Names sort chronologically because of their format.
std::vector<std::string> ListBackups(const KitLayout& kit);

// Copies a folder tree file by file. Symbolic links are skipped. Used to install an element into the kit.
bool CopyElementFolder(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error);

// Puts a snapshot back in place. Takes a snapshot of the current state first, so the restore can be undone.
bool RestoreBackup(const KitLayout& kit, std::string_view name, std::string& error);

}  // namespace ampify
