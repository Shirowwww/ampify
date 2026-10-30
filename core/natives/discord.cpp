// Natives of the module discord (core/prelude/discord.cpp). Permission: ipc.discord. Setting: discord.enabled.

#include "discord.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"
#include "service_gate.hpp"

namespace amk::natives {

namespace {

struct DiscordNatives : ScriptBridge {
    // native.discordSet(activityJson): true when the host took the activity.
    AMK_NATIVE(Set) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ipc.discord", &failure)) return failure;
        std::optional<Json> value = Json::Parse(String(ctx, argc, argv, 0).value_or(""));
        if (!value || !value->AsObject()) return Throw(ctx, "TypeError", "discord.setActivity attend un objet");
        const Json* clientId = value->Find("clientId");
        if (!clientId || !clientId->AsString() || !discord::IsValidClientId(*clientId->AsString())) {
            return Throw(ctx, "TypeError",
                         "discord.setActivity attend clientId, l'identifiant de l'application Discord (17 à 20 chiffres)");
        }
        std::string error;
        std::optional<discord::Activity> activity = discord::ActivityFromJson(*value, &error);
        if (!activity) return Throw(ctx, "TypeError", error);

        if (!ServiceGate::Open(ext, "discord.enabled", "Discord")) return JS_FALSE;
        DiscordHost* discordHost = HostOf(ext).Capability<DiscordHost>();
        if (!discordHost) return JS_FALSE;
        return JS_NewBool(ctx, discordHost->SetActivity(ext->id(), *clientId->AsString(), *activity));
    }

    AMK_NATIVE(Clear) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ipc.discord", &failure)) return failure;
        if (DiscordHost* discordHost = HostOf(ext).Capability<DiscordHost>()) discordHost->ClearActivity(ext->id());
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineDiscord(api::NativeScope& scope) {
    scope.Function("discordSet", DiscordNatives::Set, 1);
    scope.Function("discordClear", DiscordNatives::Clear, 0);
}

// The presence of an element goes away with it.
void StopDiscord(ScriptExtension& extension) {
    if (!extension.manifest().HasPermission("ipc.discord")) return;
    if (DiscordHost* host = ScriptBridge::HostOf(&extension).Capability<DiscordHost>()) host->ClearActivity(extension.id());
}

}  // namespace amk::natives
