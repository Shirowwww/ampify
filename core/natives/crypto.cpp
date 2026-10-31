// Natives of the module crypto (core/prelude/crypto.cpp). No permission: hashing touches nothing outside the element.

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../md5.hpp"

namespace ampify::natives {

namespace {

struct CryptoNatives : ScriptBridge {
    // native.md5(text): the digest of the UTF-8 bytes of the text.
    AMPIFY_NATIVE(Md5) {
        std::string digest = Md5Hex(String(ctx, argc, argv, 0).value_or(""));
        return JS_NewStringLen(ctx, digest.data(), digest.size());
    }
};

}  // namespace

void DefineCrypto(api::NativeScope& scope) {
    scope.Function("md5", CryptoNatives::Md5, 1);
}

}  // namespace ampify::natives
