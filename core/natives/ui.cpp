// Natives of the module ui (core/prelude/ui.cpp). The kit keeps what an element asks for, and the host draws it
// (M8). Each call needs the permission of its part of the interface.

#include <algorithm>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"
#include "../ui_model.hpp"

namespace amk::natives {

namespace {

// Sizes of the interface, in characters, and the names that panels, pages and routes may use.
constexpr size_t kMaxUiLabel = 60;
constexpr size_t kMaxUiIcon = 8000;
constexpr size_t kMaxUiTitle = 120;
constexpr size_t kMaxUiName = 64;

// Lowercase letters, digits, dashes and slashes, so that a name is never a path trick.
bool ValidUiName(const std::string& name) {
    if (name.empty() || name.size() > kMaxUiName) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '/';
    });
}

struct UiNatives : ScriptBridge {
    // Parses the tree that the element built with JSON.stringify, and checks its shape. On failure, `failure`
    // holds the exception to return.
    static std::optional<Json> TreeOf(JSContext* ctx, const std::string& text, JSValue* failure) {
        std::string error;
        std::optional<Json> tree = Json::Parse(text, &error);
        if (tree && ValidateTree(*tree, &error)) return tree;
        *failure = Throw(ctx, "TypeError", "interface refusée : " + error);
        return std::nullopt;
    }

    AMK_NATIVE(UiControl) {
        ScriptExtension* ext = Owner(ctx);
        std::string placement = String(ctx, argc, argv, 0).value_or("");
        if (!IsPlacement(placement)) return Throw(ctx, "TypeError", "emplacement inconnu : " + placement);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, std::string(PlacementPermission(placement)), &failure)) return failure;
        std::string label = String(ctx, argc, argv, 1).value_or("").substr(0, kMaxUiLabel);
        std::string icon = String(ctx, argc, argv, 2).value_or("").substr(0, kMaxUiIcon);
        return JS_NewInt32(ctx, HostOf(ext).Ui().AddControl(ext->id(), placement, label, icon));
    }

    AMK_NATIVE(UiPanel) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.panel", &failure)) return failure;
        std::string panelId = String(ctx, argc, argv, 0).value_or("");
        if (!ValidUiName(panelId)) return Throw(ctx, "TypeError", "nom de panneau invalide : " + panelId);
        std::optional<Json> tree = TreeOf(ctx, String(ctx, argc, argv, 1).value_or(""), &failure);
        if (!tree) return failure;
        HostOf(ext).Ui().SetPanel(ext->id(), panelId, std::move(*tree));
        return JS_UNDEFINED;
    }

    AMK_NATIVE(UiPopup) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.popup", &failure)) return failure;
        std::optional<Json> tree = TreeOf(ctx, String(ctx, argc, argv, 0).value_or(""), &failure);
        if (!tree) return failure;
        HostOf(ext).Ui().OpenPopup(ext->id(), std::move(*tree));
        return JS_UNDEFINED;
    }

    AMK_NATIVE(UiPage) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.page", &failure)) return failure;
        if (ext->manifest().kind != ElementKind::App) {
            return Throw(ctx, "PermissionDenied", "les pages sont réservées aux apps (kind app dans manifest.json)");
        }
        std::string path = String(ctx, argc, argv, 0).value_or("");
        if (path.empty() || path[0] != '/' || !ValidUiName(path)) {
            return Throw(ctx, "TypeError", "chemin de page invalide : " + path);
        }
        std::optional<Json> tree = TreeOf(ctx, String(ctx, argc, argv, 1).value_or(""), &failure);
        if (!tree) return failure;
        HostOf(ext).Ui().SetPage(ext->id(), path, std::move(*tree));
        return JS_UNDEFINED;
    }

    AMK_NATIVE(UiNavigate) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.page", &failure)) return failure;
        std::string path = String(ctx, argc, argv, 0).value_or("");
        if (path.empty() || path[0] != '/' || !ValidUiName(path)) {
            return Throw(ctx, "TypeError", "chemin de page invalide : " + path);
        }
        HostOf(ext).Ui().Navigate(ext->id(), path);
        return JS_UNDEFINED;
    }

    AMK_NATIVE(UiTitle) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.window", &failure)) return failure;
        HostOf(ext).Ui().SetTitle(ext->id(), String(ctx, argc, argv, 0).value_or("").substr(0, kMaxUiTitle));
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineUi(api::NativeScope& scope) {
    scope.Function("uiControl", UiNatives::UiControl, 3);
    scope.Function("uiPanel", UiNatives::UiPanel, 2);
    scope.Function("uiPopup", UiNatives::UiPopup, 1);
    scope.Function("uiPage", UiNatives::UiPage, 2);
    scope.Function("uiNavigate", UiNatives::UiNavigate, 1);
    scope.Function("uiTitle", UiNatives::UiTitle, 1);
}

// Removes what the element added to the interface when it stops.
void StopUi(ScriptExtension& extension) { ScriptBridge::HostOf(&extension).Ui().RemoveElement(extension.id()); }

}  // namespace amk::natives
