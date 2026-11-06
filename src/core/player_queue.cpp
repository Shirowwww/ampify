#include "player_queue.hpp"

#include <charconv>
#include <system_error>

namespace ampify {

const char* QueueSectionName(QueueSection section) {
    switch (section) {
        case QueueSection::History: return "history";
        case QueueSection::Current: return "current";
        case QueueSection::NextAdded: return "next-added";
        case QueueSection::Next: return "next";
        case QueueSection::Autoplay: return "autoplay";
        case QueueSection::Unknown: return "unknown";
    }
    return "unknown";
}

QueueSection QueueSectionFromKind(int32_t kind) {
    switch (kind) {
        case 1: return QueueSection::History;
        case 2: return QueueSection::Current;
        case 3: return QueueSection::NextAdded;
        case 4: return QueueSection::Next;
        case 5: return QueueSection::Autoplay;
        default: return QueueSection::Unknown;
    }
}

bool IsValidQueueId(std::string_view id) {
    if (id.empty() || id.size() > kMaxQueueId) return false;
    for (char c : id) {
        if (c <= 0x20 || c >= 0x7F) return false;
    }
    return true;
}

bool IsCatalogId(std::string_view id) {
    if (id.empty() || id.size() > 19) return false;
    for (char c : id) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

namespace {
constexpr std::string_view kQueueArtworkPrefix = "artwork:";
}  // namespace

std::string QueueArtworkSource(uint64_t itemId) {
    return std::string(kQueueArtworkPrefix) + std::to_string(itemId);
}

std::optional<uint64_t> QueueArtworkItem(std::string_view source) {
    if (!source.starts_with(kQueueArtworkPrefix)) return std::nullopt;
    std::string_view digits = source.substr(kQueueArtworkPrefix.size());
    uint64_t id = 0;
    auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
    if (digits.empty() || error != std::errc() || end != digits.data() + digits.size()) return std::nullopt;
    return id;
}

bool ParseQueuePosition(std::string_view text, QueuePosition& position) {
    if (text.empty() || text == "last") {
        position = QueuePosition::Last;
        return true;
    }
    if (text == "next") {
        position = QueuePosition::Next;
        return true;
    }
    return false;
}

Json QueueJson(const QueueSnapshot& snapshot, bool available) {
    Json::Array items;
    for (const QueueItem& item : snapshot.items) {
        Json::Object entry;
        entry.emplace_back("id", Json(item.id));
        entry.emplace_back("title", Json(item.title));
        entry.emplace_back("artist", Json(item.artist));
        entry.emplace_back("album", Json(item.album));
        entry.emplace_back("durationMs", Json(static_cast<double>(item.durationMs)));
        entry.emplace_back("section", Json(std::string(QueueSectionName(item.section))));
        entry.emplace_back("catalogId", Json(item.catalogId));
        entry.emplace_back("artwork", Json(item.artwork));
        items.emplace_back(std::move(entry));
    }
    Json::Object members;
    members.emplace_back("available", Json(available));
    members.emplace_back("complete", Json(available && snapshot.complete));
    members.emplace_back("source", Json(snapshot.source));
    members.emplace_back("reason", Json(available ? snapshot.reason
                                                  : std::string("no queue in this process")));
    members.emplace_back("items", Json(std::move(items)));
    return Json(std::move(members));
}

}  // namespace ampify
