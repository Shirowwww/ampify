// Natives of the module color (src/core/prelude/color.cpp). No permission: the script only gets back colours computed
// from pixels it already holds, or from an artwork on Apple's image server, whose address the player gives every
// element (track.artwork).

#include <string>
#include <string_view>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../color_extract.hpp"
#include "../image_host.hpp"
#include "http.hpp"

namespace ampify::natives {

namespace {

// The side an artwork is decoded to at most: enough for the palette, cheap to count.
constexpr uint32_t kArtworkSide = 128;

// Apple's image server (https://is1-ssl.mzstatic.com/image/thumb/...).
bool IsArtworkHost(const std::string& host) {
    return host == "mzstatic.com" || host.ends_with(".mzstatic.com");
}

// The keys of Spicetify's colorExtractor, then the palette.
std::string ColorsJson(const ExtractedColors& colors) {
    auto key = [](const char* name, const Rgb& color) {
        return std::string("\"") + name + "\":\"" + color.Hex() + "\",";
    };
    std::string json = "{" + key("PROMINENT", colors.prominent) + key("VIBRANT", colors.vibrant) +
                       key("DARK_VIBRANT", colors.darkVibrant) + key("LIGHT_VIBRANT", colors.lightVibrant) +
                       key("DESATURATED", colors.desaturated) +
                       key("VIBRANT_NON_ALARMING", colors.vibrantNonAlarming) + "\"palette\":[";
    for (size_t i = 0; i < colors.palette.size(); i++) {
        json += (i ? ",{" : "{");
        json += "\"color\":\"" + colors.palette[i].color.Hex() +
                "\",\"population\":" + std::to_string(colors.palette[i].population) + "}";
    }
    json += "]}";
    return json;
}

// What the artwork at `url` gives: status 200 and the colours as JSON text, or status 0 and the reason. Worker thread.
HttpResponse ArtworkColors(Host& host, ImageHost* images, const std::string& url) {
    if (!images) return {0, "images are not decoded by this host"};
    HttpResponse got = host.HttpGet(url);
    if (got.status != 200) return {0, "the artwork could not be downloaded (status " + std::to_string(got.status) + ")"};
    std::optional<RgbaImage> image = images->DecodeImage(got.text, kArtworkSide);
    if (!image) return {0, "the artwork is not an image Windows can read"};
    std::optional<ExtractedColors> colors =
        ExtractColors(image->pixels.data(), image->pixels.size(), image->width, image->height);
    if (!colors) return {0, "the artwork is fully transparent"};
    return {200, ColorsJson(*colors)};
}

// The colours of the cover playing now, the one Windows shows in its media flyout. Worker thread.
HttpResponse PlayingColors(ArtworkHost* artworks, ImageHost* images) {
    if (!artworks || !images) return {0, "covers are not read by this host"};
    std::optional<ArtworkHost::File> file = artworks->CurrentArtwork();
    if (!file || file->bytes.empty()) return {204, ""};
    std::optional<RgbaImage> image = images->DecodeImage(file->bytes, kArtworkSide);
    if (!image) return {0, "the cover is not an image Windows can read"};
    std::optional<ExtractedColors> colors =
        ExtractColors(image->pixels.data(), image->pixels.size(), image->width, image->height);
    if (!colors) return {204, ""};
    return {200, ColorsJson(*colors)};
}

struct ColorNatives : ScriptBridge {
    // native.colorFromPlaying(): starts reading the colours of the cover playing (permission player.read) and returns
    // the request's id; the answer comes back as dispatch.http, 204 without a cover.
    AMPIFY_NATIVE(FromPlaying) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        Host* host = &HostOf(ext);
        ArtworkHost* artworks = host->Capability<ArtworkHost>();
        ImageHost* images = host->Capability<ImageHost>();
        uint32_t id = SubmitHttp(ext, [artworks, images] { return PlayingColors(artworks, images); });
        if (id == 0) {
            return Throw(ctx, "RangeError",
                         "too many requests in flight (" + std::to_string(kMaxHttpInFlight) + " at most)");
        }
        return JS_NewUint32(ctx, id);
    }

    // native.colorExtract(bytes: Uint8Array, width, height): JSON text with Spicetify's keys, or null.
    AMPIFY_NATIVE(Extract) {
        if (argc < 3) return JS_NULL;
        size_t size = 0;
        uint8_t* bytes = JS_GetUint8Array(ctx, &size, argv[0]);
        if (!bytes) return JS_EXCEPTION;
        double width = Number(ctx, argc, argv, 1);
        double height = Number(ctx, argc, argv, 2);
        if (!(width > 0 && height > 0 && width <= 16384 && height <= 16384)) return JS_NULL;
        std::optional<ExtractedColors> colors =
            ExtractColors(bytes, size, static_cast<size_t>(width), static_cast<size_t>(height));
        if (!colors) return JS_NULL;
        std::string json = ColorsJson(*colors);
        return JS_NewStringLen(ctx, json.data(), json.size());
    }

    // native.colorFromArtwork(url): starts the download and returns its id. The answer comes back as dispatch.http,
    // like a request of Ampify.http, and counts towards the same cap.
    AMPIFY_NATIVE(FromArtwork) {
        std::string url = String(ctx, argc, argv, 0).value_or("");
        if (!IsArtworkHost(HttpsHost(url))) {
            return Throw(ctx, "TypeError",
                         "color.fromArtwork takes an artwork address of Apple Music (https://*.mzstatic.com), such as "
                         "track.artwork");
        }
        ScriptExtension* ext = Owner(ctx);
        Host* host = &HostOf(ext);
        ImageHost* images = host->Capability<ImageHost>();
        uint32_t id = SubmitHttp(ext, [host, images, url] { return ArtworkColors(*host, images, url); });
        if (id == 0) {
            return Throw(ctx, "RangeError",
                         "too many requests in flight (" + std::to_string(kMaxHttpInFlight) + " at most)");
        }
        return JS_NewUint32(ctx, id);
    }
};

}  // namespace

void DefineColor(api::NativeScope& scope) {
    scope.Function("colorExtract", ColorNatives::Extract, 3);
    scope.Function("colorFromArtwork", ColorNatives::FromArtwork, 1);
    scope.Function("colorFromPlaying", ColorNatives::FromPlaying, 0);
}

}  // namespace ampify::natives
