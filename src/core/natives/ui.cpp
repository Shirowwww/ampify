// Natives of the module ui (src/core/prelude/ui.cpp). The kit keeps what an element asks for, and the host draws it
// (M8). Each call needs the permission of its part of the interface.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../i18n.hpp"
#include "../json.hpp"
#include "../ui_model.hpp"

namespace ampify::natives {

namespace {

// Sizes of the interface, in characters, and the names that panels, pages and routes may use.
constexpr size_t kMaxUiLabel = 60;
constexpr size_t kMaxUiIcon = 8000;
constexpr size_t kMaxUiTitle = 120;
constexpr size_t kMaxUiName = 64;
constexpr double kMaxSession = 9007199254740991.0;  // the largest whole number a JavaScript number holds exactly

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
        *failure = Throw(ctx, "TypeError", "interface refused: " + error);
        return std::nullopt;
    }

    AMPIFY_NATIVE(UiControl) {
        ScriptExtension* ext = Owner(ctx);
        std::string placement = String(ctx, argc, argv, 0).value_or("");
        if (!IsPlacement(placement)) return Throw(ctx, "TypeError", "unknown placement: " + placement);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, std::string(PlacementPermission(placement)), &failure)) return failure;
        std::string label = String(ctx, argc, argv, 1).value_or("").substr(0, kMaxUiLabel);
        std::string icon = String(ctx, argc, argv, 2).value_or("").substr(0, kMaxUiIcon);
        // Argument 3, false for a text that only shows something (no onClick); older preludes leave it out.
        bool clickable = argc < 4 || JS_ToBool(ctx, argv[3]) != 0;
        // Argument 4, the submenu an entry of a menu belongs to (0 or left out: the menu itself).
        int parent = argc >= 5 ? static_cast<int>(Number(ctx, argc, argv, 4)) : 0;
        int id = HostOf(ext).Ui().AddControl(ext->id(), placement, label, icon, clickable, parent);
        if (id == 0 && parent != 0) return Throw(ctx, "RangeError", "ui: the submenu does not exist or is not of this menu");
        return JS_NewInt32(ctx, id);
    }

    // No permission of its own: the element can only relabel what it added, which needed the permission already.
    AMPIFY_NATIVE(UiControlLabel) {
        ScriptExtension* ext = Owner(ctx);
        int id = static_cast<int>(Number(ctx, argc, argv, 0));
        std::string label = String(ctx, argc, argv, 1).value_or("").substr(0, kMaxUiLabel);
        return JS_NewBool(ctx, HostOf(ext).Ui().SetControlLabel(ext->id(), id, label));
    }

    // Same rule as the label: no permission of its own, the element can only touch what it added.
    AMPIFY_NATIVE(UiControlActive) {
        ScriptExtension* ext = Owner(ctx);
        int id = static_cast<int>(Number(ctx, argc, argv, 0));
        bool active = argc >= 2 && JS_ToBool(ctx, argv[1]) != 0;
        return JS_NewBool(ctx, HostOf(ext).Ui().SetControlActive(ext->id(), id, active));
    }

    AMPIFY_NATIVE(UiPanel) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.panel", &failure)) return failure;
        std::string panelId = String(ctx, argc, argv, 0).value_or("");
        if (!ValidUiName(panelId)) return Throw(ctx, "TypeError", "invalid panel name: " + panelId);
        std::optional<Json> tree = TreeOf(ctx, String(ctx, argc, argv, 1).value_or(""), &failure);
        if (!tree) return failure;
        HostOf(ext).Ui().SetPanel(ext->id(), panelId, std::move(*tree));
        return JS_UNDEFINED;
    }

    AMPIFY_NATIVE(UiPopup) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.popup", &failure)) return failure;
        std::optional<Json> tree = TreeOf(ctx, String(ctx, argc, argv, 0).value_or(""), &failure);
        if (!tree) return failure;
        HostOf(ext).Ui().OpenPopup(ext->id(), std::move(*tree));
        return JS_UNDEFINED;
    }

    // native.uiFullscreen(treeJson, session): opens the fullscreen surface (session 0) or updates it. Returns the
    // session the tree belongs to, 0 when that session was closed meanwhile (UiModel::SetFullscreen).
    AMPIFY_NATIVE(UiFullscreen) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.fullscreen", &failure)) return failure;
        std::optional<Json> tree = TreeOf(ctx, String(ctx, argc, argv, 0).value_or(""), &failure);
        if (!tree) return failure;
        std::uint64_t session = SessionOf(ctx, argc, argv, 1);
        return JS_NewFloat64(ctx, static_cast<double>(HostOf(ext).Ui().SetFullscreen(ext->id(), std::move(*tree), session)));
    }

    // native.uiApplePlayer(): opens Apple Music's own full-screen player, as a window over the main one.
    AMPIFY_NATIVE(UiApplePlayer) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.fullscreen", &failure)) return failure;
        HostOf(ext).Ui().RequestApplePlayer();
        return JS_UNDEFINED;
    }

    // native.uiFullscreenClose(session): closes the element's own fullscreen surface, so it needs no permission.
    AMPIFY_NATIVE(UiFullscreenClose) {
        ScriptExtension* ext = Owner(ctx);
        HostOf(ext).Ui().CloseFullscreen(ext->id(), SessionOf(ctx, argc, argv, 0));
        return JS_UNDEFINED;
    }

    static std::uint64_t SessionOf(JSContext* ctx, int argc, JSValueConst* argv, int index) {
        double session = Number(ctx, argc, argv, index);
        if (!std::isfinite(session) || session < 1 || session > kMaxSession) return 0;
        return static_cast<std::uint64_t>(session);
    }

    AMPIFY_NATIVE(UiPage) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.page", &failure)) return failure;
        if (ext->manifest().kind != ElementKind::App) {
            return Throw(ctx, "PermissionDenied", "pages are reserved for apps (kind app in manifest.json)");
        }
        std::string path = String(ctx, argc, argv, 0).value_or("");
        if (path.empty() || path[0] != '/' || !ValidUiName(path)) {
            return Throw(ctx, "TypeError", "invalid page path: " + path);
        }
        std::optional<Json> tree = TreeOf(ctx, String(ctx, argc, argv, 1).value_or(""), &failure);
        if (!tree) return failure;
        HostOf(ext).Ui().SetPage(ext->id(), path, std::move(*tree));
        return JS_UNDEFINED;
    }

    AMPIFY_NATIVE(UiNavigate) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.page", &failure)) return failure;
        std::string path = String(ctx, argc, argv, 0).value_or("");
        if (path.empty() || path[0] != '/' || !ValidUiName(path)) {
            return Throw(ctx, "TypeError", "invalid page path: " + path);
        }
        HostOf(ext).Ui().Navigate(ext->id(), path);
        return JS_UNDEFINED;
    }

    AMPIFY_NATIVE(UiTitle) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.window", &failure)) return failure;
        HostOf(ext).Ui().SetTitle(ext->id(), String(ctx, argc, argv, 0).value_or("").substr(0, kMaxUiTitle));
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineUi(api::NativeScope& scope) {
    scope.Function("uiControl", UiNatives::UiControl, 5);
    scope.Function("uiControlLabel", UiNatives::UiControlLabel, 2);
    scope.Function("uiControlActive", UiNatives::UiControlActive, 2);
    scope.Function("uiPanel", UiNatives::UiPanel, 2);
    scope.Function("uiPopup", UiNatives::UiPopup, 1);
    scope.Function("uiFullscreen", UiNatives::UiFullscreen, 2);
    scope.Function("uiFullscreenClose", UiNatives::UiFullscreenClose, 1);
    scope.Function("uiApplePlayer", UiNatives::UiApplePlayer, 0);
    scope.Function("uiPage", UiNatives::UiPage, 2);
    scope.Function("uiNavigate", UiNatives::UiNavigate, 1);
    scope.Function("uiTitle", UiNatives::UiTitle, 1);
}

// The panel frame, page header and popup of an element show its name, in the display language when the manifest
// translates it, until it sets a title of its own.
void StartUi(ScriptExtension& extension) {
    const Manifest& manifest = extension.manifest();
    auto translated = manifest.translations.find(i18n::Language());
    const std::string& name = translated != manifest.translations.end() && !translated->second.name.empty()
                                  ? translated->second.name
                                  : manifest.name;
    ScriptBridge::HostOf(&extension).Ui().SetTitle(extension.id(), name.substr(0, kMaxUiTitle));
}

// Removes what the element added to the interface when it stops.
void StopUi(ScriptExtension& extension) { ScriptBridge::HostOf(&extension).Ui().RemoveElement(extension.id()); }

}  // namespace ampify::natives
