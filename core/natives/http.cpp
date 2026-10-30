// Natives of the module http (core/prelude/http.cpp). Permission: network:<host>, for each https host. get goes
// through Host::HttpGet; post through the optional HttpPostHost (core/natives/http.hpp), with the same URL rules.

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"
#include "http.hpp"

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

constexpr size_t kMaxPostBody = 64 * 1024;
constexpr size_t kMaxHeaderValue = 1024;

// The headers an element may set on a POST. Host, length, cookies and the like stay with the host.
bool IsAllowedHeader(const std::string& name) {
    return name == "content-type" || name == "authorization" || name == "accept";
}

struct HttpNatives : ScriptBridge {
    // The https URL of argument 0 when its host is declared in the manifest. Otherwise throws, through `failure`.
    static std::optional<std::string> CheckedUrl(JSContext* ctx, ScriptExtension* ext, int argc, JSValueConst* argv,
                                                 JSValue* failure) {
        std::string url = String(ctx, argc, argv, 0).value_or("");
        std::string host = HttpsHost(url);
        if (host.empty()) {
            *failure = Throw(ctx, "TypeError", "seules les URL https:// sont autorisées");
            return std::nullopt;
        }
        std::vector<std::string> allowed = ext->manifest().NetworkHosts();
        if (std::find(allowed.begin(), allowed.end(), host) == allowed.end()) {
            *failure = Throw(ctx, "PermissionDenied",
                             "permission manquante : network:" + host + " (à déclarer dans manifest.json)");
            return std::nullopt;
        }
        return url;
    }

    static JSValue ResponseObject(JSContext* ctx, const HttpResponse& response) {
        JSValue object = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, object, "status", JS_NewInt32(ctx, response.status));
        JS_SetPropertyStr(ctx, object, "text", JS_NewStringLen(ctx, response.text.data(), response.text.size()));
        return object;
    }

    AMK_NATIVE(HttpGet) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        std::optional<std::string> url = CheckedUrl(ctx, ext, argc, argv, &failure);
        if (!url) return failure;
        return ResponseObject(ctx, HostOf(ext).HttpGet(*url));
    }

    // native.httpPost(url, body, headersJson): headersJson is an object of lowercase names, built by the prelude.
    AMK_NATIVE(HttpPost) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        std::optional<std::string> url = CheckedUrl(ctx, ext, argc, argv, &failure);
        if (!url) return failure;
        std::string body = String(ctx, argc, argv, 1).value_or("");
        if (body.size() > kMaxPostBody) return Throw(ctx, "RangeError", "corps de requête trop long (64 Ko au plus)");

        HttpHeaders headers;
        std::optional<Json> given = Json::Parse(String(ctx, argc, argv, 2).value_or("{}"));
        if (!given || !given->AsObject()) return Throw(ctx, "TypeError", "headers doit être un objet");
        for (const auto& [name, value] : *given->AsObject()) {
            if (!IsAllowedHeader(name)) {
                return Throw(ctx, "TypeError", "en-tête non autorisé : " + name + " (content-type, authorization, accept)");
            }
            const std::string* text = value.AsString();
            if (!text || text->size() > kMaxHeaderValue ||
                text->find_first_of(std::string("\r\n\0", 3)) != std::string::npos) {
                return Throw(ctx, "TypeError", "valeur d'en-tête refusée : " + name);
            }
            headers.emplace_back(name, *text);
        }

        HttpPostHost* poster = HostOf(ext).Capability<HttpPostHost>();
        if (!poster) return ResponseObject(ctx, HttpResponse{0, "envoi non pris en charge par cet hôte"});
        return ResponseObject(ctx, poster->HttpPost(*url, body, headers));
    }
};

}  // namespace

void DefineHttp(api::NativeScope& scope) {
    scope.Function("httpGet", HttpNatives::HttpGet, 1);
    scope.Function("httpPost", HttpNatives::HttpPost, 3);
}

}  // namespace amk::natives
