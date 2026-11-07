#include "player_model.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <tuple>

#include "amp_abi.hpp"
#include "md5.hpp"

namespace ampify {

namespace {

// Without SMTC, a duration below 20 000 is read as seconds (a song is rarely shorter than 20 s), and from 36 000 000
// as 100 ns ticks (3.6 s; in ms it would be a 10 h track).
constexpr int64_t kSecondsBelow = 20'000;
constexpr int64_t kTicksFrom = 36'000'000;

constexpr int kArtworkSize = 1000;
constexpr size_t kMaxArtworkCodeLength = 8;
constexpr char kDefaultCropCode[] = "bb";
constexpr char kDefaultArtworkFormat[] = "jpg";

bool HasPrefixNoCase(const std::string& text, const char* prefix) {
    for (size_t i = 0; prefix[i]; ++i) {
        if (i >= text.size() || std::tolower(static_cast<unsigned char>(text[i])) != prefix[i]) return false;
    }
    return true;
}

// A crop code or a format goes into the address: letters and digits only, else the default.
std::string Token(const std::string& value, const char* fallback) {
    bool ok = !value.empty() && value.size() <= kMaxArtworkCodeLength;
    for (char c : value) ok = ok && std::isalnum(static_cast<unsigned char>(c));
    return ok ? value : fallback;
}

// The template may reach us with its braces escaped, as Windows.Foundation.Uri writes them.
void FillPlaceholder(std::string& text, const std::string& name, const std::string& value) {
    for (const std::string& token : {"{" + name + "}", "%7B" + name + "%7D", "%7b" + name + "%7d"}) {
        for (size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size())) {
            text.replace(at, token.size(), value);
        }
    }
}

}  // namespace

std::string ArtworkAddress(const ArtworkReading& artwork) {
    const std::string& uri = artwork.uri;
    if (!HasPrefixNoCase(uri, "https://")) return "";
    for (char c : uri) {
        // The address is handed to scripts and to image loaders: no white space, control or quote characters.
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F || c == '"' || c == '<' || c == '>' || c == '\\') return "";
    }
    int width = kArtworkSize, height = kArtworkSize;
    if (artwork.width > 0 && artwork.height > 0) {
        double ratio = artwork.width / artwork.height;
        if (ratio >= 1) height = std::max(1, static_cast<int>(std::lround(kArtworkSize / ratio)));
        else width = std::max(1, static_cast<int>(std::lround(kArtworkSize * ratio)));
    }
    std::string address = uri;
    FillPlaceholder(address, "w", std::to_string(width));
    FillPlaceholder(address, "h", std::to_string(height));
    FillPlaceholder(address, "c", Token(artwork.cropCode, kDefaultCropCode));
    FillPlaceholder(address, "f", Token(artwork.format, kDefaultArtworkFormat));
    return address;
}

std::string ArtworkColorHex(const ArtworkReading& artwork) {
    if (!artwork.hasColor) return "";
    if (artwork.alpha == 0 && artwork.red == 0 && artwork.green == 0 && artwork.blue == 0) return "";
    char text[8];
    std::snprintf(text, sizeof text, "#%02X%02X%02X", artwork.red, artwork.green, artwork.blue);
    return text;
}

std::string CatalogLookupAddress(const std::string& catalogId, const std::string& storefront) {
    auto all = [](const std::string& text, int (*test)(int)) {
        return std::all_of(text.begin(), text.end(), [test](char c) { return test(static_cast<unsigned char>(c)) != 0; });
    };
    if (!IsCatalogId(catalogId) || storefront.size() != 2 || !all(storefront, std::isalpha)) return "";
    return "https://itunes.apple.com/lookup?id=" + catalogId + "&country=" + storefront;
}

std::string CatalogCoverAddress(const std::string& artworkUrl100, double pixels) {
    constexpr int kSmallest = 600, kLargest = 3000, kStep = 100;
    constexpr char kScheme[] = "https://";
    if (!HasPrefixNoCase(artworkUrl100, kScheme)) return "";
    size_t hostEnd = artworkUrl100.find('/', sizeof kScheme - 1);
    std::string host = artworkUrl100.substr(sizeof kScheme - 1, hostEnd - (sizeof kScheme - 1));
    constexpr std::string_view kHost = ".mzstatic.com";
    if (hostEnd == std::string::npos || host.size() <= kHost.size() || !host.ends_with(kHost)) return "";
    // Letters, digits, dots and hyphens only: "evil.com#.mzstatic.com" ends with the name, but its host is evil.com.
    bool plainHost = std::all_of(host.begin(), host.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-';
    });
    if (!plainHost) return "";
    // The last segment is "<w>x<h><crop>.<format>", such as 100x100bb.jpg.
    size_t name = artworkUrl100.rfind('/') + 1;
    size_t cross = artworkUrl100.find('x', name);
    size_t crop = cross == std::string::npos ? cross : artworkUrl100.find_first_not_of("0123456789", cross + 1);
    if (cross == std::string::npos || cross == name || crop == cross + 1 || crop == std::string::npos) return "";
    for (size_t i = name; i < cross; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(artworkUrl100[i]))) return "";
    }
    int wanted = static_cast<int>(std::ceil(std::max(0.0, pixels) / kStep)) * kStep;
    std::string side = std::to_string(std::clamp(wanted, kSmallest, kLargest));
    return artworkUrl100.substr(0, name) + side + "x" + side + artworkUrl100.substr(crop);
}

AmpTimeUnit DetectAmpTimeUnit(int64_t durationRaw, std::optional<int64_t> smtcDurationMs) {
    if (durationRaw <= 0) return AmpTimeUnit::Unknown;
    if (smtcDurationMs && *smtcDurationMs > 0) {
        // The unit whose conversion lands closest to the SMTC duration, compared on a log scale.
        const AmpTimeUnit units[] = {AmpTimeUnit::Seconds, AmpTimeUnit::Milliseconds, AmpTimeUnit::Ticks100ns};
        AmpTimeUnit best = AmpTimeUnit::Unknown;
        double bestDistance = 0;
        for (AmpTimeUnit unit : units) {
            double ms = static_cast<double>(AmpTimeToMs(durationRaw, unit));
            if (ms <= 0) continue;
            double distance = std::fabs(std::log(ms / static_cast<double>(*smtcDurationMs)));
            if (best == AmpTimeUnit::Unknown || distance < bestDistance) {
                best = unit;
                bestDistance = distance;
            }
        }
        // More than a factor 3 away from every unit: the two sources do not describe the same track yet.
        if (best != AmpTimeUnit::Unknown && bestDistance < std::log(3.0)) return best;
    }
    if (durationRaw < kSecondsBelow) return AmpTimeUnit::Seconds;
    if (durationRaw < kTicksFrom) return AmpTimeUnit::Milliseconds;
    return AmpTimeUnit::Ticks100ns;
}

int64_t AmpTimeToMs(int64_t raw, AmpTimeUnit unit) {
    if (raw <= 0) return 0;
    switch (unit) {
        case AmpTimeUnit::Seconds: return raw * 1000;
        case AmpTimeUnit::Milliseconds: return raw;
        case AmpTimeUnit::Ticks100ns: return TicksToMs(raw);
        case AmpTimeUnit::Unknown: return 0;
    }
    return 0;
}

const char* AmpTimeUnitName(AmpTimeUnit unit) {
    switch (unit) {
        case AmpTimeUnit::Seconds: return "s";
        case AmpTimeUnit::Milliseconds: return "ms";
        case AmpTimeUnit::Ticks100ns: return "100 ns";
        case AmpTimeUnit::Unknown: return "unknown";
    }
    return "unknown";
}

std::string TrackIdOf(const std::string& title, const std::string& artist, const std::string& album) {
    return "t-" + Md5Hex(title + '\n' + artist + '\n' + album).substr(0, 16);
}

std::string VariantOfAudioFormat(int32_t format) {
    switch (static_cast<amp::AudioFormat>(format)) {
        case amp::AudioFormat::Other: return "other";
        case amp::AudioFormat::Lossless: return "lossless";
        case amp::AudioFormat::HiResLossless: return "hi-res-lossless";
        case amp::AudioFormat::DolbyAtmos: return "dolby-atmos";
        case amp::AudioFormat::Spatial: return "spatial";
        case amp::AudioFormat::Surround: return "surround";
    }
    return "format-" + std::to_string(format);
}

bool IsAtmosFormat(int32_t format) { return format == static_cast<int32_t>(amp::AudioFormat::DolbyAtmos); }

bool IsLosslessFormat(int32_t format) {
    return format == static_cast<int32_t>(amp::AudioFormat::Lossless) ||
           format == static_cast<int32_t>(amp::AudioFormat::HiResLossless);
}

std::pair<std::string, std::string> SplitArtistAlbum(const std::string& text) {
    static const std::string kSeparator = " \xE2\x80\x94 ";  // " \u2014 ", an em dash between spaces
    size_t at = text.find(kSeparator);
    if (at == std::string::npos) return {text, ""};
    return {text.substr(0, at), text.substr(at + kSeparator.size())};
}

QueueItem QueueItemFromAmp(const AmpQueueEntry& entry) {
    QueueItem item;
    item.id = std::to_string(entry.id);
    item.title = entry.title;
    std::tie(item.artist, item.album) = SplitArtistAlbum(entry.subtitle);
    // The measured unit, not a guess from the value: a guess takes a song under 20 s for seconds.
    item.durationMs = AmpTimeToMs(entry.durationRaw, kQueueDurationUnit);
    item.section = QueueSectionFromKind(entry.sectionKind);
    item.catalogId = entry.catalogId;
    // Apple Music 1.1792 keeps its covers' addresses internal: Apple's loader then draws the entry's cover.
    item.artwork = ArtworkAddress(entry.artwork);
    if (item.artwork.empty() && entry.hasArtwork) item.artwork = AppleArtworkSource({entry.id});
    return item;
}

PlayerSnapshot SnapshotFromAmp(const AmpReading& reading, AmpTimeUnit unit) {
    PlayerSnapshot snapshot;
    snapshot.source = "apple";
    snapshot.playing = reading.playing && !reading.paused;
    // A placeholder item is the empty slot Apple shows before anything is loaded.
    if (!reading.hasItem || reading.placeholder) return snapshot;
    snapshot.positionMs = AmpTimeToMs(reading.positionRaw, unit);
    snapshot.durationMs = AmpTimeToMs(reading.durationRaw, unit);
    if (snapshot.durationMs > 0) snapshot.positionMs = std::min(snapshot.positionMs, snapshot.durationMs);

    TrackInfo track;
    track.title = reading.title;
    track.artist = reading.artist;
    track.album = reading.album;
    // Some items leave the artist empty and only fill the subtitle, "Artist \u2014 Album".
    if (track.artist.empty() && !reading.subtitle.empty()) {
        auto [artist, album] = SplitArtistAlbum(reading.subtitle);
        track.artist = artist;
        if (track.album.empty()) track.album = album;
    }
    track.id = TrackIdOf(track.title, track.artist, track.album);
    track.variantId = VariantOfAudioFormat(reading.audioFormat);
    track.atmos = IsAtmosFormat(reading.audioFormat);
    track.lossless = IsLosslessFormat(reading.audioFormat);
    if (reading.queueItemId != 0) track.queueItemId = std::to_string(reading.queueItemId);
    track.artwork = ArtworkAddress(reading.artwork);
    track.artworkColor = ArtworkColorHex(reading.artwork);
    snapshot.track = std::move(track);
    snapshot.lyricsAvailable = reading.lyricsAvailable;
    return snapshot;
}

int64_t PositionClock::Next(const std::string& track, bool playing, int64_t reportedMs, int64_t durationMs,
                            int64_t nowMs) {
    if (track != m_track) {
        *this = PositionClock();
        m_track = track;
    }
    if (!playing && m_playing) {
        m_baseMs = Position(nowMs);  // paused: the clock keeps what it counted
        m_learntMs = m_learntMs >= 0 ? nowMs : -1;
    }
    if (playing && !m_playing && m_learntMs >= 0) m_learntMs = nowMs;  // resumed: the pause is not counted
    m_playing = playing;
    if (reportedMs > 0 && reportedMs != m_lastReported) {
        m_lastReported = reportedMs;
        m_baseMs = reportedMs;
        m_learntMs = nowMs;
        m_fromApple = true;
    }
    int64_t position = Position(nowMs);
    return durationMs > 0 ? std::min(position, durationMs) : position;
}

void PositionClock::Seeked(int64_t positionMs, int64_t nowMs) {
    m_baseMs = std::max<int64_t>(0, positionMs);
    m_learntMs = nowMs;
    m_fromApple = false;
}

bool PositionClock::Moving(int64_t nowMs) const {
    if (!m_playing || m_learntMs < 0) return false;
    return !m_fromApple || nowMs - m_learntMs <= kPositionStallMs;
}

int64_t PositionClock::Position(int64_t nowMs) const {
    return ExtrapolatePosition(m_baseMs, m_learntMs, nowMs, Moving(nowMs));
}

PlayerSnapshot SnapshotFromSmtc(const SmtcReading& reading) {
    PlayerSnapshot snapshot;
    snapshot.source = "smtc";
    snapshot.playing = reading.status == kSmtcPlaying;
    if (reading.title.empty() && reading.artist.empty()) return snapshot;
    // Apple Music 1.7's stopped entry ("Not Playing", translated): a title alone, no artist, no length, not playing.
    const bool idle = !snapshot.playing && reading.artist.empty() && reading.album.empty() &&
                      reading.endTicks <= reading.startTicks;
    if (idle) return snapshot;

    int64_t length = std::max<int64_t>(0, reading.endTicks - reading.startTicks);
    int64_t position = std::max<int64_t>(0, reading.positionTicks - reading.startTicks);
    // A song still at 0 a stall after the timeline was set is not heard yet (loading, or a stream that never comes).
    // Extrapolated in ticks, then rounded to ms once.
    const bool started = position > 0 || reading.now - reading.lastUpdated <= MsToTicks(kPositionStallMs);
    position = ExtrapolatePosition(position, reading.lastUpdated, reading.now,
                                   snapshot.playing && reading.lastUpdated > 0 && started);
    if (length > 0) position = std::min(position, length);
    snapshot.positionMs = TicksToMs(position);
    snapshot.durationMs = TicksToMs(length);

    TrackInfo track;
    track.title = reading.title;
    track.artist = reading.artist;
    track.album = reading.album;
    // Apple Music writes "Artist — Album" in the session's artist and leaves the album empty.
    if (track.album.empty()) std::tie(track.artist, track.album) = SplitArtistAlbum(reading.artist);
    track.id = TrackIdOf(track.title, track.artist, track.album);
    snapshot.track = std::move(track);
    return snapshot;
}

std::optional<int64_t> SmtcLengthMsOf(const SmtcReading& smtc, const std::string& title) {
    if (smtc.title.empty() || smtc.title != title || smtc.endTicks <= smtc.startTicks) return std::nullopt;
    return TicksToMs(smtc.endTicks - smtc.startTicks);
}

void FillArtistAlbumFromSmtc(TrackInfo& track, const SmtcReading& smtc) {
    if (smtc.title != track.title || !track.artist.empty()) return;
    auto [artist, album] = SplitArtistAlbum(smtc.artist);
    track.artist = artist;
    if (track.album.empty()) track.album = album.empty() ? smtc.album : album;
}

CommandPlan PlanCommand(PlayerCommand command, int64_t seekMs, const PlayerSnapshot& known,
                        const RouteAvailability& available) {
    CommandPlan plan;
    const bool stateKnown = !known.source.empty();
    using Key = CommandStep::Key;

    auto add = [&](CommandRoute route, Key key = Key::None) { plan.steps.push_back(CommandStep{route, key}); };
    switch (command) {
        case PlayerCommand::Play:
        case PlayerCommand::Pause: {
            const bool wantPlaying = command == PlayerCommand::Play;
            if (stateKnown && known.playing == wantPlaying) {
                plan.alreadyDone = true;
                return plan;
            }
            if (available.apple) add(CommandRoute::Apple);
            if (available.smtc) add(CommandRoute::Smtc);
            // The toggle key would invert an unknown state, so it waits for a known one.
            if (available.mediaKeys && stateKnown) add(CommandRoute::MediaKey, Key::PlayPause);
            return plan;
        }
        case PlayerCommand::Next:
        case PlayerCommand::Previous:
            if (available.apple) add(CommandRoute::Apple);
            if (available.smtc) add(CommandRoute::Smtc);
            if (available.mediaKeys) {
                add(CommandRoute::MediaKey, command == PlayerCommand::Next ? Key::Next : Key::Previous);
            }
            return plan;
        case PlayerCommand::Seek:
            // Apple's IPlayer.SetPositionAsync first: Apple Music 1.1792 answers the SMTC one but does not move. There
            // is no media key for it.
            plan.seekMs = std::max<int64_t>(0, seekMs);
            if (known.durationMs > 0) plan.seekMs = std::min(plan.seekMs, known.durationMs);
            if (available.apple) add(CommandRoute::Apple);
            if (available.smtc) add(CommandRoute::Smtc);
            return plan;
    }
    return plan;
}

const char* CommandName(PlayerCommand command) {
    switch (command) {
        case PlayerCommand::Play: return "play";
        case PlayerCommand::Pause: return "pause";
        case PlayerCommand::Next: return "next";
        case PlayerCommand::Previous: return "previous";
        case PlayerCommand::Seek: return "seek";
    }
    return "?";
}

const char* RouteName(CommandRoute route) {
    switch (route) {
        case CommandRoute::Apple: return "Apple";
        case CommandRoute::Smtc: return "SMTC";
        case CommandRoute::MediaKey: return "media key";
    }
    return "?";
}

}  // namespace ampify
