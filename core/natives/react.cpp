// Natives of the module react (core/prelude/react.cpp): callback ids for the trees that AMK.ReactDOM renders, and
// the named popups that a re-render updates in place. Panels and pages go through AMK.ui, with its own checks.

#include <algorithm>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"
#include "../ui_model.hpp"

namespace amk::natives {

namespace {

constexpr size_t kMaxPopupName = 64;

// Lowercase letters, digits and dashes.
bool ValidPopupName(const std::string& name) {
    if (name.empty() || name.size() > kMaxPopupName) return false;
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

struct ReactNatives : ScriptBridge {
    // native.reactHandlerId(): a callback id for onClick or onChange, unique among the ids of the interface.
    AMK_NATIVE(HandlerId) {
        return JS_NewInt32(ctx, HostOf(Owner(ctx)).Ui().ReserveId());
    }

    // native.reactPopup(name, treeJson): opens the popup, or replaces its tree when it is already open.
    AMK_NATIVE(Popup) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.popup", &failure)) return failure;
        std::string name = String(ctx, argc, argv, 0).value_or("");
        if (!ValidPopupName(name)) return Throw(ctx, "TypeError", "nom de popup invalide : " + name);
        std::string error;
        std::optional<Json> tree = Json::Parse(String(ctx, argc, argv, 1).value_or(""), &error);
        if (!tree || !ValidateTree(*tree, &error)) return Throw(ctx, "TypeError", "interface refusée : " + error);
        HostOf(ext).Ui().SetPopup(ext->id(), name, std::move(*tree));
        return JS_UNDEFINED;
    }

    // native.reactClosePopup(name): closes a popup opened by reactPopup.
    AMK_NATIVE(ClosePopup) {
        ScriptExtension* ext = Owner(ctx);
        HostOf(ext).Ui().ClosePopup(ext->id(), String(ctx, argc, argv, 0).value_or(""));
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineReact(api::NativeScope& scope) {
    scope.Function("reactHandlerId", ReactNatives::HandlerId, 0);
    scope.Function("reactPopup", ReactNatives::Popup, 2);
    scope.Function("reactClosePopup", ReactNatives::ClosePopup, 1);
    // The page where an app's render() goes: "/<id>" for an app, empty otherwise.
    const Manifest& manifest = scope.extension().manifest();
    scope.String("reactAppPage", manifest.kind == ElementKind::App ? "/" + manifest.id : std::string());
}

}  // namespace amk::natives
