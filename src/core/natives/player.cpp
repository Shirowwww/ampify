// Natives of the module player (src/core/prelude/player.cpp). Permissions: player.read and player.control. The queue
// (Ampify.player.queue) is the optional QueueHost capability of src/core/player_queue.hpp.

#include "player.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../form_data.hpp"
#include "../image_host.hpp"
#include "../lyrics_host.hpp"
#include "../player_modes.hpp"
#include "../player_queue.hpp"

namespace ampify::natives {

namespace {

// A cover is a few hundred KB; past this, it is not worth carrying through the script.
constexpr size_t kMaxArtworkBytes = 4 * 1024 * 1024;

}  // namespace

Json PlayerJson(const PlayerSnapshot& snapshot) {
    Json::Object members;
    members.emplace_back("playing", Json(snapshot.playing));
    members.emplace_back("positionMs", Json(static_cast<double>(snapshot.positionMs)));
    members.emplace_back("durationMs", Json(static_cast<double>(snapshot.durationMs)));
    if (!snapshot.track) {
        members.emplace_back("track", Json());
    } else {
        const TrackInfo& t = *snapshot.track;
        Json::Object track;
        track.emplace_back("id", Json(t.id));
        track.emplace_back("title", Json(t.title));
        track.emplace_back("artist", Json(t.artist));
        track.emplace_back("album", Json(t.album));
        track.emplace_back("variantId", Json(t.variantId));
        track.emplace_back("atmos", Json(t.atmos));
        track.emplace_back("lossless", Json(t.lossless));
        track.emplace_back("queueItemId", Json(t.queueItemId));
        track.emplace_back("artwork", Json(t.artwork));
        track.emplace_back("artworkColor", Json(t.artworkColor));
        members.emplace_back("track", Json(std::move(track)));
    }
    members.emplace_back("source", Json(snapshot.source));
    return Json(std::move(members));
}

namespace {

struct PlayerNatives : ScriptBridge {
    AMPIFY_NATIVE(PlayerState) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        std::string json = PlayerJson(HostOf(ext).Player()).Dump();
        return JS_NewStringLen(ctx, json.data(), json.size());
    }

    // native.playerArtwork(): starts reading the cover off the JavaScript thread and returns the request's id. The
    // answer comes back as dispatch.http: status 200 and {"type","base64"}, or 204 without a cover, or 0 and why.
    AMPIFY_NATIVE(PlayerArtwork) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        ArtworkHost* artworks = HostOf(ext).Capability<ArtworkHost>();
        uint32_t id = SubmitHttp(ext, [artworks]() -> HttpResponse {
            if (!artworks) return {0, "covers are not read by this host"};
            std::optional<ArtworkHost::File> file = artworks->CurrentArtwork();
            if (!file || file->bytes.empty()) return {204, ""};
            if (file->bytes.size() > kMaxArtworkBytes) return {0, "the cover is too large"};
            Json::Object answer;
            answer.emplace_back("type", Json(file->contentType));
            answer.emplace_back("base64", Json(Base64Encode(file->bytes)));
            return {200, Json(std::move(answer)).Dump()};
        });
        if (id == 0) {
            return Throw(ctx, "RangeError",
                         "too many requests in flight (" + std::to_string(kMaxHttpInFlight) + " at most)");
        }
        return JS_NewUint32(ctx, id);
    }

    AMPIFY_NATIVE(PlayerControl) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.control", &failure)) return failure;
        std::string name = String(ctx, argc, argv, 0).value_or("");
        int64_t seek = static_cast<int64_t>(Number(ctx, argc, argv, 1));
        PlayerCommand command;
        if (name == "play") command = PlayerCommand::Play;
        else if (name == "pause") command = PlayerCommand::Pause;
        else if (name == "next") command = PlayerCommand::Next;
        else if (name == "previous") command = PlayerCommand::Previous;
        else if (name == "seek") command = PlayerCommand::Seek;
        // Toggle needs the state, but only player.control: the element does not read it.
        else if (name == "toggle") command = HostOf(ext).Player().playing ? PlayerCommand::Pause : PlayerCommand::Play;
        else return Throw(ctx, "TypeError", "unknown player command: " + name);
        return JS_NewBool(ctx, HostOf(ext).Control(command, seek));
    }

    // native.playerModes(): {"shuffle":true|false|null,"repeat":"off"|"all"|"one"|null} as text, null while unknown.
    AMPIFY_NATIVE(PlayerModes) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        ampify::PlayerModesHost* host = HostOf(ext).Capability<ampify::PlayerModesHost>();
        ampify::PlayerModes modes = host ? host->Modes() : ampify::PlayerModes{};
        Json::Object members;
        members.emplace_back("shuffle", modes.shuffle ? Json(*modes.shuffle) : Json());
        members.emplace_back("repeat", IsRepeatMode(modes.repeat) ? Json(modes.repeat) : Json());
        std::string json = Json(std::move(members)).Dump();
        return JS_NewStringLen(ctx, json.data(), json.size());
    }

    // native.playerSetShuffle(on), native.playerSetRepeat(mode) and native.playerSetVolume(volume): true when the
    // player is in the asked state. A host without the service answers false.
    AMPIFY_NATIVE(PlayerSetShuffle) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.control", &failure)) return failure;
        ampify::PlayerModesHost* host = HostOf(ext).Capability<ampify::PlayerModesHost>();
        return JS_NewBool(ctx, host && host->SetShuffle(Number(ctx, argc, argv, 0) != 0));
    }

    AMPIFY_NATIVE(PlayerSetRepeat) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.control", &failure)) return failure;
        std::string mode = String(ctx, argc, argv, 0).value_or("");
        if (!IsRepeatMode(mode)) return Throw(ctx, "RangeError", "setRepeat expects \"off\", \"all\" or \"one\"");
        ampify::PlayerModesHost* host = HostOf(ext).Capability<ampify::PlayerModesHost>();
        return JS_NewBool(ctx, host && host->SetRepeat(mode));
    }

    AMPIFY_NATIVE(PlayerSetVolume) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.control", &failure)) return failure;
        double volume = Number(ctx, argc, argv, 0);
        if (!(volume >= 0.0 && volume <= 1.0)) return Throw(ctx, "RangeError", "setVolume expects a number from 0 to 1");
        ampify::PlayerVolumeHost* host = HostOf(ext).Capability<ampify::PlayerVolumeHost>();
        return JS_NewBool(ctx, host && host->SetVolume(volume));
    }

    // native.playerLyrics(): the LyricsJson of src/core/lyrics.hpp, as text ("null" without lyrics or without the service).
    AMPIFY_NATIVE(PlayerLyrics) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        LyricsHost* source = HostOf(ext).Capability<LyricsHost>();
        std::optional<RawLyrics> raw = source ? source->Lyrics() : std::nullopt;
        std::string json = LyricsJson(raw ? LyricsFromRaw(*raw) : std::nullopt).Dump();
        return JS_NewStringLen(ctx, json.data(), json.size());
    }

    // native.queueList(): the QueueJson of src/core/player_queue.hpp, as text.
    AMPIFY_NATIVE(QueueList) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        QueueHost* queue = HostOf(ext).Capability<QueueHost>();
        std::string json = QueueJson(queue ? queue->Queue() : QueueSnapshot{}, queue != nullptr).Dump();
        return JS_NewStringLen(ctx, json.data(), json.size());
    }

    // native.queueAdd(trackId, position) and native.queueRemove(itemId): true when the host changed the queue. A
    // refusal goes to the journal with its reason, so that the author sees why.
    AMPIFY_NATIVE(QueueAdd) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.control", &failure)) return failure;
        std::string id = String(ctx, argc, argv, 0).value_or("");
        if (!IsValidQueueId(id)) return Throw(ctx, "TypeError", "queue.add expects a track id");
        QueuePosition position;
        if (!ParseQueuePosition(String(ctx, argc, argv, 1).value_or(""), position)) {
            return Throw(ctx, "RangeError", "queue.add: position \"next\" or \"last\"");
        }
        return Answer(ctx, ext, "queue.add", [&](QueueHost& queue) { return queue.AddToQueue(id, position); });
    }

    AMPIFY_NATIVE(QueueRemove) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.control", &failure)) return failure;
        std::string id = String(ctx, argc, argv, 0).value_or("");
        if (!IsValidQueueId(id)) {
            return Throw(ctx, "TypeError", "queue.remove expects the id of a queue entry");
        }
        return Answer(ctx, ext, "queue.remove", [&](QueueHost& queue) { return queue.RemoveFromQueue(id); });
    }

    template <class Call>
    static JSValue Answer(JSContext* ctx, ScriptExtension* ext, const char* what, Call call) {
        QueueHost* queue = HostOf(ext).Capability<QueueHost>();
        QueueResult result = queue ? call(*queue) : QueueResult{false, "no queue in this process"};
        if (!result.ok) JournalOf(ext).Write("[" + ext->id() + "] " + what + " refused: " + result.reason);
        return JS_NewBool(ctx, result.ok);
    }
};

}  // namespace

void DefinePlayer(api::NativeScope& scope) {
    scope.Function("playerState", PlayerNatives::PlayerState, 0);
    scope.Function("playerCommand", PlayerNatives::PlayerControl, 2);
    scope.Function("playerModes", PlayerNatives::PlayerModes, 0);
    scope.Function("playerSetShuffle", PlayerNatives::PlayerSetShuffle, 1);
    scope.Function("playerSetRepeat", PlayerNatives::PlayerSetRepeat, 1);
    scope.Function("playerSetVolume", PlayerNatives::PlayerSetVolume, 1);
    scope.Function("playerLyrics", PlayerNatives::PlayerLyrics, 0);
    scope.Function("playerArtwork", PlayerNatives::PlayerArtwork, 0);
    scope.Function("queueList", PlayerNatives::QueueList, 0);
    scope.Function("queueAdd", PlayerNatives::QueueAdd, 2);
    scope.Function("queueRemove", PlayerNatives::QueueRemove, 1);
}

}  // namespace ampify::natives
