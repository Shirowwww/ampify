#include "build_signature.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace amk {
namespace {

constexpr std::size_t kLfanewOffset = 0x3C;     // DOS header: offset of the PE header
constexpr std::size_t kPeSignatureSize = 4;     // "PE\0\0"
constexpr std::size_t kCoffHeaderSize = 20;
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::uint16_t kMagicPe32 = 0x10B;
constexpr std::uint16_t kMagicPe32Plus = 0x20B;
constexpr std::size_t kDataDirectoriesPe32 = 96;      // offset of the data directories in the optional header
constexpr std::size_t kDataDirectoriesPe32Plus = 112;
constexpr std::size_t kExportNumberOfNames = 24;      // offsets inside IMAGE_EXPORT_DIRECTORY
constexpr std::size_t kExportAddressOfNames = 32;
constexpr std::size_t kMaxNameLength = 512;

std::optional<std::uint32_t> ReadUnsigned(std::span<const std::byte> image, std::size_t offset, std::size_t bytes) {
    if (offset > image.size() || image.size() - offset < bytes) return std::nullopt;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(image[offset + i])) << (8 * i);
    }
    return value;
}

std::optional<std::uint16_t> ReadU16(std::span<const std::byte> image, std::size_t offset) {
    std::optional<std::uint32_t> value = ReadUnsigned(image, offset, 2);
    if (!value) return std::nullopt;
    return static_cast<std::uint16_t>(*value);
}

std::optional<std::uint32_t> ReadU32(std::span<const std::byte> image, std::size_t offset) {
    return ReadUnsigned(image, offset, 4);
}

// File offset of a relative virtual address, through the section table. Empty when no section holds it.
std::optional<std::size_t> FileOffsetOf(std::span<const std::byte> image, std::size_t sectionTable,
                                        std::size_t sectionCount, std::uint32_t rva) {
    for (std::size_t i = 0; i < sectionCount; ++i) {
        std::size_t header = sectionTable + i * kSectionHeaderSize;
        std::optional<std::uint32_t> virtualSize = ReadU32(image, header + 8);
        std::optional<std::uint32_t> virtualAddress = ReadU32(image, header + 12);
        std::optional<std::uint32_t> rawSize = ReadU32(image, header + 16);
        std::optional<std::uint32_t> rawPointer = ReadU32(image, header + 20);
        if (!virtualSize || !virtualAddress || !rawSize || !rawPointer) return std::nullopt;

        // A section covers the larger of its virtual and raw sizes. The loader zero-fills the rest.
        std::uint64_t span = std::max(*virtualSize, *rawSize);
        if (rva < *virtualAddress || rva - *virtualAddress >= span) continue;

        std::uint64_t offset = static_cast<std::uint64_t>(*rawPointer) + (rva - *virtualAddress);
        if (offset >= image.size()) return std::nullopt;
        return static_cast<std::size_t>(offset);
    }
    return std::nullopt;
}

std::optional<std::string> ReadName(std::span<const std::byte> image, std::size_t offset) {
    std::string name;
    for (std::size_t i = 0; i < kMaxNameLength; ++i) {
        if (offset + i >= image.size()) return std::nullopt;
        char c = static_cast<char>(std::to_integer<unsigned char>(image[offset + i]));
        if (c == '\0') return name;
        name += c;
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::vector<std::string>> PeExportNames(std::span<const std::byte> image) {
    // DOS header, then the PE signature, the COFF header and the optional header.
    std::optional<std::uint16_t> dos = ReadU16(image, 0);
    if (!dos || *dos != 0x5A4D) return std::nullopt;  // "MZ"
    std::optional<std::uint32_t> peOffset = ReadU32(image, kLfanewOffset);
    if (!peOffset) return std::nullopt;

    std::size_t pe = *peOffset;
    std::optional<std::uint32_t> peSignature = ReadU32(image, pe);
    if (!peSignature || *peSignature != 0x00004550) return std::nullopt;  // "PE\0\0"

    std::size_t coff = pe + kPeSignatureSize;
    std::optional<std::uint16_t> sectionCount = ReadU16(image, coff + 2);
    std::optional<std::uint16_t> optionalSize = ReadU16(image, coff + 16);
    if (!sectionCount || !optionalSize) return std::nullopt;

    std::size_t optional = coff + kCoffHeaderSize;
    std::optional<std::uint16_t> magic = ReadU16(image, optional);
    if (!magic) return std::nullopt;
    std::size_t directories = 0;
    if (*magic == kMagicPe32) {
        directories = kDataDirectoriesPe32;
    } else if (*magic == kMagicPe32Plus) {
        directories = kDataDirectoriesPe32Plus;
    } else {
        return std::nullopt;
    }

    // Data directory 0 is the export directory. No export directory means no exports, which is a valid image.
    std::optional<std::uint32_t> exportRva = ReadU32(image, optional + directories);
    if (!exportRva) return std::nullopt;
    std::vector<std::string> names;
    if (*exportRva == 0) return names;

    std::size_t sectionTable = optional + *optionalSize;
    std::optional<std::size_t> exportOffset = FileOffsetOf(image, sectionTable, *sectionCount, *exportRva);
    if (!exportOffset) return std::nullopt;

    std::optional<std::uint32_t> count = ReadU32(image, *exportOffset + kExportNumberOfNames);
    std::optional<std::uint32_t> namesRva = ReadU32(image, *exportOffset + kExportAddressOfNames);
    if (!count || !namesRva) return std::nullopt;
    std::optional<std::size_t> namesOffset = FileOffsetOf(image, sectionTable, *sectionCount, *namesRva);
    if (!namesOffset) return std::nullopt;

    // Each entry is the RVA of one name. A pointer outside the image makes the whole table unreadable.
    for (std::uint32_t i = 0; i < *count; ++i) {
        std::optional<std::uint32_t> nameRva = ReadU32(image, *namesOffset + 4 * static_cast<std::size_t>(i));
        if (!nameRva) return std::nullopt;
        std::optional<std::size_t> nameOffset = FileOffsetOf(image, sectionTable, *sectionCount, *nameRva);
        if (!nameOffset) return std::nullopt;
        std::optional<std::string> name = ReadName(image, *nameOffset);
        if (!name) return std::nullopt;
        names.push_back(*name);
    }
    return names;
}

BuildSignature CheckBuildSignature(std::span<const std::byte> image, std::span<const std::string_view> required) {
    BuildSignature signature;
    std::optional<std::vector<std::string>> exports = PeExportNames(image);
    if (!exports) return signature;

    signature.readable = true;
    for (std::string_view name : required) {
        if (std::find(exports->begin(), exports->end(), name) == exports->end()) {
            signature.missing.emplace_back(name);
        }
    }
    return signature;
}

std::string DescribeSignature(const BuildSignature& signature) {
    std::string dll(kPlayerDllName);
    if (!signature.readable) return "signature non reconnue : " + dll + " illisible";
    if (!signature.missing.empty()) {
        std::string list;
        for (const std::string& name : signature.missing) {
            if (!list.empty()) list += ", ";
            list += name;
        }
        return "signature non reconnue : exports absents de " + dll + " (" + list + ")";
    }
    return "signature reconnue : " + std::to_string(std::size(kRequiredPlayerExports)) + " exports trouvés dans " +
           dll;
}

}  // namespace amk
