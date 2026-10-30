// Natives of the module player (core/prelude/player.cpp). Permissions: player.read and player.control.

#include "player.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"

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
        members.emplace_back("track", Json(std::move(track)));
    }
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
        else return Throw(ctx, "TypeError", "commande de lecteur inconnue : " + name);
        return JS_NewBool(ctx, HostOf(ext).Control(command, seek));
    }
};

}  // namespace

void DefinePlayer(api::NativeScope& scope) {
    scope.Function("playerState", PlayerNatives::PlayerState, 0);
    scope.Function("playerCommand", PlayerNatives::PlayerControl, 2);
}

}  // namespace amk::natives
