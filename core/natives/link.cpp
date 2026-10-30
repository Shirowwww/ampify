// Natives of the module link (core/prelude/link.cpp). An app and an extension with the same id, started by the same
// host (so in the same process), are linked: what one posts reaches the other's dispatch.link, queued like any other
// event, so it runs on the JavaScript thread at the receiver's next Pump. No permission: the two elements come from
// the same id, and nothing leaves the process.

#include <algorithm>
#include <mutex>
#include <vector>

#include "../api/bridge.hpp"
#include "../api/module.hpp"

namespace amk::natives {

namespace {

constexpr size_t kMaxLinkMessageBytes = size_t{64} << 10;  // JSON text of one message

// The elements that can be linked, while their runtime exists.
struct LinkRegistry {
    std::mutex mutex;
    std::vector<ScriptExtension*> members;
};

LinkRegistry& Registry() {
    static LinkRegistry registry;
    return registry;
}

// Kept in the element's module state (ScriptBridge::State): it is dropped with the runtime, whether the element
// stops, fails to start or is destroyed, and then leaves the registry.
struct LinkSlot {
    ScriptExtension* self = nullptr;

    ~LinkSlot() {
        if (!self) return;
        LinkRegistry& registry = Registry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        registry.members.erase(std::remove(registry.members.begin(), registry.members.end(), self),
                               registry.members.end());
    }
};

bool Linkable(const ScriptExtension& element) {
    return element.manifest().kind == ElementKind::App || element.manifest().kind == ElementKind::Extension;
}

struct LinkNatives : ScriptBridge {
    // The linked element: same id, the other kind, same host. Call under the registry lock.
    static ScriptExtension* PeerOf(ScriptExtension* ext) {
        for (ScriptExtension* other : Registry().members) {
            if (other != ext && other->id() == ext->id() && other->manifest().kind != ext->manifest().kind &&
                &HostOf(other) == &HostOf(ext)) {
                return other;
            }
        }
        return nullptr;
    }

    AMK_NATIVE(LinkPost) {
        ScriptExtension* ext = Owner(ctx);
        std::string json = String(ctx, argc, argv, 0).value_or("null");
        if (json.size() > kMaxLinkMessageBytes) {
            return Throw(ctx, "RangeError", "message trop long pour AMK.link : 64 Ko au plus");
        }
        std::lock_guard<std::mutex> lock(Registry().mutex);
        ScriptExtension* peer = PeerOf(ext);
        if (!peer || !peer->running()) return JS_FALSE;
        peer->DeliverEvent("link", json);
        return JS_TRUE;
    }

    AMK_NATIVE(LinkConnected) {
        std::lock_guard<std::mutex> lock(Registry().mutex);
        ScriptExtension* peer = PeerOf(Owner(ctx));
        return JS_NewBool(ctx, peer && peer->running());
    }
};

}  // namespace

void DefineLink(api::NativeScope& scope) {
    scope.Function("linkPost", LinkNatives::LinkPost, 1);
    scope.Function("linkConnected", LinkNatives::LinkConnected, 0);
}

void StartLink(ScriptExtension& extension) {
    if (!Linkable(extension)) return;
    LinkSlot& slot = ScriptBridge::State<LinkSlot>(&extension);
    if (slot.self) return;
    slot.self = &extension;
    LinkRegistry& registry = Registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.members.push_back(&extension);
}

}  // namespace amk::natives
