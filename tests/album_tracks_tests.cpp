// apple.albumClick: an album's tracks from the library file and Apple's ids for them
// (src/core/library/album_tracks.hpp).

#include <string>
#include <vector>

#include "../src/core/library/album_tracks.hpp"
#include "test.hpp"

namespace {

ampify::LibraryTrack Track(uint64_t id, uint64_t album, uint32_t number, std::string title, uint32_t disc = 0) {
    ampify::LibraryTrack track;
    track.persistentId = id;
    track.albumId = album;
    track.trackNumber = number;
    track.discNumber = disc;
    track.title = std::move(title);
    return track;
}

}  // namespace

TEST_CASE(AlbumTracks_ReadsApplesLibraryItemIds) {
    auto album = ampify::ParseLibraryItemId("DBID:0xf74fb9a3f2dce41c-PID:0xdea20c15cb557038-PPID:0x0-PIPID:0x0-IKIND:eAlbum");
    CHECK(album.has_value());
    if (!album) return;
    CHECK(album->database == 0xf74fb9a3f2dce41cull);
    CHECK(album->persistent == 0xdea20c15cb557038ull);
    CHECK(album->kind == "eAlbum");
}

TEST_CASE(AlbumTracks_RefusesWhatIsNotALibraryItem) {
    CHECK(!ampify::ParseLibraryItemId("1658011741"));                       // a catalog album
    CHECK(!ampify::ParseLibraryItemId(""));
    CHECK(!ampify::ParseLibraryItemId("DBID:0x1-PID:0x0-PPID:0x0-PIPID:0x0-IKIND:eAlbum"));  // no item
    CHECK(!ampify::ParseLibraryItemId("DBID:0x1-PPID:0x5-PIPID:0x0-IKIND:eAlbum"));          // PPID is not PID
    CHECK(!ampify::ParseLibraryItemId("DBID:0x1-PID:0xZZ-PPID:0x0-PIPID:0x0-IKIND:eAlbum"));
    CHECK(!ampify::ParseLibraryItemId("DBID:0x1-PID:0x11112222333344445-PPID:0x0-IKIND:eAlbum"));  // 17 digits
    CHECK(!ampify::ParseLibraryItemId("DBID:0x1-PID:0x5-PPID:0x0-PIPID:0x0"));                       // no kind
    CHECK(!ampify::ParseLibraryItemId("x DBID:0x1-PID:0x5-PPID:0x0-PIPID:0x0-IKIND:eAlbum"));
}

TEST_CASE(AlbumTracks_WritesASongAsAppleSerializesIt) {
    CHECK(ampify::SongLibraryItemId(0xf74fb9a3f2dce41cull, 0x838c4443377adeeaull) ==
          "DBID:0xf74fb9a3f2dce41c-PID:0x838c4443377adeea-PPID:0x0-PIPID:0x0-IKIND:eSong");
    CHECK(ampify::SongDescriptorJson(0x1, 0x2) ==
          R"([{"kind":"song","identifiers":{"libraryItemID":"DBID:0x1-PID:0x2-PPID:0x0-PIPID:0x0-IKIND:eSong"}}])");
    auto song = ampify::ParseLibraryItemId(ampify::SongLibraryItemId(7, 9));
    CHECK(song && song->database == 7 && song->persistent == 9 && song->kind == "eSong");
}

TEST_CASE(AlbumTracks_KeepsTheAlbumsTracksInTrackListOrder) {
    std::vector<ampify::LibraryTrack> library = {
        Track(1, 50, 10, "Crystal"), Track(2, 60, 1, "Other album"), Track(3, 50, 0, "B, no number"),
        Track(4, 50, 1, "10 Balles"), Track(5, 50, 0, "A, no number"), Track(6, 50, 4, "5712"),
    };
    std::vector<ampify::LibraryTrack> album = ampify::TracksOfAlbum(library, 50);
    std::vector<uint64_t> ids;
    for (const ampify::LibraryTrack& track : album) ids.push_back(track.persistentId);
    CHECK((ids == std::vector<uint64_t>{4, 6, 1, 5, 3}));
    CHECK(ampify::TracksOfAlbum(library, 0).empty());   // tracks without an album are not one album
    CHECK(ampify::TracksOfAlbum(library, 99).empty());
}

TEST_CASE(AlbumTracks_PutsEachDiscAfterThePreviousOne) {
    std::vector<ampify::LibraryTrack> library = {
        Track(1, 50, 1, "Count Me Out", 2), Track(2, 50, 2, "N95", 1), Track(3, 50, 1, "United in Grief", 1),
        Track(4, 50, 3, "No disc said", 0),  // a disc of 0 is the first one
    };
    std::vector<uint64_t> ids;
    for (const ampify::LibraryTrack& track : ampify::TracksOfAlbum(library, 50)) ids.push_back(track.persistentId);
    CHECK((ids == std::vector<uint64_t>{3, 2, 4, 1}));
    CHECK(ampify::DiscCount(ampify::TracksOfAlbum(library, 50)) == 2);
    CHECK(ampify::DiscCount({}) == 0);
}

// Issue #7: the panel marks the song that plays, which Apple's player names by title and album only.
TEST_CASE(AlbumTracks_FindsTheSongThatPlaysByItsTitleAndAlbum) {
    std::vector<ampify::LibraryTrack> album = {Track(1, 50, 1, "Intro"), Track(2, 50, 2, "Stay Gold"),
                                               Track(3, 50, 3, "Outro")};
    for (ampify::LibraryTrack& track : album) track.album = "Map of the Soul: 7";
    CHECK(ampify::PlayingTrackIndex(album, "Stay Gold", "Map of the Soul: 7") == std::optional<size_t>(1));
    CHECK(!ampify::PlayingTrackIndex(album, "Stay Gold", "Another album").has_value());
    CHECK(!ampify::PlayingTrackIndex(album, "", "Map of the Soul: 7").has_value());
    album.push_back(Track(4, 50, 4, "Intro"));
    album.back().album = "Map of the Soul: 7";
    CHECK(!ampify::PlayingTrackIndex(album, "Intro", "Map of the Soul: 7").has_value());  // twice: unknown which
}

// Issue #7: a compilation's tile says "Various Artists"; the album's own artist wins when the file names one.
TEST_CASE(AlbumTracks_NamesACompilationAfterItsAlbumArtist) {
    std::vector<ampify::LibraryTrack> album = {Track(1, 50, 1, "Mixed"), Track(2, 50, 2, "Unmixed")};
    album[0].artist = "Some Producer";
    CHECK(ampify::AlbumArtistOf(album, "Various Artists") == "Various Artists");
    CHECK(ampify::AlbumArtistOf(album, "") == "Some Producer");
    album[0].albumArtist = "James Grant & Jody Wisternoff";
    CHECK(ampify::AlbumArtistOf(album, "Various Artists") == "James Grant & Jody Wisternoff");
    CHECK(ampify::AlbumArtistOf({}, "Various Artists") == "Various Artists");
}
