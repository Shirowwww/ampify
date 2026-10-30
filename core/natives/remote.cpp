// Natives of the module remote (core/prelude/remote.cpp). No permission: the state goes only to the local remote
// control, which listens on 127.0.0.1 and asks for the token. Setting: remote.enabled.

#include "remote.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"
#include "service_flags.hpp"

namespace amk::natives {

namespace {

struct RemoteNatives : ScriptBridge {
    // native.remoteState(json): true when the remote control took it.
    AMK_NATIVE(State) {
        ScriptExtension* ext = Owner(ctx);
        std::string json = String(ctx, argc, argv, 0).value_or("");
        if (json.size() > kMaxRemoteStateBytes) return Throw(ctx, "RangeError", "remote.setState : 4 Ko au plus");
        std::optional<Json> value = Json::Parse(json);
        if (!value || !value->AsObject()) return Throw(ctx, "TypeError", "remote.setState attend un objet");
        if (!ServiceEnabled(HostOf(ext), "remote.enabled")) return JS_FALSE;
        RemoteHost* remote = HostOf(ext).Capability<RemoteHost>();
        return JS_NewBool(ctx, remote && remote->PublishRemoteState(ext->id(), value->Dump()));
    }
};

}  // namespace

void DefineRemote(api::NativeScope& scope) {
    scope.Function("remoteState", RemoteNatives::State, 1);
}

void StopRemote(ScriptExtension& extension) {
    if (RemoteHost* remote = ScriptBridge::HostOf(&extension).Capability<RemoteHost>()) {
        remote->RemoveRemoteState(extension.id());
    }
}

}  // namespace amk::natives
