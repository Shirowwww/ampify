// Natives of the module http (src/core/prelude/http.cpp). Permission: network:<host>, for each https host. get goes
// through Host::HttpGet; post through the optional HttpPostHost and open through BrowserHost
// (src/core/natives/http.hpp), with the same URL rules.
// The checks run here, on the JavaScript thread; the request runs on the element's HttpWorker and the answer comes
// back as dispatch.http (ScriptExtension::SubmitHttp).

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../form_data.hpp"
#include "../json.hpp"
#include "../text_util.hpp"
#include "http.hpp"

namespace ampify {

std::string HttpsHost(const std::string& url) {
    constexpr std::string_view kPrefix = "https://";
    if (url.compare(0, kPrefix.size(), kPrefix) != 0) return {};
    size_t end = url.find_first_of("/?#", kPrefix.size());
    std::string host = url.substr(kPrefix.size(), end == std::string::npos ? std::string::npos : end - kPrefix.size());
    if (host.empty() || host.find_first_of("@:[") != std::string::npos) return {};
    return ToLowerAscii(host);
}

}  // namespace ampify

namespace ampify::natives {

namespace {

constexpr size_t kMaxPostBody = 64 * 1024;
constexpr size_t kMaxHeaderValue = 1024;
constexpr std::chrono::seconds kOpenInterval{10};

constexpr size_t kMaxFormBody = 8 * 1024 * 1024;
constexpr size_t kMaxFormJsonOverhead = 64 * 1024;  // field names, quotes and keys around the values

// A field name or a file name: letters, digits and . _ - only (and [ ] in a field name, as in "files[]"), so that
// it never breaks the part's header.
bool IsFormName(const std::string& name, size_t limit, bool brackets = false) {
    return !name.empty() && name.size() <= limit && std::all_of(name.begin(), name.end(), [brackets](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-' ||
               (brackets && (c == '[' || c == ']'));
    });
}

// "image/jpeg", "text/plain"...: type/subtype in lower case.
bool IsMediaType(const std::string& type) {
    size_t slash = type.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 == type.size() || type.size() > 100) return false;
    return std::all_of(type.begin(), type.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '+' || c == '-';
    });
}

// The fields of Ampify.http.postForm, checked. On error returns nullopt and fills `error` in English.
std::optional<std::vector<FormField>> FormFields(const std::string& json, std::string* error) {
    std::optional<Json> given = Json::Parse(json);
    if (!given || !given->AsObject() || given->AsObject()->empty()) {
        *error = "postForm expects an object of fields";
        return std::nullopt;
    }
    std::vector<FormField> fields;
    for (const auto& [name, value] : *given->AsObject()) {
        if (!IsFormName(name, 64, true)) {
            *error = "field name refused: " + name + " (letters, digits, . _ - [ ] only)";
            return std::nullopt;
        }
        if (const std::string* text = value.AsString()) {
            fields.push_back({name, *text, "", ""});
            continue;
        }
        const Json* filename = value.Find("filename");
        const Json* type = value.Find("type");
        const Json* data = value.Find("base64");
        const std::string* file = filename ? filename->AsString() : nullptr;
        const std::string* media = type ? type->AsString() : nullptr;
        const std::string* encoded = data ? data->AsString() : nullptr;
        std::optional<std::string> bytes = encoded ? Base64Decode(*encoded) : std::nullopt;
        if (!value.AsObject() || !file || !IsFormName(*file, 100) || (media && !IsMediaType(*media)) || !bytes) {
            *error = "field " + name + " must be a text or { filename, type, base64 } with a plain file name";
            return std::nullopt;
        }
        fields.push_back({name, std::move(*bytes), *file, media ? *media : ""});
    }
    return fields;
}

// A multipart boundary found in none of the values. Unique, not secret: it only has to split the parts.
std::string UniqueBoundary(const std::vector<FormField>& fields) {
    for (uint64_t seed = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());;
         seed = seed * 6364136223846793005ULL + 1442695040888963407ULL) {
        std::string boundary = "ampify-" + std::to_string(seed);
        bool clash = std::any_of(fields.begin(), fields.end(), [&](const FormField& field) {
            return field.value.find(boundary) != std::string::npos;
        });
        if (!clash) return boundary;
    }
}

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
            *failure = Throw(ctx, "TypeError", "only https:// URLs are allowed");
            return std::nullopt;
        }
        std::vector<std::string> allowed = ext->manifest().NetworkHosts();
        if (std::find(allowed.begin(), allowed.end(), host) == allowed.end()) {
            *failure = Throw(ctx, "PermissionDenied",
                             "missing permission: network:" + host + " (declare it in manifest.json)");
            return std::nullopt;
        }
        return url;
    }

    // Starts the request off the JavaScript thread and returns its id; the prelude resolves the promise when
    // dispatch.http gets the answer. Over the cap, throws instead of queuing.
    static JSValue Start(JSContext* ctx, ScriptExtension* ext, std::function<HttpResponse()> request) {
        uint32_t id = SubmitHttp(ext, std::move(request));
        if (id == 0) {
            return Throw(ctx, "RangeError",
                         "too many requests in flight (" + std::to_string(kMaxHttpInFlight) + " at most)");
        }
        return JS_NewUint32(ctx, id);
    }

    AMPIFY_NATIVE(HttpGet) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        std::optional<std::string> url = CheckedUrl(ctx, ext, argc, argv, &failure);
        if (!url) return failure;
        Host* host = &HostOf(ext);
        return Start(ctx, ext, [host, target = *url] { return host->HttpGet(target); });
    }

    // native.httpPost(url, body, headersJson): headersJson is an object of lowercase names, built by the prelude.
    AMPIFY_NATIVE(HttpPost) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        std::optional<std::string> url = CheckedUrl(ctx, ext, argc, argv, &failure);
        if (!url) return failure;
        std::string body = String(ctx, argc, argv, 1).value_or("");
        if (body.size() > kMaxPostBody) return Throw(ctx, "RangeError", "request body too long (64 KB at most)");

        HttpHeaders headers;
        std::optional<Json> given = Json::Parse(String(ctx, argc, argv, 2).value_or("{}"));
        if (!given || !given->AsObject()) return Throw(ctx, "TypeError", "headers must be an object");
        for (const auto& [name, value] : *given->AsObject()) {
            if (!IsAllowedHeader(name)) {
                return Throw(ctx, "TypeError", "header not allowed: " + name + " (content-type, authorization, accept)");
            }
            const std::string* text = value.AsString();
            if (!text || text->size() > kMaxHeaderValue ||
                text->find_first_of(std::string("\r\n\0", 3)) != std::string::npos) {
                return Throw(ctx, "TypeError", "header value refused: " + name);
            }
            headers.emplace_back(name, *text);
        }

        HttpPostHost* poster = HostOf(ext).Capability<HttpPostHost>();
        if (!poster) {
            return Start(ctx, ext, [] { return HttpResponse{0, "sending is not supported by this host"}; });
        }
        return Start(ctx, ext, [poster, target = *url, body = std::move(body), headers = std::move(headers)] {
            return poster->HttpPost(target, body, headers);
        });
    }

    // native.httpPostForm(url, fieldsJson): a multipart/form-data POST, for an upload service. fieldsJson maps each
    // name to a text, or to { filename, type, base64 } for a file.
    AMPIFY_NATIVE(HttpPostForm) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        std::optional<std::string> url = CheckedUrl(ctx, ext, argc, argv, &failure);
        if (!url) return failure;
        std::string json = String(ctx, argc, argv, 1).value_or("");
        // Refused before decoding: base64 is 4/3 of the bytes, so a larger text cannot give a form within the limit.
        if (json.size() > kMaxFormBody / 3 * 4 + kMaxFormJsonOverhead) {
            return Throw(ctx, "RangeError", "form too large (8 MB at most)");
        }
        std::string error;
        std::optional<std::vector<FormField>> fields = FormFields(json, &error);
        if (!fields) return Throw(ctx, "TypeError", error);
        std::string boundary = UniqueBoundary(*fields);
        std::string body = MultipartBody(*fields, boundary);
        if (body.size() > kMaxFormBody) return Throw(ctx, "RangeError", "form too large (8 MB at most)");

        HttpPostHost* poster = HostOf(ext).Capability<HttpPostHost>();
        if (!poster) {
            return Start(ctx, ext, [] { return HttpResponse{0, "sending is not supported by this host"}; });
        }
        HttpHeaders headers{{"content-type", "multipart/form-data; boundary=" + boundary}};
        return Start(ctx, ext, [poster, target = *url, body = std::move(body), headers = std::move(headers)] {
            return poster->HttpPost(target, body, headers);
        });
    }

    // native.httpOpen(url): true when the browser shows the page. Within kOpenInterval of the element's last page,
    // or on a host without a browser, false.
    AMPIFY_NATIVE(HttpOpen) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        std::optional<std::string> url = CheckedUrl(ctx, ext, argc, argv, &failure);
        if (!url) return failure;
        BrowserHost* browser = HostOf(ext).Capability<BrowserHost>();
        if (!browser) return JS_FALSE;
        static std::mutex mutex;  // elements run on their own threads
        static std::map<std::string, std::chrono::steady_clock::time_point> lastOpened;
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto last = lastOpened.find(ext->id());
            if (last != lastOpened.end() && now - last->second < kOpenInterval) return JS_FALSE;
            lastOpened[ext->id()] = now;
        }
        return JS_NewBool(ctx, browser->OpenInBrowser(*url));
    }
};

}  // namespace

void DefineHttp(api::NativeScope& scope) {
    scope.Function("httpGet", HttpNatives::HttpGet, 1);
    scope.Function("httpPost", HttpNatives::HttpPost, 3);
    scope.Function("httpPostForm", HttpNatives::HttpPostForm, 2);
    scope.Function("httpOpen", HttpNatives::HttpOpen, 1);
}

}  // namespace ampify::natives
