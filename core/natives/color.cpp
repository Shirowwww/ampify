// Natives of the module color (core/prelude/color.cpp). No permission: the script only gets back colours computed
// from pixels it already holds.

#include <string>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../color_extract.hpp"

namespace ampify::natives {

namespace {

struct ColorNatives : ScriptBridge {
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

        auto key = [](const char* name, const Rgb& color) {
            return std::string("\"") + name + "\":\"" + color.Hex() + "\",";
        };
        std::string json = "{" + key("PROMINENT", colors->prominent) + key("VIBRANT", colors->vibrant) +
                           key("DARK_VIBRANT", colors->darkVibrant) + key("LIGHT_VIBRANT", colors->lightVibrant) +
                           key("DESATURATED", colors->desaturated) +
                           key("VIBRANT_NON_ALARMING", colors->vibrantNonAlarming) + "\"palette\":[";
        for (size_t i = 0; i < colors->palette.size(); i++) {
            json += (i ? ",{" : "{");
            json += "\"color\":\"" + colors->palette[i].color.Hex() +
                    "\",\"population\":" + std::to_string(colors->palette[i].population) + "}";
        }
        json += "]}";
        return JS_NewStringLen(ctx, json.data(), json.size());
    }
};

}  // namespace

void DefineColor(api::NativeScope& scope) {
    scope.Function("colorExtract", ColorNatives::Extract, 3);
}

}  // namespace ampify::natives
