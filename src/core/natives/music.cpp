// Natives of the module music (src/core/prelude/music.cpp). Permission player.read: the script learns about songs,
// as the player tells it about the one playing. The kernel talks to Apple's public web API itself (src/core/apple_web.*),
// so the element needs no network permission and never sees the token.

#include <string>
#include <vector>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../apple_web.hpp"
#include "http.hpp"

namespace ampify::natives {

namespace {

std::vector<std::string> SplitIds(const std::string& text) {
    std::vector<std::string> ids;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t comma = text.find(',', start);
        if (comma == std::string::npos) comma = text.size();
        if (comma > start) ids.push_back(text.substr(start, comma - start));
        start = comma + 1;
    }
    return ids;
}

}  // namespace

struct MusicNatives : ScriptBridge {
    // native.musicAnalysis(idsCommaSeparated, storefront): a request whose answer is {"items":{...}}.
    AMPIFY_NATIVE(MusicAnalysis) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        std::vector<std::string> ids = appleweb::CatalogIds(SplitIds(String(ctx, argc, argv, 0).value_or("")));
        std::string storefront = appleweb::Storefront(String(ctx, argc, argv, 1).value_or(""));
        Host* host = &HostOf(ext);
        HttpPostHost* sender = host->Capability<HttpPostHost>();
        uint32_t id = SubmitHttp(ext, [host, sender, ids, storefront]() -> HttpResponse {
            if (ids.empty()) return {200, "{\"items\":{}}"};
            if (!sender) return {0, "this host cannot reach Apple's web API"};
            appleweb::Fetch fetch{
                [host](const std::string& url) { return host->HttpGet(url); },
                [sender](const std::string& url, const HttpHeaders& headers) {
                    return sender->HttpGetWithHeaders(url, headers);
                }};
            return appleweb::Analysis(fetch, storefront, ids);
        });
        return RequestStarted(ctx, id);
    }
};

void DefineMusic(api::NativeScope& scope) {
    scope.Function("musicAnalysis", MusicNatives::MusicAnalysis, 2);
}

}  // namespace ampify::natives
