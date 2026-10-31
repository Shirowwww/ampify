#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../core/build_signature.hpp"
#include "test.hpp"

namespace {

using Bytes = std::vector<std::byte>;

void Put(Bytes& bytes, std::size_t offset, std::uint32_t value, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
    }
}

// A minimal PE32+ DLL: DOS header, PE headers, and one ".edata" section that holds the export directory.
// With `withExports` false, the image has no export directory at all.
Bytes MakeDll(const std::vector<std::string>& names, bool withExports = true) {
    constexpr std::size_t kSectionRva = 0x1000;
    constexpr std::size_t kSectionFile = 0x200;
    constexpr std::size_t kSectionSize = 0x400;
    constexpr std::size_t kOptional = 0x58;
    constexpr std::size_t kSection = kOptional + 240;
    constexpr std::size_t kExportDirectorySize = 40;

    Bytes bytes(kSectionFile + kSectionSize, std::byte{0});
    Put(bytes, 0, 0x5A4D, 2);           // "MZ"
    Put(bytes, 0x3C, 0x40, 4);          // offset of the PE header
    Put(bytes, 0x40, 0x00004550, 4);    // "PE\0\0"
    Put(bytes, 0x44, 0x8664, 2);        // machine: x64
    Put(bytes, 0x46, 1, 2);             // one section
    Put(bytes, 0x54, 240, 2);           // size of the optional header
    Put(bytes, 0x56, 0x2022, 2);        // characteristics: DLL
    Put(bytes, kOptional, 0x20B, 2);    // PE32+
    Put(bytes, kOptional + 108, 16, 4); // number of data directories

    const char name[] = ".edata";
    for (std::size_t i = 0; name[i] != '\0'; ++i) bytes[kSection + i] = static_cast<std::byte>(name[i]);
    Put(bytes, kSection + 8, kSectionSize, 4);   // virtual size
    Put(bytes, kSection + 12, kSectionRva, 4);   // virtual address
    Put(bytes, kSection + 16, kSectionSize, 4);  // raw size
    Put(bytes, kSection + 20, kSectionFile, 4);  // raw pointer

    if (!withExports) return bytes;

    // Export directory at the start of the section; the name pointers follow it, then the strings.
    Put(bytes, kOptional + 112, kSectionRva, 4);
    Put(bytes, kOptional + 116, kExportDirectorySize, 4);
    Put(bytes, kSectionFile + 24, static_cast<std::uint32_t>(names.size()), 4);
    Put(bytes, kSectionFile + 32, kSectionRva + kExportDirectorySize, 4);

    std::size_t text = kSectionFile + kExportDirectorySize + 4 * names.size();
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::size_t pointer = kSectionFile + kExportDirectorySize + 4 * i;
        Put(bytes, pointer, static_cast<std::uint32_t>(kSectionRva + (text - kSectionFile)), 4);
        for (char c : names[i]) bytes[text++] = static_cast<std::byte>(c);
        bytes[text++] = std::byte{0};
    }
    return bytes;
}

std::vector<std::string> RequiredNames() {
    std::vector<std::string> names;
    for (std::string_view name : ampify::kRequiredPlayerExports) names.emplace_back(name);
    return names;
}

}  // namespace

TEST_CASE(PeExports_ReadsTheNamesOfAnImage) {
    std::optional<std::vector<std::string>> names = ampify::PeExportNames(MakeDll({"AVCFPlayerSetVolume", "Other"}));
    CHECK(names.has_value());
    CHECK(names && *names == std::vector<std::string>({"AVCFPlayerSetVolume", "Other"}));
}

TEST_CASE(PeExports_RefusesBytesThatAreNotAPeImage) {
    std::string text = "this is not a DLL";
    Bytes bytes;
    for (char c : text) bytes.push_back(static_cast<std::byte>(c));
    CHECK(!ampify::PeExportNames(bytes).has_value());
    CHECK(!ampify::PeExportNames({}).has_value());
}

TEST_CASE(PeExports_RefusesATruncatedImage) {
    Bytes bytes = MakeDll(RequiredNames());
    bytes.resize(0x180);  // the headers survive, the export directory at 0x200 is cut off
    CHECK(!ampify::PeExportNames(bytes).has_value());
}

TEST_CASE(PeExports_ImageWithoutExportsIsReadableAndEmpty) {
    std::optional<std::vector<std::string>> names = ampify::PeExportNames(MakeDll({}, false));
    CHECK(names.has_value());
    CHECK(names && names->empty());
}

TEST_CASE(Signature_MatchesWhenEveryRequiredExportIsThere) {
    std::vector<std::string> names = RequiredNames();
    names.push_back("SomethingElse");  // extra exports do no harm

    ampify::BuildSignature signature = ampify::CheckBuildSignature(MakeDll(names));
    CHECK(signature.readable);
    CHECK(signature.missing.empty());
    CHECK(ampify::SignatureMatches(signature));
}

TEST_CASE(Signature_ReportsEachMissingExport) {
    std::vector<std::string> names = RequiredNames();
    names.erase(names.begin() + 4);  // AVCFPlayerSetVolume is gone
    names[0] = "avcfplayercopycurrentitem";  // names are case-sensitive

    ampify::BuildSignature signature = ampify::CheckBuildSignature(MakeDll(names));
    CHECK(signature.readable);
    CHECK(!ampify::SignatureMatches(signature));
    CHECK(signature.missing.size() == 2);
    CHECK(signature.missing.size() == 2 && signature.missing[0] == "AVCFPlayerCopyCurrentItem");
    CHECK(signature.missing.size() == 2 && signature.missing[1] == "AVCFPlayerSetVolume");
}

TEST_CASE(Signature_AnUnreadableFileIsNeverAMatch) {
    ampify::BuildSignature signature = ampify::CheckBuildSignature({});
    CHECK(!signature.readable);
    CHECK(!ampify::SignatureMatches(signature));
}

TEST_CASE(Signature_AnImageWithoutExportsMissesEverything) {
    ampify::BuildSignature signature = ampify::CheckBuildSignature(MakeDll({}, false));
    CHECK(signature.readable);
    CHECK(signature.missing.size() == std::size(ampify::kRequiredPlayerExports));
    CHECK(!ampify::SignatureMatches(signature));
}

TEST_CASE(Signature_DescribesWhatWasFound) {
    CHECK(ampify::DescribeSignature(ampify::CheckBuildSignature(MakeDll(RequiredNames()))).find("signature recognised") !=
          std::string::npos);

    std::string missing = ampify::DescribeSignature(ampify::CheckBuildSignature(MakeDll({"Other"})));
    CHECK(missing.find("exports missing") != std::string::npos);
    CHECK(missing.find("AVCFPlayerSetVolume") != std::string::npos);

    CHECK(ampify::DescribeSignature({}).find("unreadable") != std::string::npos);
}
