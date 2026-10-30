// Natives of the module player (core/prelude/player.cpp). Permissions: player.read and player.control. The queue
// (AMK.player.queue) is the optional QueueHost capability of core/player_queue.hpp.

#include "player.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../player_queue.hpp"

namespace amk::natives {

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
        members.emplace_back("track", Json(std::move(track)));
    }
    members.emplace_back("source", Json(snapshot.source));
    return Json(std::move(members));
}

namespace {

struct PlayerNatives : ScriptBridge {
    AMK_NATIVE(PlayerState) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        std::string json = PlayerJson(HostOf(ext).Player()).Dump();
        return JS_NewStringLen(ctx, json.data(), json.size());
    }

    AMK_NATIVE(PlayerControl) {
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
        else return Throw(ctx, "TypeError", "commande de lecteur inconnue : " + name);
        return JS_NewBool(ctx, HostOf(ext).Control(command, seek));
    }

    // native.queueList(): the QueueJson of core/player_queue.hpp, as text.
    AMK_NATIVE(QueueList) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        QueueHost* queue = HostOf(ext).Capability<QueueHost>();
        std::string json = QueueJson(queue ? queue->Queue() : QueueSnapshot{}, queue != nullptr).Dump();
        return JS_NewStringLen(ctx, json.data(), json.size());
    }

    // native.queueAdd(trackId, position) and native.queueRemove(itemId): true when the host changed the queue. A
    // refusal goes to the journal with its reason, so that the author sees why.
    AMK_NATIVE(QueueAdd) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.control", &failure)) return failure;
        std::string id = String(ctx, argc, argv, 0).value_or("");
        if (!IsValidQueueId(id)) return Throw(ctx, "TypeError", "queue.add attend un identifiant de morceau");
        QueuePosition position;
        if (!ParseQueuePosition(String(ctx, argc, argv, 1).value_or(""), position)) {
            return Throw(ctx, "RangeError", "queue.add : position \"next\" ou \"last\"");
        }
        return Answer(ctx, ext, "queue.add", [&](QueueHost& queue) { return queue.AddToQueue(id, position); });
    }

    AMK_NATIVE(QueueRemove) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.control", &failure)) return failure;
        std::string id = String(ctx, argc, argv, 0).value_or("");
        if (!IsValidQueueId(id)) {
            return Throw(ctx, "TypeError", "queue.remove attend l'identifiant d'une entrée de la file");
        }
        return Answer(ctx, ext, "queue.remove", [&](QueueHost& queue) { return queue.RemoveFromQueue(id); });
    }

    template <class Call>
    static JSValue Answer(JSContext* ctx, ScriptExtension* ext, const char* what, Call call) {
        QueueHost* queue = HostOf(ext).Capability<QueueHost>();
        QueueResult result = queue ? call(*queue) : QueueResult{false, "pas de file d'attente dans ce processus"};
        if (!result.ok) JournalOf(ext).Write("[" + ext->id() + "] " + what + " refusé : " + result.reason);
        return JS_NewBool(ctx, result.ok);
    }
};

}  // namespace

void DefinePlayer(api::NativeScope& scope) {
    scope.Function("playerState", PlayerNatives::PlayerState, 0);
    scope.Function("playerCommand", PlayerNatives::PlayerControl, 2);
    scope.Function("queueList", PlayerNatives::QueueList, 0);
    scope.Function("queueAdd", PlayerNatives::QueueAdd, 2);
    scope.Function("queueRemove", PlayerNatives::QueueRemove, 1);
}

}  // namespace amk::natives
