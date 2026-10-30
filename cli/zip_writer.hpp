#pragma once

// Writes .zip archives without compression: the same files always give the same bytes, and no library is needed.
// Tar.exe, unzip and Python's zipfile all read the result.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace amk::cli {

// CRC-32 (IEEE 802.3), the checksum that zip keeps for each file.
std::uint32_t Crc32(std::string_view bytes);

struct ZipSource {
    std::string name;            // path inside the archive, '/' separated, relative
    std::filesystem::path file;  // the bytes come from this file...
    std::string text;            // ...or from this text, when `file` is empty
};

// Writes `archive` from `files`, in name order, with timestamps fixed to 1980. The archive is first written beside
// its target and renamed at the end, so a failure leaves no half-written file. Refuses names that `amk install`
// would refuse, and archives over 4 GiB or 65535 entries (zip64 is not implemented).
bool WriteZip(const std::filesystem::path& archive, std::vector<ZipSource> files, std::string& error);

}  // namespace amk::cli
