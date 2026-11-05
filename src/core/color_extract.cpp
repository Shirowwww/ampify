#include "color_extract.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace ampify {

namespace {

constexpr size_t kMaxSamples = 40000;  // pixels looked at, at most: a large cover is sampled on a grid

struct Pixel {
    std::array<uint8_t, 3> c;
};

struct Hsl {
    double h = 0, s = 0, l = 0;  // h in degrees, s and l in 0..1
};

Hsl ToHsl(const Rgb& rgb) {
    double r = rgb.r / 255.0, g = rgb.g / 255.0, b = rgb.b / 255.0;
    double high = std::max({r, g, b}), low = std::min({r, g, b});
    Hsl out;
    out.l = (high + low) / 2;
    double delta = high - low;
    if (delta < 1e-9) return out;
    out.s = out.l > 0.5 ? delta / (2 - high - low) : delta / (high + low);
    if (high == r) out.h = std::fmod((g - b) / delta + 6, 6.0);
    else if (high == g) out.h = (b - r) / delta + 2;
    else out.h = (r - g) / delta + 4;
    out.h *= 60;
    return out;
}

Rgb FromHsl(Hsl hsl) {
    double s = std::clamp(hsl.s, 0.0, 1.0), l = std::clamp(hsl.l, 0.0, 1.0);
    double chroma = (1 - std::fabs(2 * l - 1)) * s;
    double h = std::fmod(std::fmod(hsl.h, 360.0) + 360.0, 360.0) / 60.0;
    double x = chroma * (1 - std::fabs(std::fmod(h, 2.0) - 1));
    double r = 0, g = 0, b = 0;
    if (h < 1) r = chroma, g = x;
    else if (h < 2) r = x, g = chroma;
    else if (h < 3) g = chroma, b = x;
    else if (h < 4) g = x, b = chroma;
    else if (h < 5) r = x, b = chroma;
    else r = chroma, b = x;
    double m = l - chroma / 2;
    auto byte = [](double v) { return static_cast<uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
    return Rgb{byte(r + m), byte(g + m), byte(b + m)};
}

// One box of the median cut: a range of the pixel array.
struct Box {
    size_t begin = 0, end = 0;

    size_t Count() const { return end - begin; }
};

// The channel with the widest range in the box, and that range.
std::pair<int, int> WidestChannel(const std::vector<Pixel>& pixels, const Box& box) {
    std::array<int, 3> low{255, 255, 255}, high{0, 0, 0};
    for (size_t i = box.begin; i < box.end; i++) {
        for (int c = 0; c < 3; c++) {
            low[c] = std::min<int>(low[c], pixels[i].c[c]);
            high[c] = std::max<int>(high[c], pixels[i].c[c]);
        }
    }
    int best = 0;
    for (int c = 1; c < 3; c++) {
        if (high[c] - low[c] > high[best] - low[best]) best = c;
    }
    return {best, high[best] - low[best]};
}

// Splits the box at the median of its widest channel, never through a run of equal values, so two flat colours
// always end up in two boxes. False when the box holds a single colour.
bool Split(std::vector<Pixel>& pixels, const Box& box, Box& first, Box& second) {
    auto [channel, range] = WidestChannel(pixels, box);
    if (range == 0 || box.Count() < 2) return false;
    auto begin = pixels.begin() + static_cast<std::ptrdiff_t>(box.begin);
    auto end = pixels.begin() + static_cast<std::ptrdiff_t>(box.end);
    const int c = channel;
    std::sort(begin, end, [c](const Pixel& a, const Pixel& b) { return a.c[c] < b.c[c]; });
    uint8_t median = (begin + static_cast<std::ptrdiff_t>(box.Count() / 2))->c[c];
    auto at = std::lower_bound(begin, end, median, [c](const Pixel& p, uint8_t v) { return p.c[c] < v; });
    if (at == begin) at = std::upper_bound(begin, end, median, [c](uint8_t v, const Pixel& p) { return v < p.c[c]; });
    if (at == begin || at == end) return false;
    size_t middle = static_cast<size_t>(at - pixels.begin());
    first = {box.begin, middle};
    second = {middle, box.end};
    return true;
}

Rgb Average(const std::vector<Pixel>& pixels, const Box& box) {
    std::array<uint64_t, 3> sum{};
    for (size_t i = box.begin; i < box.end; i++) {
        for (int c = 0; c < 3; c++) sum[c] += pixels[i].c[c];
    }
    uint64_t n = std::max<uint64_t>(1, box.Count());
    return Rgb{static_cast<uint8_t>((sum[0] + n / 2) / n), static_cast<uint8_t>((sum[1] + n / 2) / n),
               static_cast<uint8_t>((sum[2] + n / 2) / n)};
}

// node-vibrant's targets and weights.
struct Target {
    double minL, targetL, maxL, minS, targetS, maxS;
};
constexpr Target kVibrant{0.3, 0.5, 0.7, 0.35, 1.0, 1.0};
constexpr Target kLightVibrant{0.55, 0.74, 1.0, 0.35, 1.0, 1.0};
constexpr Target kDarkVibrant{0.0, 0.26, 0.45, 0.35, 1.0, 1.0};
constexpr Target kMuted{0.3, 0.5, 0.7, 0.0, 0.3, 0.4};

bool IsAlarming(const Rgb& color) {
    Hsl hsl = ToHsl(color);
    return (hsl.h < 20 || hsl.h > 340) && hsl.s > 0.5 && hsl.l > 0.25 && hsl.l < 0.75;
}

// The best swatch for a target that is not taken yet, or -1.
int Pick(const std::vector<Swatch>& palette, const Target& target, const std::vector<bool>& taken,
         bool avoidAlarming = false) {
    size_t maxPopulation = 1;
    for (const Swatch& swatch : palette) maxPopulation = std::max(maxPopulation, swatch.population);
    int best = -1;
    double bestScore = -1;
    for (size_t i = 0; i < palette.size(); i++) {
        if (taken[i]) continue;
        if (avoidAlarming && IsAlarming(palette[i].color)) continue;
        Hsl hsl = ToHsl(palette[i].color);
        if (hsl.s < target.minS || hsl.s > target.maxS || hsl.l < target.minL || hsl.l > target.maxL) continue;
        double score = 3.0 * (1 - std::fabs(hsl.s - target.targetS)) + 6.5 * (1 - std::fabs(hsl.l - target.targetL)) +
                       0.5 * static_cast<double>(palette[i].population) / static_cast<double>(maxPopulation);
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

Rgb WithLightness(const Rgb& color, double lightness, double minSaturation = 0) {
    Hsl hsl = ToHsl(color);
    hsl.l = lightness;
    hsl.s = std::max(hsl.s, minSaturation);
    return FromHsl(hsl);
}

}  // namespace

std::string Rgb::Hex() const {
    char text[8];
    std::snprintf(text, sizeof(text), "#%02x%02x%02x", r, g, b);
    return text;
}

std::optional<ExtractedColors> ExtractColors(const uint8_t* rgba, size_t size, size_t width, size_t height,
                                             size_t maxColors) {
    if (!rgba || width == 0 || height == 0 || width > 16384 || height > 16384 || size != width * height * 4) {
        return std::nullopt;
    }
    maxColors = std::clamp<size_t>(maxColors, 2, 64);

    // A grid of at most kMaxSamples pixels.
    size_t step = 1;
    while ((width / step) * (height / step) > kMaxSamples) step++;
    std::vector<Pixel> pixels;
    pixels.reserve(std::min(width * height, kMaxSamples + width + height));
    for (size_t y = 0; y < height; y += step) {
        for (size_t x = 0; x < width; x += step) {
            const uint8_t* p = rgba + (y * width + x) * 4;
            if (p[3] < 128) continue;
            pixels.push_back(Pixel{{p[0], p[1], p[2]}});
        }
    }
    if (pixels.empty()) return std::nullopt;

    // Median cut: the most populated box that still holds several colours is split, until there are enough.
    std::vector<Box> boxes{{0, pixels.size()}};
    std::vector<bool> single(1, false);
    while (boxes.size() < maxColors) {
        int chosen = -1;
        for (size_t i = 0; i < boxes.size(); i++) {
            if (single[i]) continue;
            if (chosen < 0 || boxes[i].Count() > boxes[static_cast<size_t>(chosen)].Count()) chosen = static_cast<int>(i);
        }
        if (chosen < 0) break;
        Box first, second;
        if (!Split(pixels, boxes[static_cast<size_t>(chosen)], first, second)) {
            single[static_cast<size_t>(chosen)] = true;
            continue;
        }
        boxes[static_cast<size_t>(chosen)] = first;
        boxes.push_back(second);
        single.push_back(false);
    }

    ExtractedColors out;
    for (const Box& box : boxes) {
        Rgb color = Average(pixels, box);
        auto same = std::find_if(out.palette.begin(), out.palette.end(), [&](const Swatch& s) { return s.color == color; });
        if (same != out.palette.end()) same->population += box.Count();
        else out.palette.push_back({color, box.Count()});
    }
    std::stable_sort(out.palette.begin(), out.palette.end(),
                     [](const Swatch& a, const Swatch& b) { return a.population > b.population; });
    out.prominent = out.palette.front().color;

    std::vector<bool> taken(out.palette.size(), false);
    auto take = [&](const Target& target, bool avoidAlarming = false) -> std::optional<Rgb> {
        int index = Pick(out.palette, target, taken, avoidAlarming);
        if (index < 0) return std::nullopt;
        taken[static_cast<size_t>(index)] = true;
        return out.palette[static_cast<size_t>(index)].color;
    };
    std::optional<Rgb> vibrant = take(kVibrant);
    std::optional<Rgb> light = take(kLightVibrant);
    std::optional<Rgb> dark = take(kDarkVibrant);
    std::optional<Rgb> muted = take(kMuted);

    // What the image lacks is made from what it has: first the other vibrant colours, then the prominent one.
    if (!vibrant && dark) vibrant = WithLightness(*dark, kVibrant.targetL);
    if (!vibrant && light) vibrant = WithLightness(*light, kVibrant.targetL);
    if (!vibrant) vibrant = WithLightness(out.prominent, kVibrant.targetL);
    if (!dark) dark = WithLightness(*vibrant, kDarkVibrant.targetL);
    if (!light) light = WithLightness(*vibrant, kLightVibrant.targetL);
    if (!muted) {
        Hsl hsl = ToHsl(out.prominent);
        hsl.s = std::min(hsl.s, kMuted.targetS);
        hsl.l = std::clamp(hsl.l, kMuted.minL, kMuted.maxL);
        muted = FromHsl(hsl);
    }

    out.vibrant = *vibrant;
    out.lightVibrant = *light;
    out.darkVibrant = *dark;
    out.desaturated = *muted;
    out.vibrantNonAlarming = *vibrant;
    if (IsAlarming(*vibrant)) {
        std::vector<bool> none(out.palette.size(), false);
        int index = Pick(out.palette, kVibrant, none, true);
        if (index >= 0) {
            out.vibrantNonAlarming = out.palette[static_cast<size_t>(index)].color;
        } else {
            Hsl hsl = ToHsl(*vibrant);
            hsl.s = std::min(hsl.s, 0.45);  // a calmer red
            out.vibrantNonAlarming = FromHsl(hsl);
        }
    }
    return out;
}

// Relative luminance under which white text has a contrast of 7:1: (1 + 0.05) / (L + 0.05) >= 7.
constexpr double kDarkestForWhiteText = 0.1;

std::map<std::string, std::string> ArtworkVariables(const ExtractedColors& colors) {
    // Relative luminance (WCAG): above the middle grey, black text reads better than white.
    auto linear = [](uint8_t channel) {
        double c = channel / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto luminanceOf = [&](const Rgb& c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); };
    const Rgb& base = colors.prominent;
    double luminance = luminanceOf(base);
    // art-3 sits under white text: darkened until white reads on it at 7:1 or more, whatever the artwork.
    Rgb dark = colors.darkVibrant;
    while (luminanceOf(dark) > kDarkestForWhiteText) {
        dark = {static_cast<uint8_t>(dark.r * 9 / 10), static_cast<uint8_t>(dark.g * 9 / 10), static_cast<uint8_t>(dark.b * 9 / 10)};
    }
    std::map<std::string, std::string> variables = {{"art-1", base.Hex()},
                                                    {"art-2", colors.vibrant.Hex()},
                                                    {"art-3", dark.Hex()},
                                                    {"art-text", luminance > 0.179 ? "#000000" : "#ffffff"}};
    // The same without "#", so a theme can put an alpha in front: #40var(--art-1-hex).
    for (const char* name : {"art-1", "art-2", "art-3"}) variables[std::string(name) + "-hex"] = variables[name].substr(1);
    // Plain numbers from 0 to 1, for a theme's own choices: how light the artwork is, and how coloured.
    auto fraction = [](double value) {
        char text[16];
        std::snprintf(text, sizeof text, "%.2f", std::clamp(value, 0.0, 1.0));
        return std::string(text);
    };
    variables["art-luma"] = fraction((0.299 * base.r + 0.587 * base.g + 0.114 * base.b) / 255.0);
    variables["art-sat"] = fraction(ToHsl(base).s);
    return variables;
}

}  // namespace ampify
