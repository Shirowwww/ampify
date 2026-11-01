#include "apple_music.hpp"

#include <cstdint>
#include <vector>

#include "text_util.hpp"

namespace ampify {

namespace {

// The parts of a package full name that identify Apple Music. Name_Version_Architecture_ResourceId_PublisherId.
constexpr std::string_view kPackageName = "AppleInc.AppleMusicWin";
constexpr std::string_view kPublisherId = "nzyj5cx40ttqa";

// Splits on '_' and keeps empty parts: "a__b" gives three parts.
std::vector<std::string_view> SplitOnUnderscore(std::string_view text) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        size_t cut = text.find('_', start);
        if (cut == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, cut - start));
        start = cut + 1;
    }
}

}  // namespace

ProcessRole RoleOfImageName(std::string_view imagePath) {
    std::string name = ToLowerAscii(FileName(imagePath));
    if (name == "applemusic.exe") return ProcessRole::AppleMusic;
    if (name == "amplibraryagent.exe") return ProcessRole::LibraryAgent;
    return ProcessRole::Unknown;
}

std::string AppleMusicVersion(std::string_view packageFullName) {
    std::vector<std::string_view> parts = SplitOnUnderscore(packageFullName);
    if (parts.size() != 5) return {};
    if (parts[0] != kPackageName || parts[4] != kPublisherId) return {};
    return std::string(parts[1]);
}

namespace {

// Reads the number at the start of `text` and moves past the next dot. A missing number counts as 0.
std::uint64_t TakeNumber(std::string_view& text) {
    std::uint64_t value = 0;
    size_t digits = 0;
    while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
        value = value * 10 + static_cast<std::uint64_t>(text[digits] - '0');
        ++digits;
    }
    size_t dot = text.find('.', digits);
    text = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
    return value;
}

}  // namespace

int CompareVersions(std::string_view left, std::string_view right) {
    while (!left.empty() || !right.empty()) {
        std::uint64_t a = TakeNumber(left);
        std::uint64_t b = TakeNumber(right);
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}

}  // namespace ampify
