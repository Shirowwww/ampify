#pragma once

// The optional host service behind Ampify.player.queue (permissions player.read to list, player.control to change).
// See src/core/host.hpp for how a capability works. The Windows host offers it (src/win/player_bridge.*); a host without it
// makes Ampify.player.queue.list() answer { available: false }.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace ampify {

// Where an entry sits in the queue, after AMP.Services.PlayQueueSectionKind (Unknown, History, CurrentlyPlaying,
// NextManuallyAdded, NextMain, NextAutoPlay).
enum class QueueSection { Unknown, History, Current, NextAdded, Next, Autoplay };

struct QueueItem {
    std::string id;  // the entry's id in the queue, what remove() takes
    std::string title;
    std::string artist;
    int64_t durationMs = 0;
    QueueSection section = QueueSection::Unknown;
    std::string catalogId;  // the song's Apple Music catalog id, what add() takes; empty when unknown
    std::string album;
    // The entry's cover for an image node's src: its https address (about 1000 px), or an AppleArtworkSource when
    // Apple keeps the address internal; empty when the entry has no cover.
    std::string artwork;
};

// A cover that Apple's own loader draws, named in an image node's src instead of an address: the song playing
// ("artwork"), or a queue entry ("artwork:<entry id>").
struct AppleArtwork {
    std::optional<uint64_t> queueItem;  // nullopt: the song playing
};
std::string AppleArtworkSource(const AppleArtwork& artwork);
// The cover such a src names; nullopt for anything else (an address, or a malformed entry id).
std::optional<AppleArtwork> ParseAppleArtworkSource(std::string_view source);

struct QueueSnapshot {
    // False when the host only knows part of the queue (for example the current track alone). `reason` says why.
    bool complete = false;
    std::string source;  // "apple", "smtc", or empty
    std::string reason;
    std::vector<QueueItem> items;
};

enum class QueuePosition { Next, Last };

struct QueueResult {
    bool ok = false;
    std::string reason;  // when !ok, in plain English, for the element's journal
};

inline constexpr size_t kMaxQueueId = 128;

struct QueueHost {
    static constexpr std::string_view kCapabilityName = "player.queue";
    virtual ~QueueHost() = default;

    virtual QueueSnapshot Queue() = 0;
    // `trackId` is an Apple Music catalog id (IsCatalogId).
    virtual QueueResult AddToQueue(const std::string& trackId, QueuePosition position) = 0;
    // `itemId` is a QueueItem::id.
    virtual QueueResult RemoveFromQueue(const std::string& itemId) = 0;
};

// "history", "current", "next-added", "next", "autoplay", "unknown".
const char* QueueSectionName(QueueSection section);
// From the integer value of AMP.Services.PlayQueueSectionKind; Unknown when out of range.
QueueSection QueueSectionFromKind(int32_t kind);

// Ids are short printable ASCII without spaces: anything else is refused before the host sees it.
bool IsValidQueueId(std::string_view id);
// An Apple Music catalog id: 1 to 19 digits, as in https://music.apple.com/.../song/...?i=1440833098. Never "0", the
// AdamId Apple gives a file that is not in the catalog.
bool IsCatalogId(std::string_view id);
// "next" or "last" (also when empty); false for anything else.
bool ParseQueuePosition(std::string_view text, QueuePosition& position);

// What Ampify.player.queue.list() returns: { available, complete, source, reason, items: [{ id, title, artist,
// album, durationMs, section, catalogId }] }. `available` is false when the host has no queue service at all.
Json QueueJson(const QueueSnapshot& snapshot, bool available);

}  // namespace ampify
