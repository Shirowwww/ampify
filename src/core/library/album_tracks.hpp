#pragma once

// An album of the library and Apple Music's names for it. Apple's views name a library item with an id such as
// "DBID:0xf74fb9a3f2dce41c-PID:0xdea20c15cb557038-PPID:0x0-PIPID:0x0-IKIND:eAlbum": the library's id, the item's
// persistent id (the one of Library.musicdb) and its kind (knowledge/apple-music-queue.md).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "musicdb.hpp"

namespace ampify {

struct LibraryItemId {
    uint64_t database = 0;
    uint64_t persistent = 0;
    std::string kind;  // "eAlbum", "eSong"...
};

// Empty when `text` is not such an id, or names no item (a persistent id of 0).
std::optional<LibraryItemId> ParseLibraryItemId(std::string_view text);

// The id Apple gives a library song, and the JSON of its content descriptor (IContentDescriptorStatics), which
// StartOrQueuePlaybackArgs takes as its start item.
std::string SongLibraryItemId(uint64_t database, uint64_t song);
std::string SongDescriptorJson(uint64_t database, uint64_t song);

// The tracks of one album in the order of its track list: by disc (0, unsaid, counts as the first), then by number,
// the unnumbered ones last, then by title.
std::vector<LibraryTrack> TracksOfAlbum(std::span<const LibraryTrack> tracks, uint64_t albumId);

// A track's disc as the track list counts it: 0, unsaid, is the first.
inline uint32_t DiscOf(const LibraryTrack& track) { return track.discNumber == 0 ? 1 : track.discNumber; }

// How many discs those tracks come from; 0 for none.
size_t DiscCount(std::span<const LibraryTrack> tracks);

}  // namespace ampify
