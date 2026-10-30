#pragma once

// The optional host service behind AMK.player.queue (permissions player.read to list, player.control to change).
// See core/host.hpp for how a capability works. The Windows host offers it (win/player_bridge.*); a host without it
// makes AMK.player.queue.list() answer { available: false }.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace amk {

// Where an entry sits in the queue, after AMP.Services.PlayQueueSectionKind (Unknown, History, CurrentlyPlaying,
// NextManuallyAdded, NextMain, NextAutoPlay).
enum class QueueSection { Unknown, History, Current, NextAdded, Next, Autoplay };

struct QueueItem {
    std::string id;  // the entry's id in the queue, what remove() takes
    std::string title;
    std::string artist;
    int64_t durationMs = 0;
    QueueSection section = QueueSection::Unknown;
};

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
    std::string reason;  // when !ok, in simple French, for the element's journal
};

inline constexpr size_t kMaxQueueId = 128;

struct QueueHost {
    static constexpr std::string_view kCapabilityName = "player.queue";
    virtual ~QueueHost() = default;

    virtual QueueSnapshot Queue() = 0;
    // `trackId` is a track id as AMK.player.state() gives it, or an Apple Music catalog id.
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
// "next" or "last" (also when empty); false for anything else.
bool ParseQueuePosition(std::string_view text, QueuePosition& position);

// What AMK.player.queue.list() returns: { available, complete, source, reason, items: [{ id, title, artist,
// durationMs, section }] }. `available` is false when the host has no queue service at all.
Json QueueJson(const QueueSnapshot& snapshot, bool available);

}  // namespace amk
