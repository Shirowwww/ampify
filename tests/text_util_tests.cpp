// The small routines everything shares (src/core/text_util.*): UTF-8, numbers, versions and the atomic file write.

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/core/settings.hpp"
#include "../src/core/text_util.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

fs::path Scratch(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("text-util-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// The two number formatters this one replaced, kept here as the oracle of the corpus below.
std::string OldJsonNumber(double value) {
    char buffer[32];
    if (std::floor(value) == value && std::fabs(value) < 9e15) {
        std::snprintf(buffer, sizeof buffer, "%lld", static_cast<long long>(value));
    } else {
        *std::to_chars(buffer, buffer + sizeof buffer - 1, value).ptr = '\0';
    }
    return buffer;
}

std::string OldManifestNumber(double value) {
    char buffer[32];
    auto [end, error] = std::to_chars(buffer, buffer + sizeof buffer, value);
    return error == std::errc() ? std::string(buffer, end) : std::to_string(value);
}

// The release comparison this one replaced.
std::vector<std::uint64_t> OldNumericParts(std::string_view version) {
    if (!version.empty() && (version.front() == 'v' || version.front() == 'V')) version.remove_prefix(1);
    std::vector<std::uint64_t> parts;
    std::uint64_t current = 0;
    bool any = false;
    for (char c : version) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + static_cast<std::uint64_t>(c - '0');
            any = true;
        } else {
            parts.push_back(current);
            current = 0;
            any = false;
        }
    }
    if (any || version.empty() || version.back() == '.') parts.push_back(current);
    return parts;
}

int OldCompare(std::string_view left, std::string_view right) {
    std::vector<std::uint64_t> a = OldNumericParts(left);
    std::vector<std::uint64_t> b = OldNumericParts(right);
    size_t count = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < count; i++) {
        std::uint64_t x = i < a.size() ? a[i] : 0;
        std::uint64_t y = i < b.size() ? b[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

}  // namespace

TEST_CASE(TextUtil_ShortestNumberTextKeepsTheTextOfBothOldFormatters) {
    const double corpus[] = {0,       1,      -1,       12,      12.5,   0.5,     0.72,   0.1,     1.5e-7,
                             100000,  1e6,    123456789, 8999999999999999.0,      1e15 - 1, -42,   -0.25, 3.14159, 1e-5,
                             65535,   0.001,  1e20,     1.25e300, 2.5e-300, 9e15,   -9e15,  32,     0.333333333333333};
    for (double value : corpus) {
        CHECK(ampify::ShortestNumberText(value) == OldJsonNumber(value));
        if (std::fabs(value) < 1e15 && std::floor(value) != value) CHECK(ampify::ShortestNumberText(value) == OldManifestNumber(value));
    }
    // The manifest used to write whole numbers through to_chars: the ones a setting can hold read the same.
    for (double value : {0.0, 1.0, 5.0, 12.0, 100.0, 255.0, 1000.0, 99999.0, -7.0}) {
        CHECK(ampify::ShortestNumberText(value) == OldManifestNumber(value));
    }
    CHECK(ampify::ShortestNumberText(0.72) == "0.72");
    CHECK(ampify::ShortestNumberText(-0.0) == "0");
}

TEST_CASE(TextUtil_CompareDottedVersionsAgreesWithTheReleaseComparison) {
    const char* corpus[] = {"0.10.0", "0.9.0", "v1.2.3", "1.2.3", "1.2", "1.2.0", "0.1.0", "0.2.0", "1.1.1", "1.10.0",
                            "1.9.0",  "V2.0",  "",      "1.",    ".1",  "1..2",  "1.2-rc1", "10.0.0", "9.99.99"};
    for (const char* a : corpus) {
        for (const char* b : corpus) CHECK(ampify::CompareDottedVersions(a, b) == OldCompare(a, b));
    }
    CHECK(ampify::CompareDottedVersions("1.1792.39682.0", "1.1540.23042.0") > 0);
    CHECK(ampify::CompareDottedVersions("1.2", "1.2.0.0") == 0);
}

TEST_CASE(TextUtil_AppendUtf8WritesOneToFourBytes) {
    std::string out;
    ampify::AppendUtf8(out, 'A');
    ampify::AppendUtf8(out, 0xE9);
    ampify::AppendUtf8(out, 0x20AC);
    ampify::AppendUtf8(out, 0x1F3B5);
    CHECK(out == "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x8E\xB5");
}

TEST_CASE(TextUtil_ClipUtf8NeverCutsInsideACharacter) {
    const std::string text = "ab\xE2\x82\xAC" "cd";  // the euro sign takes bytes 2, 3 and 4
    CHECK(ampify::ClipUtf8(text, 100) == text);
    CHECK(ampify::ClipUtf8(text, 7) == text);
    CHECK(ampify::ClipUtf8(text, 6) == "ab\xE2\x82\xAC" "c");
    CHECK(ampify::ClipUtf8(text, 5) == "ab\xE2\x82\xAC");
    CHECK(ampify::ClipUtf8(text, 4) == "ab");   // the boundary is inside the 3-byte character
    CHECK(ampify::ClipUtf8(text, 3) == "ab");
    CHECK(ampify::ClipUtf8(text, 2) == "ab");
    CHECK(ampify::ClipUtf8(text, 1) == "a");
    CHECK(ampify::ClipUtf8(text, 0).empty());
    CHECK(ampify::ClipUtf8("\xF0\x9F\x8E\xB5", 3).empty());
}

TEST_CASE(TextUtil_Utf8ToPathReadsTheBytesAsUtf8) {
    fs::path path = ampify::Utf8ToPath("caf\xC3\xA9");
    CHECK(path.u8string() == u8"café");
}

TEST_CASE(TextUtil_WriteFileAtomicallyReplacesTheFileByteForByte) {
    fs::path file = Scratch("write") / "data.bin";
    const std::string bytes("a\nb\r\n\0c", 7);
    CHECK(ampify::WriteFileAtomically(file, "old") == ampify::WriteResult::Written);
    CHECK(ampify::WriteFileAtomically(file, bytes) == ampify::WriteResult::Written);
    CHECK(ampify::ReadWholeFile(file).value_or("") == bytes);
    CHECK(!fs::exists(fs::path(file) += ".tmp"));

    CHECK(ampify::WriteFileAtomically(Scratch("write") / "missing" / "x.txt", "x") == ampify::WriteResult::CannotWrite);

    fs::path folderTarget = Scratch("write") / "taken";
    fs::create_directories(folderTarget / "inside");
    CHECK(ampify::WriteFileAtomically(folderTarget, "x") == ampify::WriteResult::CannotReplace);
    CHECK(fs::is_directory(folderTarget));
    CHECK(!fs::exists(fs::path(folderTarget) += ".tmp"));
}

TEST_CASE(TextUtil_SettingsSaveKeepsItsLineEndingsAndOrder) {
    fs::path file = Scratch("settings") / "settings.txt";
    ampify::Settings settings;
    settings.Set("b", "2");
    settings.Set("a", "1");
    CHECK(settings.Save(file.string()));
#ifdef _WIN32
    const char* end = "\r\n";
#else
    const char* end = "\n";
#endif
    std::string expected = std::string("# Ampify settings. Edit by hand if you like.") + end + "a = 1" + end + "b = 2" + end;
    CHECK(ampify::ReadWholeFile(file).value_or("") == expected);
}
