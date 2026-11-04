#pragma once

// The track list of Apple Music's library file, Library.musicdb, with the listening statistics Apple keeps there. The
// format is the one of Music.app on macOS, which Apple does not document; the layout read here, checked on a real
// Windows library (Apple Music 1.7.0.155), is described in knowledge/apple-music-library-file.md. The file is untrusted
// input: every size is bounded, and a damaged or unknown file gives an error, never a partial read.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

struct LibraryTrack {
    uint64_t persistentId = 0;
    std::string title;
    std::string artist;
    std::string album;
    uint32_t playCount = 0;
    uint32_t skipCount = 0;
    int64_t lastPlayedMs = 0;  // Unix time in milliseconds; 0 when never played
    int64_t dateAddedMs = 0;   // 0 when the file does not say
    uint32_t durationMs = 0;   // 0 when the file does not say
};

struct MusicDbResult {
    std::vector<LibraryTrack> tracks;
    std::string error;  // empty when the file was read
};

// Bounds. A library of 2 000 tracks is a 1.5 MB file that decodes to 6 MB.
inline constexpr size_t kMaxMusicDbFileBytes = 64u << 20;
inline constexpr size_t kMaxMusicDbPlainBytes = 256u << 20;
inline constexpr size_t kMaxLibraryTracks = 200000;
inline constexpr size_t kMaxLibraryTextBytes = 4096;  // a longer title, artist or album is cut

// Reads the whole file at `file`.
MusicDbResult ReadMusicDb(const std::filesystem::path& file);

// The tracks of the bytes of a Library.musicdb file.
MusicDbResult ParseMusicDb(std::string_view file);

// Apple's dates count seconds since 1904-01-01 in local wall-clock time. 0 stays 0.
int64_t MacLocalTimeToUnixMs(uint32_t macSeconds);

// AES-128-ECB with the library's key over `length` bytes (a multiple of 16), in place. Encrypting serves the tests
// only. False with `error` set when the system cannot do it (only Windows can, through BCrypt).
bool ApplyMusicDbCipher(char* data, size_t length, bool decrypt, std::string* error);

}  // namespace ampify
