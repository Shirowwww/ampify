// Natives of the module http (core/prelude/http.cpp). Permission: network:<host>, for each https host.

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

#include "../api/bridge.hpp"
#include "../api/module.hpp"

namespace amk::natives {

namespace {

// The host of an https URL, lowercase, or empty when the URL has another form.
std::string HttpsHost(const std::string& url) {
    constexpr std::string_view kPrefix = "https://";
    if (url.compare(0, kPrefix.size(), kPrefix) != 0) return {};
    size_t end = url.find_first_of("/?#", kPrefix.size());
    std::string host = url.substr(kPrefix.size(), end == std::string::npos ? std::string::npos : end - kPrefix.size());
    if (host.empty() || host.find_first_of("@:[") != std::string::npos) return {};
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });
    return host;
}

struct HttpNatives : ScriptBridge {
    AMK_NATIVE(HttpGet) {
        ScriptExtension* ext = Owner(ctx);
        std::string url = String(ctx, argc, argv, 0).value_or("");
        std::string host = HttpsHost(url);
        if (host.empty()) return Throw(ctx, "TypeError", "seules les URL https:// sont autorisées");
        std::vector<std::string> allowed = ext->manifest().NetworkHosts();
        if (std::find(allowed.begin(), allowed.end(), host) == allowed.end()) {
            return Throw(ctx, "PermissionDenied", "permission manquante : network:" + host + " (à déclarer dans manifest.json)");
        }
        HttpResponse response = HostOf(ext).HttpGet(url);
        JSValue object = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, object, "status", JS_NewInt32(ctx, response.status));
        JS_SetPropertyStr(ctx, object, "text", JS_NewStringLen(ctx, response.text.data(), response.text.size()));
        return object;
    }
};

}  // namespace

void DefineHttp(api::NativeScope& scope) {
    scope.Function("httpGet", HttpNatives::HttpGet, 1);
}

}  // namespace amk::natives
