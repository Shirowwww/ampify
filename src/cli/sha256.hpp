#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ampify {

// SHA-256 of the bytes, as 64 lowercase hexadecimal characters.
std::string Sha256Hex(std::string_view data);

// SHA-256 of a file, read in chunks. Empty when the file cannot be read.
std::optional<std::string> Sha256File(const std::filesystem::path& file);

// True for exactly 64 lowercase hexadecimal characters, the only form a catalog accepts.
bool IsSha256Hex(std::string_view text);

}  // namespace ampify
