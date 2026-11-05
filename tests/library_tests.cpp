// Apple Music's library file (src/core/library/*) and Ampify.library.tracks, permission library.read. The files are
// built here, field by field, at the offsets read from a real library (knowledge/apple-music-library-file.md).

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../src/core/library/inflate.hpp"
#include "../src/core/library/musicdb.hpp"
#include "pump.hpp"
#include "service_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;
using ampify::LibraryTrack;
using ampify::MusicDbResult;

// zlib.compress(..., 0), (..., 9) of short texts and of 40 lines that make a dynamic Huffman block, by Python's zlib.
const std::string kStoredStream("\x78\x01\x01\x0c\x00\xf3\xff\x73\x74\x6f\x72\x65\x64\x20\x62\x79\x74\x65\x73\x1f\xcf\x04\xd9", 23);
const std::string kFixedStream(
    "\x78\xda\x2b\x2e\xcf\xcc\x4b\x57\x28\x46\x22\x2b\x13\x15\x0a\xf2\x73\x52\x01\x7e\x33\x09\x83", 23);
const std::string kDynamicStream(
    "\x78\xda\x85\x92\x4b\x0a\x02\x31\x10\x05\xaf\x92\x13\x48\xd2\x9d\xef\x72\xee\x30\x17\x70\x5c\x89\x3f\x18\xc7\x85"
    "\xb7\x57\x84\x49\x67\xf1\xe4\xed\x02\x05\xfd\xa8\x50\xf3\x7a\x3c\x5d\x9c\x77\xcb\xdb\x4d\xeb\x76\x7e\x6e\xdf\xf7"
    "\xe3\xee\xa6\xeb\xf2\xba\x39\x7f\x70\xf3\x8f\x87\x81\x07\xe3\x61\xe7\x32\x70\x31\x2e\x3b\xd7\x81\x2b\xb8\x1f\x07"
    "\x1e\xc1\xfd\x34\xf0\x04\xee\xe7\x81\x67\x70\xbf\x60\xbf\x7e\xbf\x62\xbf\x7e\xbf\x61\x3f\xfb\x1f\x8f\x05\xfb\x40"
    "\x08\xd8\xb0\x2f\x04\xc1\x8a\x36\xa1\xd8\xd1\x26\x22\x96\xb4\x89\x84\x2d\x6d\x22\x63\x4d\x9b\x28\x58\xd3\x26\x2a"
    "\xd6\xb4\x89\x86\x35\xad\x24\x8f\x35\xfb\x84\x04\xd2\xaa\x08\x8b\x55\x49\xad\x12\x49\xae\x92\x48\xaf\x92\x49\xb0"
    "\x52\x48\xb1\x52\x49\xb2\xd2\x48\xb3\xea\x49\xb4\x1a\x48\xb4\x2a\x24\x5a\x55\x12\xad\x46\x12\xad\x26\x12\xad\x66"
    "\x12\xad\x16\x12\xad\x56\x12\xad\xb6\xbf\xd1\x7e\x00\xc3\x77\x90\x32",
    213);

std::string DynamicText() {
    std::string text;
    for (int i = 0; i < 40; i++) {
        text += "Track " + std::to_string(i) + " by Artist " + std::to_string(i % 7) + " on Album " +
                std::to_string(i % 3) + ". ";
    }
    return text;
}

// ---- Building a library file ----------------------------------------------------------------------------------

constexpr uint32_t kMacEpochToUnix = 2082844800;

void Put32(std::string& bytes, size_t at, uint32_t value) {
    for (int i = 0; i < 4; i++) bytes[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
}

void Put64(std::string& bytes, size_t at, uint64_t value) {
    Put32(bytes, at, static_cast<uint32_t>(value));
    Put32(bytes, at + 4, static_cast<uint32_t>(value >> 32));
}

std::string Record(const char* tag, size_t headerBytes) {
    std::string record(headerBytes, '\0');
    record.replace(0, 4, tag);
    return record;
}

std::string Boma(uint32_t subtype, size_t totalBytes) {
    std::string boma = Record("boma", totalBytes);
    Put32(boma, 4, 0x14);
    Put32(boma, 8, static_cast<uint32_t>(totalBytes));
    Put32(boma, 12, subtype);
    return boma;
}

std::string TextBoma(uint32_t subtype, const std::u16string& text) {
    std::string boma = Boma(subtype, 36 + 2 * text.size());
    Put32(boma, 20, 1);
    Put32(boma, 24, static_cast<uint32_t>(2 * text.size()));
    for (size_t i = 0; i < text.size(); i++) {
        boma[36 + 2 * i] = static_cast<char>(text[i] & 0xff);
        boma[37 + 2 * i] = static_cast<char>(text[i] >> 8);
    }
    return boma;
}

std::string NumbersBoma(uint32_t dateAdded, uint32_t durationMs) {
    std::string boma = Boma(0x1, 384);
    Put32(boma, 112, dateAdded);
    Put32(boma, 176, durationMs);
    return boma;
}

std::string PlaysBoma(uint64_t id, uint32_t lastPlayed, uint32_t plays, uint32_t skips) {
    std::string boma = Boma(0x17, 72);
    Put64(boma, 20, id);
    Put32(boma, 28, lastPlayed);
    Put32(boma, 32, plays);
    Put32(boma, 36, skips);
    return boma;
}

std::string Track(uint64_t id, const std::vector<std::string>& bomas, uint16_t year = 0) {
    std::string track = Record("itma", 0x178);
    Put32(track, 4, 0x178);
    track[0xa8] = static_cast<char>(year & 0xff);
    track[0xa9] = static_cast<char>(year >> 8);
    Put32(track, 12, static_cast<uint32_t>(bomas.size()));
    Put64(track, 16, id);
    for (const std::string& boma : bomas) track += boma;
    Put32(track, 8, static_cast<uint32_t>(track.size()));
    return track;
}

std::string Section(uint32_t type, const std::string& content) {
    std::string section = Record("hsma", 0x38);
    Put32(section, 4, 0x38);
    Put32(section, 12, type);
    Put32(section, 16, static_cast<uint32_t>(0x38 + content.size()));
    return section + content;
}

std::string TrackList(const std::vector<std::string>& tracks, uint32_t count) {
    std::string list = Record("ltma", 0x5c);
    Put32(list, 4, 0x5c);
    Put32(list, 8, count);
    for (const std::string& track : tracks) list += track;
    return list;
}

// A zlib stream of stored blocks: valid input for the decoder without a compressor.
std::string ZlibStored(const std::string& data) {
    std::string out("\x78\x01", 2);
    size_t at = 0;
    do {
        size_t length = std::min<size_t>(data.size() - at, 65535);
        bool last = at + length == data.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<char>(length & 0xff));
        out.push_back(static_cast<char>(length >> 8));
        out.push_back(static_cast<char>(~length & 0xff));
        out.push_back(static_cast<char>((~length >> 8) & 0xff));
        out += data.substr(at, length);
        at += length;
    } while (at < data.size());
    uint32_t a = 1, b = 0;
    for (unsigned char c : data) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(((b << 16 | a) >> shift) & 0xff));
    return out;
}

std::string Envelope(const std::string& payload, uint32_t cryptBytes) {
    std::string file = Record("hfma", 0xa0);
    Put32(file, 4, 0xa0);
    file.replace(0x10, 9, "1.7.0.155");
    Put32(file, 0x54, cryptBytes);
    file += payload;
    Put32(file, 8, static_cast<uint32_t>(file.size()));
    return file;
}

const uint32_t kPlayed = kMacEpochToUnix + 1790000000;  // 2026-09-21, in Apple's local-time seconds
const uint32_t kAdded = kMacEpochToUnix + 1727000000;   // 2024-09-22

std::string LibraryPlain() {
    std::vector<std::string> tracks = {
        Track(0x0123456789ABCDEFull,
              {TextBoma(0x2, u"Sibérie \U0001F3B5"), TextBoma(0x4, u"Artiste"), TextBoma(0x3, u"Album"),
               Boma(0x36, 40), NumbersBoma(kAdded, 162677), PlaysBoma(0x0123456789ABCDEFull, kPlayed, 40, 1),
               TextBoma(0x5, u"Rap"), TextBoma(0xC, u"Compositrice"), TextBoma(0x1B, u"Artiste d'album")},
              2024),
        Track(0x42, {TextBoma(0x2, u"Never played")}),
    };
    return Section(3, std::string(0xa0, '\0')) + Section(6, "playlists") + Section(1, TrackList(tracks, 2));
}

MusicDbResult ParsePlain(const std::string& plain) { return ampify::ParseMusicDb(Envelope(ZlibStored(plain), 0)); }

}  // namespace

TEST_CASE(Inflate_DecodesStoredFixedAndDynamicBlocks) {
    std::string out, error;
    CHECK(ampify::InflateZlib(kStoredStream, 1024, &out, &error));
    CHECK(out == "stored bytes");
    CHECK(ampify::InflateZlib(kFixedStream, 1024, &out, &error));
    CHECK(out == "swing swing swing ya pole");
    CHECK(ampify::InflateZlib(kDynamicStream, 1 << 16, &out, &error));
    CHECK(out == DynamicText());
    std::string big(200000, 'x');
    for (size_t i = 0; i < big.size(); i += 7) big[i] = static_cast<char>('a' + i % 26);
    CHECK(ampify::InflateZlib(ZlibStored(big), big.size(), &out, &error));
    CHECK(out == big);
}

TEST_CASE(Inflate_RefusesDamagedTruncatedAndOversizedStreams) {
    std::string out, error;
    for (size_t length = 0; length < kDynamicStream.size(); length++) {
        if (ampify::InflateZlib(kDynamicStream.substr(0, length), 1 << 16, &out, &error)) {
            ampify::test::ReportFailure(__FILE__, __LINE__, "accepted a prefix of " + std::to_string(length));
        }
    }
    std::string badChecksum = kFixedStream;
    badChecksum.back() ^= 1;
    CHECK(!ampify::InflateZlib(badChecksum, 1024, &out, &error));
    CHECK(error == "wrong checksum");
    CHECK(!ampify::InflateZlib(kDynamicStream, 100, &out, &error));
    CHECK(error == "the decoded data is larger than allowed");
    CHECK(out.empty());
    CHECK(!ampify::InflateZlib("not zlib at all", 1024, &out, &error));
    // Every single-bit flip either decodes to something or is refused; none may crash or overrun.
    for (size_t i = 0; i < kDynamicStream.size() * 8; i++) {
        std::string flipped = kDynamicStream;
        flipped[i / 8] ^= static_cast<char>(1 << (i % 8));
        ampify::InflateZlib(flipped, 1 << 16, &out, &error);
    }
}

TEST_CASE(MusicDb_ReadsTitlesAndPlayCounts) {
    MusicDbResult result = ParsePlain(LibraryPlain());
    CHECK(result.error.empty());
    CHECK(result.tracks.size() == 2);
    if (result.tracks.size() != 2) return;
    const LibraryTrack& played = result.tracks[0];
    CHECK(played.persistentId == 0x0123456789ABCDEFull);
    CHECK(played.title == "Sib\xC3\xA9rie \xF0\x9F\x8E\xB5");
    CHECK(played.artist == "Artiste");
    CHECK(played.album == "Album");
    CHECK(played.playCount == 40);
    CHECK(played.skipCount == 1);
    CHECK(played.durationMs == 162677);
    CHECK(played.genre == "Rap");
    CHECK(played.composer == "Compositrice");
    CHECK(played.albumArtist == "Artiste d'album");
    CHECK(played.year == 2024);
    CHECK(played.lastPlayedMs == ampify::MacLocalTimeToUnixMs(kPlayed));
    CHECK(played.dateAddedMs == ampify::MacLocalTimeToUnixMs(kAdded));
    const LibraryTrack& never = result.tracks[1];
    CHECK(never.title == "Never played");
    CHECK(never.playCount == 0 && never.lastPlayedMs == 0 && never.dateAddedMs == 0 && never.durationMs == 0);
    CHECK(never.genre.empty() && never.year == 0);
}

TEST_CASE(MusicDb_DatesAreLocalWallClockSeconds) {
    CHECK(ampify::MacLocalTimeToUnixMs(0) == 0);
    int64_t asUtc = int64_t{1790000000} * 1000;
    int64_t local = ampify::MacLocalTimeToUnixMs(kPlayed);
    constexpr int64_t kFourteenHoursMs = 14 * 3600 * 1000;
    CHECK(local % 1000 == 0);
    CHECK(local >= asUtc - kFourteenHoursMs && local <= asUtc + kFourteenHoursMs);
}

TEST_CASE(MusicDb_RefusesTruncatedDamagedAndForeignFiles) {
    std::string file = Envelope(ZlibStored(LibraryPlain()), 0);
    for (size_t length = 0; length < file.size(); length += 13) {
        if (ampify::ParseMusicDb(file.substr(0, length)).error.empty()) {
            ampify::test::ReportFailure(__FILE__, __LINE__, "accepted a file cut at " + std::to_string(length));
        }
    }
    CHECK(ampify::ParseMusicDb("").error == "not an Apple Music library file");
    CHECK(!ampify::ParseMusicDb(std::string(4096, 'z')).error.empty());
    CHECK(!ampify::ParseMusicDb(Envelope("garbage, not zlib", 0)).error.empty());

    // Inside a valid stream: a track count larger than the records, a sub-record longer than its track, no list.
    std::vector<std::string> one = {Track(1, {TextBoma(0x2, u"A")})};
    CHECK(ParsePlain(Section(1, TrackList(one, 3))).error == "the track list is truncated");
    std::string longBoma = Track(1, {TextBoma(0x2, u"A")});
    Put32(longBoma, 0x178 + 8, 5000);
    CHECK(ParsePlain(Section(1, TrackList({longBoma}, 1))).error == "a track record of the library is damaged");
    CHECK(ParsePlain(Section(6, "playlists")).error == "the library holds no track list");
    CHECK(ParsePlain(Section(1, TrackList(one, 1)) + "junk that is not a section").error.empty());
    std::string hugeSection = Section(6, "x");
    Put32(hugeSection, 16, 0xFFFFFFF0u);
    CHECK(ParsePlain(hugeSection).error == "a section of the library is damaged");

    // A text longer than its record is dropped, not read past the record.
    std::string lyingText = TextBoma(0x2, u"Title");
    Put32(lyingText, 24, 4000);
    MusicDbResult lying = ParsePlain(Section(1, TrackList({Track(7, {lyingText})}, 1)));
    CHECK(lying.error.empty() && lying.tracks.size() == 1 && lying.tracks[0].title.empty());
}

#ifdef _WIN32
TEST_CASE(MusicDb_DecryptsLikeAppleMusic) {
    // Python: AES.new(b"BHUILuilfghuila3", AES.MODE_ECB).encrypt(b"Ampify musicdb!!0123456789abcdef").
    std::string block("\xb4\x83\xe6\x87\x9f\x0c\xb6\x07\xd9\x94\xc3\xcc\x18\x3f\x10\x65\x94\x4a\xc5\xb0\x66\x43\xbc\xb6"
                      "\x8a\x57\x0f\x07\x03\x03\xd8\xda",
                      32);
    std::string error;
    CHECK(ampify::ApplyMusicDbCipher(block.data(), block.size(), true, &error));
    CHECK(block == "Ampify musicdb!!0123456789abcdef");
    CHECK(!ampify::ApplyMusicDbCipher(block.data(), 15, true, &error));

    // As Apple writes it: the first 100 KB of the payload encrypted, here all of it but its last partial block.
    std::string payload = ZlibStored(LibraryPlain());
    size_t encrypted = payload.size() & ~size_t{15};
    CHECK(ampify::ApplyMusicDbCipher(payload.data(), encrypted, false, &error));
    MusicDbResult result = ampify::ParseMusicDb(Envelope(payload, 102400));
    CHECK(result.error.empty());
    CHECK(result.tracks.size() == 2 && result.tracks[0].playCount == 40);
}
#endif

TEST_CASE(LibraryModule_ListsTracksWithThePermission) {
    ampify::test::ServiceFixture<> f;
    fs::path dir = fs::temp_directory_path() / "ampify-tests" / "library";
    fs::create_directories(dir);
    f.host.libraryFile = dir / "Library.musicdb";
    std::ofstream(f.host.libraryFile, std::ios::binary) << Envelope(ZlibStored(LibraryPlain()), 0);

    auto denied = f.Make("library-denied", R"(
        Ampify.library.tracks().catch((e) => Ampify.log("denied " + e.name));
    )");
    CHECK(denied->Start());
    CHECK(ampify::test::PumpUntil(*denied, [&] { return f.Logged("denied PermissionDenied"); }));

    auto element = f.Make("library-demo", R"(
        Ampify.library.tracks().then((tracks) => {
            const t = tracks[0];
            Ampify.log("tracks " + tracks.length + " " + t.persistentId + " " + t.title.length + " " + t.artist +
                       " plays " + t.playCount + " skips " + t.skipCount + " ms " + t.durationMs +
                       " dates " + (t.lastPlayed > t.dateAdded) + " never " + tracks[1].lastPlayed);
        });
    )", R"("library.read")");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] {
        return f.Logged("tracks 2 0123456789ABCDEF 10 Artiste plays 40 skips 1 ms 162677 dates true never 0");
    }));

    f.host.libraryFile = dir / "missing.musicdb";
    auto missing = f.Make("library-missing", R"(
        Ampify.library.tracks().catch((e) => Ampify.log("missing " + e.message));
    )", R"("library.read")");
    CHECK(missing->Start());
    CHECK(ampify::test::PumpUntil(*missing, [&] { return f.Logged("missing library.tracks: no Apple Music library"); }));
}

// Set AMPIFY_MUSICDB to a copy of a real Library.musicdb to check the reader on it: it prints what it found.
TEST_CASE(MusicDb_ReadsTheFileOfAmpifyMusicDb) {
    const char* path = std::getenv("AMPIFY_MUSICDB");
    if (!path || !*path) return;
    MusicDbResult result = ampify::ReadMusicDb(fs::path(path));
    CHECK(result.error.empty());
    std::vector<LibraryTrack> top = result.tracks;
    std::sort(top.begin(), top.end(), [](const LibraryTrack& a, const LibraryTrack& b) { return a.playCount > b.playCount; });
    std::cout << "AMPIFY_MUSICDB: " << result.tracks.size() << " tracks" << result.error << "\n";
    for (size_t i = 0; i < top.size() && i < 5; i++) std::cout << "  " << top[i].playCount << "  " << top[i].title << "\n";
}
