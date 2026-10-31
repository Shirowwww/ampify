#pragma once

// Colours of an image, for Ampify.color.extract (the counterpart of Spicetify's colorExtractor). Median cut over the
// pixels, then a choice of swatches by lightness and saturation, as Android's Palette and node-vibrant do.
// Pure C++: the caller hands an RGBA buffer, already in memory.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ampify {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;

    bool operator==(const Rgb& other) const { return r == other.r && g == other.g && b == other.b; }
    std::string Hex() const;  // "#rrggbb"
};

struct Swatch {
    Rgb color;
    size_t population = 0;  // pixels the colour stands for, among those sampled
};

// The keys of Spicetify's colorExtractor, always all filled. When no colour of the image fits a key, the key gets
// a colour derived from the most prominent one.
struct ExtractedColors {
    Rgb prominent;            // PROMINENT: the most common colour
    Rgb vibrant;              // VIBRANT
    Rgb darkVibrant;          // DARK_VIBRANT
    Rgb lightVibrant;         // LIGHT_VIBRANT
    Rgb desaturated;          // DESATURATED (muted)
    Rgb vibrantNonAlarming;   // VIBRANT_NON_ALARMING: vibrant, but not a warning red
    std::vector<Swatch> palette;  // the median-cut colours, most common first
};

// `rgba` holds width * height * 4 bytes, row by row. Pixels with alpha below 128 are ignored. `maxColors` bounds
// the palette (2..64). Empty when the size does not match or no pixel is opaque.
std::optional<ExtractedColors> ExtractColors(const uint8_t* rgba, size_t size, size_t width, size_t height,
                                             size_t maxColors = 16);

}  // namespace ampify
