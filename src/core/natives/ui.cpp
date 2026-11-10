// Natives of the module ui (src/core/prelude/ui.cpp). The kit keeps what an element asks for, and the host draws it
// (M8). Each call needs the permission of its part of the interface.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../i18n.hpp"
#include "../json.hpp"
#include "../text_util.hpp"
#include "../ui_model.hpp"
#include "../ui_vocab.hpp"

namespace ampify::natives {

namespace {

// The names that panels, pages and routes may use. The sizes of the texts are in ui_vocab.hpp.
constexpr size_t kMaxUiName = 64;
constexpr double kMaxSession = 9007199254740991.0;  // the largest whole number a JavaScript number holds exactly

// Lowercase letters, digits, dashes and slashes, so that a name is never a path trick.
bool ValidUiName(const std::string& name) {
    if (name.empty() || name.size() > kMaxUiName) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '/';
    });
}

// The element's name, translated when its manifest has the display language.
std::string DefaultTitle(const ScriptExtension& extension) {
    const Manifest& manifest = extension.manifest();
    auto translated = manifest.translations.find(i18n::Language());
    const std::string& name = translated != manifest.translations.end() && !translated->second.name.empty()
                                  ? translated->second.name
                                  : manifest.name;
    return std::string(ClipUtf8(name, uivocab::kMaxTitle));
}

// The text, cut at a character boundary when it passes `limit` bytes.
std::string Clipped(const std::optional<std::string>& text, size_t limit) {
    return std::string(ClipUtf8(text.value_or(""), limit));
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
        std::string label = Clipped(String(ctx, argc, argv, 1), uivocab::kMaxControlLabel);
        std::string icon = Clipped(String(ctx, argc, argv, 2), uivocab::kMaxControlIcon);
        // Argument 3, false for a text that only shows something (no onClick).
        bool clickable = JS_ToBool(ctx, argv[3]) != 0;
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
        std::string label = Clipped(String(ctx, argc, argv, 1), uivocab::kMaxControlLabel);
        return JS_NewBool(ctx, HostOf(ext).Ui().SetControlLabel(ext->id(), id, label));
    }

    // Same rule as the label: no permission of its own, the element can only touch what it added.
    AMPIFY_NATIVE(UiControlActive) {
        ScriptExtension* ext = Owner(ctx);
        int id = static_cast<int>(Number(ctx, argc, argv, 0));
        bool active = argc >= 2 && JS_ToBool(ctx, argv[1]) != 0;
        return JS_NewBool(ctx, HostOf(ext).Ui().SetControlActive(ext->id(), id, active));
    }

    // Same rule again: the element can only grey or take out what it added.
    AMPIFY_NATIVE(UiControlDisabled) {
        ScriptExtension* ext = Owner(ctx);
        int id = static_cast<int>(Number(ctx, argc, argv, 0));
        bool disabled = argc >= 2 && JS_ToBool(ctx, argv[1]) != 0;
        return JS_NewBool(ctx, HostOf(ext).Ui().SetControlDisabled(ext->id(), id, disabled));
    }

    // Same rule: the element can only mark the progress line for a play bar button it added.
    AMPIFY_NATIVE(UiControlMark) {
        ScriptExtension* ext = Owner(ctx);
        int id = static_cast<int>(Number(ctx, argc, argv, 0));
        double from = Number(ctx, argc, argv, 1);
        double to = Number(ctx, argc, argv, 2);
        return JS_NewBool(ctx, HostOf(ext).Ui().SetControlMark(ext->id(), id, from, to));
    }

    // native.uiPlaybarReadout(totalMs): the time under the pointer on the progress line; 0 takes it away.
    AMPIFY_NATIVE(UiPlaybarReadout) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.playbar", &failure)) return failure;
        HostOf(ext).Ui().SetPlaybarReadout(ext->id(), Number(ctx, argc, argv, 0));
        return JS_UNDEFINED;
    }

    // native.uiPlaybarHover(listening): whether the element hears where the pointer is on the progress line.
    AMPIFY_NATIVE(UiPlaybarHover) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.playbar", &failure)) return failure;
        HostOf(ext).Ui().SetPlaybarHoverListener(ext->id(), argc >= 1 && JS_ToBool(ctx, argv[0]) != 0);
        return JS_UNDEFINED;
    }

    AMPIFY_NATIVE(UiControlRemove) {
        ScriptExtension* ext = Owner(ctx);
        int id = static_cast<int>(Number(ctx, argc, argv, 0));
        return JS_NewBool(ctx, HostOf(ext).Ui().RemoveControl(ext->id(), id));
    }

    // native.uiControlKinds(id, "song,album"): the kinds of item a context menu entry is for.
    AMPIFY_NATIVE(UiControlKinds) {
        ScriptExtension* ext = Owner(ctx);
        int id = static_cast<int>(Number(ctx, argc, argv, 0));
        std::vector<std::string> kinds;
        std::string list = String(ctx, argc, argv, 1).value_or("");
        for (size_t start = 0; start <= list.size();) {
            size_t comma = std::min(list.find(',', start), list.size());
            std::string kind = list.substr(start, comma - start);
            if (!IsMenuItemKind(kind)) return Throw(ctx, "RangeError", "menu.addItem: unknown kind of item: " + kind);
            kinds.push_back(kind);
            start = comma + 1;
        }
        return JS_NewBool(ctx, HostOf(ext).Ui().SetControlKinds(ext->id(), id, std::move(kinds)));
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

    AMPIFY_NATIVE(UiPanelRemove) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.panel", &failure)) return failure;
        std::string panelId = String(ctx, argc, argv, 0).value_or("");
        return JS_NewBool(ctx, HostOf(ext).Ui().RemovePanel(ext->id(), panelId));
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
        HostOf(ext).Ui().SetTitle(ext->id(), Clipped(String(ctx, argc, argv, 0), uivocab::kMaxTitle));
        return JS_UNDEFINED;
    }

    // native.uiTitleGet(): the title the element's frames show now. No permission: it only reads its own.
    AMPIFY_NATIVE(UiTitleGet) {
        ScriptExtension* ext = Owner(ctx);
        std::string title = HostOf(ext).Ui().Title(ext->id()).value_or("");
        return JS_NewStringLen(ctx, title.data(), title.size());
    }

    // native.uiTitleReset(): back to the element's name, as when it started.
    AMPIFY_NATIVE(UiTitleReset) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "ui.window", &failure)) return failure;
        HostOf(ext).Ui().SetTitle(ext->id(), DefaultTitle(*ext));
        return JS_UNDEFINED;
    }
};

}  // namespace

void DefineUi(api::NativeScope& scope) {
    // The lists the prelude of ui and react read instead of copying them (JSON, see uivocab::VocabularyJson).
    scope.String("uiVocabulary", uivocab::VocabularyJson());
    scope.Function("uiControl", UiNatives::UiControl, 5);
    scope.Function("uiControlLabel", UiNatives::UiControlLabel, 2);
    scope.Function("uiControlActive", UiNatives::UiControlActive, 2);
    scope.Function("uiControlDisabled", UiNatives::UiControlDisabled, 2);
    scope.Function("uiControlMark", UiNatives::UiControlMark, 3);
    scope.Function("uiPlaybarReadout", UiNatives::UiPlaybarReadout, 1);
    scope.Function("uiPlaybarHover", UiNatives::UiPlaybarHover, 1);
    scope.Function("uiControlRemove", UiNatives::UiControlRemove, 1);
    scope.Function("uiControlKinds", UiNatives::UiControlKinds, 2);
    scope.Function("uiPanel", UiNatives::UiPanel, 2);
    scope.Function("uiPanelRemove", UiNatives::UiPanelRemove, 1);
    scope.Function("uiPopup", UiNatives::UiPopup, 1);
    scope.Function("uiFullscreen", UiNatives::UiFullscreen, 2);
    scope.Function("uiFullscreenClose", UiNatives::UiFullscreenClose, 1);
    scope.Function("uiApplePlayer", UiNatives::UiApplePlayer, 0);
    scope.Function("uiPage", UiNatives::UiPage, 2);
    scope.Function("uiNavigate", UiNatives::UiNavigate, 1);
    scope.Function("uiTitle", UiNatives::UiTitle, 1);
    scope.Function("uiTitleGet", UiNatives::UiTitleGet, 0);
    scope.Function("uiTitleReset", UiNatives::UiTitleReset, 0);
}

// The panel frame, page header and popup of an element show its name, in the display language when the manifest
// translates it, until it sets a title of its own.
void StartUi(ScriptExtension& extension) {
    ScriptBridge::HostOf(&extension).Ui().SetTitle(extension.id(), DefaultTitle(extension));
}

void StopUi(ScriptExtension& extension) { ScriptBridge::HostOf(&extension).Ui().RemoveElement(extension.id()); }

}  // namespace ampify::natives
