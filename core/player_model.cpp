#include "player_model.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "amp_abi.hpp"
#include "md5.hpp"

namespace ampify {

namespace {

constexpr int64_t kTicksPerMs = 10'000;
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
        case AmpTimeUnit::Ticks100ns: return raw / kTicksPerMs;
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
    track.id = TrackIdOf(reading.title, reading.artist, reading.album);
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

PlayerSnapshot SnapshotFromSmtc(const SmtcReading& reading) {
    PlayerSnapshot snapshot;
    snapshot.source = "smtc";
    snapshot.playing = reading.status == kSmtcPlaying;
    if (reading.title.empty() && reading.artist.empty()) return snapshot;

    int64_t length = std::max<int64_t>(0, reading.endTicks - reading.startTicks);
    int64_t position = std::max<int64_t>(0, reading.positionTicks - reading.startTicks);
    if (snapshot.playing && reading.lastUpdated > 0 && reading.now > reading.lastUpdated) {
        position += reading.now - reading.lastUpdated;
    }
    if (length > 0) position = std::min(position, length);
    snapshot.positionMs = position / kTicksPerMs;
    snapshot.durationMs = length / kTicksPerMs;

    TrackInfo track;
    track.title = reading.title;
    track.artist = reading.artist;
    track.album = reading.album;
    track.id = TrackIdOf(reading.title, reading.artist, reading.album);
    snapshot.track = std::move(track);
    return snapshot;
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
            // IPlayer.SetPositionAsync exists, but the winmd list does not give the type of its position: only SMTC
            // seeks, with a documented TimeSpan. There is no media key for it.
            plan.seekMs = std::max<int64_t>(0, seekMs);
            if (known.durationMs > 0) plan.seekMs = std::min(plan.seekMs, known.durationMs);
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
