#include "zip_writer.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <system_error>

#include "archive.hpp"

namespace ampify::cli {

namespace {

// 1980-01-01 00:00:00, the earliest time a zip can store.
constexpr std::uint16_t kDosTime = 0;
constexpr std::uint16_t kDosDate = (0 << 9) | (1 << 5) | 1;
// General purpose flag bit 11: names are UTF-8.
constexpr std::uint16_t kUtf8Names = 0x0800;
constexpr std::uint16_t kVersionNeeded = 10;      // 1.0: stored files
constexpr std::uint16_t kVersionMadeBy = 0x031E;  // Unix host, version 3.0
constexpr std::uint32_t kRegularFileMode = 0100644u << 16;  // unzip reads the mode from the high 16 bits
constexpr std::size_t kMaxEntries = 0xFFFF;
constexpr std::uint64_t kMaxBytes = 0xFFFFFFFFu;

void Put16(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void Put32(std::string& out, std::uint32_t value) {
    Put16(out, value & 0xFFFF);
    Put16(out, value >> 16);
}

struct Written {
    std::string name;
    std::uint32_t crc;
    std::uint32_t size;
    std::uint32_t offset;
};

}  // namespace

std::uint32_t Crc32(std::string_view bytes) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; i++) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; bit++) value = (value & 1) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
            values[i] = value;
        }
        return values;
    }();

    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : bytes) crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool WriteZip(const std::filesystem::path& archive, std::vector<ZipSource> files, std::string& error) {
    if (files.size() > kMaxEntries) {
        error = "too many files for a zip: " + std::to_string(files.size());
        return false;
    }
    std::sort(files.begin(), files.end(), [](const ZipSource& a, const ZipSource& b) { return a.name < b.name; });

    std::string body;
    std::vector<Written> written;
    for (const ZipSource& source : files) {
        std::string reason;
        if (!IsSafeArchiveEntry(source.name, reason)) {
            error = "name refused in the archive (" + reason + "): " + source.name;
            return false;
        }

        std::string data = source.text;
        if (!source.file.empty()) {
            std::ifstream in(source.file, std::ios::binary);
            if (!in) {
                error = "cannot read: " + source.name;
                return false;
            }
            data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (data.size() > kMaxBytes || body.size() + data.size() > kMaxBytes) {
            error = "archive too large for a zip without zip64 (4 GiB)";
            return false;
        }

        Written entry{source.name, Crc32(data), static_cast<std::uint32_t>(data.size()),
                      static_cast<std::uint32_t>(body.size())};
        Put32(body, 0x04034B50);  // local file header
        Put16(body, kVersionNeeded);
        Put16(body, kUtf8Names);
        Put16(body, 0);  // method 0: stored
        Put16(body, kDosTime);
        Put16(body, kDosDate);
        Put32(body, entry.crc);
        Put32(body, entry.size);
        Put32(body, entry.size);
        Put16(body, static_cast<std::uint32_t>(entry.name.size()));
        Put16(body, 0);  // no extra field
        body += entry.name;
        body += data;
        written.push_back(std::move(entry));
    }

    if (body.size() > kMaxBytes) {
        error = "archive too large for a zip without zip64 (4 GiB)";
        return false;
    }
    const auto directoryStart = static_cast<std::uint32_t>(body.size());
    for (const Written& entry : written) {
        Put32(body, 0x02014B50);  // central directory header
        Put16(body, kVersionMadeBy);
        Put16(body, kVersionNeeded);
        Put16(body, kUtf8Names);
        Put16(body, 0);
        Put16(body, kDosTime);
        Put16(body, kDosDate);
        Put32(body, entry.crc);
        Put32(body, entry.size);
        Put32(body, entry.size);
        Put16(body, static_cast<std::uint32_t>(entry.name.size()));
        Put16(body, 0);  // extra field
        Put16(body, 0);  // comment
        Put16(body, 0);  // disk number
        Put16(body, 0);  // internal attributes
        Put32(body, kRegularFileMode);
        Put32(body, entry.offset);
        body += entry.name;
    }
    const auto directorySize = static_cast<std::uint32_t>(body.size()) - directoryStart;

    Put32(body, 0x06054B50);  // end of central directory
    Put16(body, 0);
    Put16(body, 0);
    Put16(body, static_cast<std::uint32_t>(written.size()));
    Put16(body, static_cast<std::uint32_t>(written.size()));
    Put32(body, directorySize);
    Put32(body, directoryStart);
    Put16(body, 0);  // comment

    std::filesystem::path partial = archive;
    partial += ".part";
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.close();
    std::error_code ec;
    if (!out) {
        std::filesystem::remove(partial, ec);
        error = "cannot write the zip";
        return false;
    }
    std::filesystem::rename(partial, archive, ec);
    if (ec) {
        std::filesystem::remove(partial, ec);
        error = "cannot create the zip at the requested place";
        return false;
    }
    return true;
}

}  // namespace ampify::cli
