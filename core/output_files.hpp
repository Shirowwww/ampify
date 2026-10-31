#pragma once

// The only files an element may write: <kit>\output\<id>\<name>, through Ampify.fs.writeText and the permission
// fs.output. They are meant to be read by other programs, such as a text source in OBS. An element cannot choose a
// folder, cannot read back, cannot delete, and cannot write anything but text with a known extension.

#include <filesystem>
#include <string>
#include <string_view>

namespace ampify {

inline constexpr size_t kMaxOutputFileBytes = 256 * 1024;
inline constexpr size_t kMaxOutputFilesPerElement = 16;

// A plain file name: a letter or a digit, then letters, digits, '.', '_' or '-', 64 characters at most, ending in
// .txt, .json, .csv, .html or .xml. No folder, no "..", no name reserved by Windows (con.txt, nul.json...).
bool IsValidOutputName(std::string_view name);

// The folder of an element under the output root.
std::filesystem::path OutputFolder(const std::filesystem::path& outputRoot, std::string_view elementId);

// Writes the text through a temporary file, then replaces the target, so that a reader such as OBS never sees half
// a file. Refuses an invalid name, a text above kMaxOutputFileBytes, and a new file once the element already has
// kMaxOutputFilesPerElement. False with `error` in English on failure.
bool WriteOutputFile(const std::filesystem::path& outputRoot, std::string_view elementId, std::string_view name,
                     std::string_view text, std::string* error);

}  // namespace ampify
