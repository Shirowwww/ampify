#include "album_tracks.hpp"

#include <algorithm>
#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <set>

namespace ampify {

namespace {

// The hexadecimal number after `key` ("PID:0x"), up to the next '-' or the end.
std::optional<uint64_t> HexField(std::string_view text, std::string_view key) {
    size_t at = text.find(key);
    if (at == std::string_view::npos) return std::nullopt;
    if (at > 0 && text[at - 1] != '-') return std::nullopt;  // "PID" inside "PPID" or "PIPID"
    std::string_view digits = text.substr(at + key.size());
    digits = digits.substr(0, digits.find('-'));
    if (digits.empty() || digits.size() > 16) return std::nullopt;
    uint64_t value = 0;
    auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    if (error != std::errc() || end != digits.data() + digits.size()) return std::nullopt;
    return value;
}

std::string Hex(uint64_t value) {
    char text[19];
    std::snprintf(text, sizeof text, "0x%" PRIx64, value);
    return text;
}

}  // namespace

std::optional<LibraryItemId> ParseLibraryItemId(std::string_view text) {
    constexpr std::string_view kKind = "-IKIND:";
    if (text.rfind("DBID:0x", 0) != 0) return std::nullopt;
    std::optional<uint64_t> database = HexField(text, "DBID:0x");
    std::optional<uint64_t> persistent = HexField(text, "PID:0x");
    size_t kind = text.find(kKind);
    if (!database || !persistent || *persistent == 0 || kind == std::string_view::npos) return std::nullopt;
    LibraryItemId id{*database, *persistent, std::string(text.substr(kind + kKind.size()))};
    if (id.kind.empty() || id.kind.find('-') != std::string::npos) return std::nullopt;
    return id;
}

std::string SongLibraryItemId(uint64_t database, uint64_t song) {
    return "DBID:" + Hex(database) + "-PID:" + Hex(song) + "-PPID:0x0-PIPID:0x0-IKIND:eSong";
}

std::string SongDescriptorJson(uint64_t database, uint64_t song) {
    return R"([{"kind":"song","identifiers":{"libraryItemID":")" + SongLibraryItemId(database, song) + R"("}}])";
}

std::vector<LibraryTrack> TracksOfAlbum(std::span<const LibraryTrack> tracks, uint64_t albumId) {
    std::vector<LibraryTrack> album;
    if (albumId == 0) return album;
    for (const LibraryTrack& track : tracks) {
        if (track.albumId == albumId) album.push_back(track);
    }
    std::stable_sort(album.begin(), album.end(), [](const LibraryTrack& a, const LibraryTrack& b) {
        if (DiscOf(a) != DiscOf(b)) return DiscOf(a) < DiscOf(b);
        if ((a.trackNumber == 0) != (b.trackNumber == 0)) return b.trackNumber == 0;
        if (a.trackNumber != b.trackNumber) return a.trackNumber < b.trackNumber;
        return a.title < b.title;
    });
    return album;
}

size_t DiscCount(std::span<const LibraryTrack> tracks) {
    std::set<uint32_t> discs;
    for (const LibraryTrack& track : tracks) discs.insert(DiscOf(track));
    return discs.size();
}

}  // namespace ampify
