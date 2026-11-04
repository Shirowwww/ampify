// Library.musicdb: an "hfma" header, then a payload whose first bytes are AES-128-ECB encrypted and which, once
// decrypted, is one zlib stream. Decoded, it is a run of "hsma" sections; the one holding "ltma" lists the tracks, one
// "itma" record each, whose "boma" sub-records hold the texts and the statistics. Offsets: knowledge/apple-music-library-file.md.

#include "musicdb.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <ctime>
#include <optional>
#include <system_error>

#include "../text_util.hpp"
#include "inflate.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace ampify {

namespace {

constexpr char kCipherKey[] = "BHUILuilfghuila3";
constexpr size_t kCipherBlock = 16;
constexpr int64_t kMacEpochToUnixSeconds = 2082844800;  // 1904-01-01 to 1970-01-01

// Envelope (hfma) header.
constexpr size_t kEnvelopeMinBytes = 0x58;
constexpr size_t kEnvelopeHeaderBytes = 4;
constexpr size_t kEnvelopeFileBytes = 8;
constexpr size_t kEnvelopeCryptBytes = 0x54;

// Section (hsma): header length at 4, total length at 16.
constexpr size_t kSectionMinBytes = 20;
// Track list (ltma): header length at 4, track count at 8. Track (itma): header length at 4, total length at 8,
// sub-record count at 12, persistent id at 16.
constexpr size_t kTrackMinBytes = 24;
// Sub-record (boma): total length at 8, subtype at 12.
constexpr size_t kBomaMinBytes = 16;
constexpr uint32_t kBomaNumbers = 0x1;
constexpr uint32_t kBomaTitle = 0x2;
constexpr uint32_t kBomaAlbum = 0x3;
constexpr uint32_t kBomaArtist = 0x4;
constexpr uint32_t kBomaPlays = 0x17;
// Text sub-record: encoding at 20 (1 is UTF-16LE), byte length at 24, the text from 36.
constexpr size_t kTextStart = 36;
constexpr uint32_t kTextUtf16 = 1;
// Numbers sub-record: date added at 112, duration in ms at 176. Plays sub-record: last played at 28, play count at 32,
// skip count at 36.
constexpr size_t kNumbersDateAdded = 112;
constexpr size_t kNumbersDuration = 176;
constexpr size_t kPlaysLastPlayed = 28;
constexpr size_t kPlaysCount = 32;
constexpr size_t kPlaysSkips = 36;

MusicDbResult Failure(std::string error) {
    MusicDbResult result;
    result.error = std::move(error);
    return result;
}

// Little-endian reads. The caller has checked that the bytes are there.
uint32_t U32(std::string_view data, size_t at) {
    uint32_t value = 0;
    for (int i = 3; i >= 0; i--) value = (value << 8) | static_cast<uint8_t>(data[at + i]);
    return value;
}

uint64_t U64(std::string_view data, size_t at) {
    return (static_cast<uint64_t>(U32(data, at + 4)) << 32) | U32(data, at);
}

void AppendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// UTF-16LE to UTF-8; a lone surrogate becomes U+FFFD.
std::string Utf16LeToUtf8(std::string_view bytes) {
    constexpr uint32_t kReplacement = 0xFFFD;
    std::string out;
    out.reserve(bytes.size() / 2);
    auto unit = [&](size_t i) {
        return static_cast<uint32_t>(static_cast<uint8_t>(bytes[2 * i]) | (static_cast<uint8_t>(bytes[2 * i + 1]) << 8));
    };
    size_t units = bytes.size() / 2;
    for (size_t i = 0; i < units; i++) {
        uint32_t code = unit(i);
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < units && unit(i + 1) >= 0xDC00 && unit(i + 1) <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) + (unit(i + 1) - 0xDC00);
            i++;
        } else if (code >= 0xD800 && code <= 0xDFFF) {
            code = kReplacement;
        }
        AppendUtf8(out, code);
    }
    return out;
}

// The text of a title, album or artist sub-record; empty when it is not UTF-16 or does not fit its record.
std::string BomaText(std::string_view boma) {
    if (boma.size() < kTextStart || U32(boma, 20) != kTextUtf16) return {};
    size_t length = U32(boma, 24);
    if (length > boma.size() - kTextStart) return {};
    length = std::min(length, kMaxLibraryTextBytes) & ~size_t{1};
    return Utf16LeToUtf8(boma.substr(kTextStart, length));
}

void ReadBoma(std::string_view boma, uint32_t subtype, LibraryTrack& track) {
    switch (subtype) {
        case kBomaTitle: track.title = BomaText(boma); break;
        case kBomaAlbum: track.album = BomaText(boma); break;
        case kBomaArtist: track.artist = BomaText(boma); break;
        case kBomaNumbers:
            if (boma.size() < kNumbersDuration + 4) break;
            track.dateAddedMs = MacLocalTimeToUnixMs(U32(boma, kNumbersDateAdded));
            track.durationMs = U32(boma, kNumbersDuration);
            break;
        case kBomaPlays:
            if (boma.size() < kPlaysSkips + 4) break;
            track.lastPlayedMs = MacLocalTimeToUnixMs(U32(boma, kPlaysLastPlayed));
            track.playCount = U32(boma, kPlaysCount);
            track.skipCount = U32(boma, kPlaysSkips);
            break;
        default: break;
    }
}

// One itma record, whose size the caller checked.
bool ReadTrack(std::string_view record, LibraryTrack& track, std::string* error) {
    size_t at = U32(record, 4);
    uint32_t count = U32(record, 12);
    track.persistentId = U64(record, 16);
    for (uint32_t i = 0; i < count && at < record.size(); i++) {
        std::string_view rest = record.substr(at);
        if (rest.size() < kBomaMinBytes || rest.substr(0, 4) != "boma") {
            *error = "a track record of the library is damaged";
            return false;
        }
        uint32_t length = U32(rest, 8);
        if (length < kBomaMinBytes || length > rest.size()) {
            *error = "a track record of the library is damaged";
            return false;
        }
        ReadBoma(rest.substr(0, length), U32(rest, 12), track);
        at += length;
    }
    return true;
}

MusicDbResult ReadTrackList(std::string_view list) {
    if (list.size() < 12) return Failure("the track list of the library is damaged");
    size_t at = U32(list, 4);
    uint32_t count = U32(list, 8);
    if (at < 12 || at > list.size()) return Failure("the track list of the library is damaged");
    if (count > kMaxLibraryTracks) return Failure("the library holds more tracks than Ampify reads");
    MusicDbResult result;
    result.tracks.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        std::string_view rest = list.substr(at);
        if (rest.size() < kTrackMinBytes || rest.substr(0, 4) != "itma") return Failure("the track list is truncated");
        uint32_t header = U32(rest, 4);
        uint32_t length = U32(rest, 8);
        if (header < kTrackMinBytes || length < header || length > rest.size()) {
            return Failure("a track record of the library is damaged");
        }
        LibraryTrack track;
        std::string error;
        if (!ReadTrack(rest.substr(0, length), track, &error)) return Failure(error);
        result.tracks.push_back(std::move(track));
        at += length;
    }
    return result;
}

MusicDbResult ReadSections(std::string_view plain) {
    size_t at = 0;
    while (plain.size() - at >= kSectionMinBytes) {
        std::string_view rest = plain.substr(at);
        if (rest.substr(0, 4) != "hsma") return Failure("the library holds an unknown section");
        uint32_t header = U32(rest, 4);
        uint32_t length = U32(rest, 16);
        if (header < kSectionMinBytes || length < header || length > rest.size()) {
            return Failure("a section of the library is damaged");
        }
        std::string_view section = rest.substr(0, length);
        if (section.substr(header, 4) == "ltma") return ReadTrackList(section.substr(header));
        at += length;
    }
    return Failure("the library holds no track list");
}

}  // namespace

int64_t MacLocalTimeToUnixMs(uint32_t macSeconds) {
    if (macSeconds == 0) return 0;
    using namespace std::chrono;
    int64_t wall = static_cast<int64_t>(macSeconds) - kMacEpochToUnixSeconds;
    sys_seconds asUtc{std::chrono::seconds{wall}};
    sys_days day = floor<days>(asUtc);
    year_month_day date{day};
    hh_mm_ss<std::chrono::seconds> time{asUtc - day};
    std::tm local{};
    local.tm_year = static_cast<int>(date.year()) - 1900;
    local.tm_mon = static_cast<int>(static_cast<unsigned>(date.month())) - 1;
    local.tm_mday = static_cast<int>(static_cast<unsigned>(date.day()));
    local.tm_hour = static_cast<int>(time.hours().count());
    local.tm_min = static_cast<int>(time.minutes().count());
    local.tm_sec = static_cast<int>(time.seconds().count());
    local.tm_isdst = -1;  // let the C library find whether summer time applied that day
    std::time_t utc = std::mktime(&local);
    return (utc == static_cast<std::time_t>(-1) ? wall : static_cast<int64_t>(utc)) * 1000;
}

#ifdef _WIN32
bool ApplyMusicDbCipher(char* data, size_t length, bool decrypt, std::string* error) {
    if (length % kCipherBlock != 0 || length > ULONG_MAX) {
        *error = "the encrypted part of the library has a bad length";
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    auto ok = [](NTSTATUS status) { return status >= 0; };
    bool done = ok(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0)) &&
                ok(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                                     reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                                     sizeof(BCRYPT_CHAIN_MODE_ECB), 0)) &&
                ok(BCryptGenerateSymmetricKey(algorithm, &key, nullptr, 0,
                                              reinterpret_cast<PUCHAR>(const_cast<char*>(kCipherKey)),
                                              static_cast<ULONG>(kCipherBlock), 0));
    if (done) {
        ULONG written = 0;
        auto bytes = reinterpret_cast<PUCHAR>(data);
        auto size = static_cast<ULONG>(length);
        NTSTATUS status = decrypt ? BCryptDecrypt(key, bytes, size, nullptr, nullptr, 0, bytes, size, &written, 0)
                                  : BCryptEncrypt(key, bytes, size, nullptr, nullptr, 0, bytes, size, &written, 0);
        done = ok(status) && written == size;
    }
    if (key) BCryptDestroyKey(key);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!done) *error = "Windows could not decrypt the library";
    return done;
}
#else
bool ApplyMusicDbCipher(char*, size_t, bool, std::string* error) {
    *error = "the library can only be decrypted on Windows";
    return false;
}
#endif

MusicDbResult ParseMusicDb(std::string_view file) {
    if (file.size() > kMaxMusicDbFileBytes) return Failure("the library file is larger than 64 MB");
    if (file.size() < kEnvelopeMinBytes || file.substr(0, 4) != "hfma") {
        return Failure("not an Apple Music library file");
    }
    uint32_t header = U32(file, kEnvelopeHeaderBytes);
    if (header < kEnvelopeMinBytes || header > file.size()) return Failure("the header of the library is damaged");
    if (U32(file, kEnvelopeFileBytes) > file.size()) {
        return Failure("the library file is incomplete (Apple Music may be writing it)");
    }
    std::string payload(file.substr(header));
    size_t encrypted = std::min<size_t>(U32(file, kEnvelopeCryptBytes), payload.size()) & ~(kCipherBlock - 1);
    std::string error;
    if (encrypted > 0 && !ApplyMusicDbCipher(payload.data(), encrypted, true, &error)) return Failure(error);
    std::string plain;
    if (!InflateZlib(payload, kMaxMusicDbPlainBytes, &plain, &error)) {
        return Failure("the library could not be decoded: " + error);
    }
    return ReadSections(plain);
}

MusicDbResult ReadMusicDb(const std::filesystem::path& file) {
    std::error_code ec;
    uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec) return Failure("no Apple Music library was found");
    if (size > kMaxMusicDbFileBytes) return Failure("the library file is larger than 64 MB");
    std::optional<std::string> bytes = ReadWholeFile(file);
    if (!bytes) return Failure("the Apple Music library could not be read");
    return ParseMusicDb(*bytes);
}

}  // namespace ampify
