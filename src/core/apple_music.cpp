#include "apple_music.hpp"

#include <cstdint>
#include <vector>

#include "text_util.hpp"

namespace ampify {

namespace {

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
    if (name == ToLowerAscii(kAppleMusicImage)) return ProcessRole::AppleMusic;
    if (name == ToLowerAscii(kLibraryAgentImage)) return ProcessRole::LibraryAgent;
    return ProcessRole::Unknown;
}

// The parts of a package full name that identify Apple Music: Name_Version_Architecture_ResourceId_PublisherId.
std::string AppleMusicVersion(std::string_view packageFullName) {
    std::vector<std::string_view> parts = SplitOnUnderscore(packageFullName);
    if (parts.size() != 5) return {};
    if (parts[0] != kPackageName || parts[4] != kPublisherId) return {};
    return std::string(parts[1]);
}

}  // namespace ampify
