// The CSS values that paint: colours, gradients, images, the song's artwork and backdrops.

#include "properties.hpp"
#include "properties_detail.hpp"

namespace ampify::theme {

using namespace detail;

namespace {

// Apple Music's own colour for a key is read when the value is written: a gradient or a tint is built at load time.
Conversion FailOnAppColour(std::string_view name, std::string_view where) {
    return Fail("color \"" + std::string(TrimSpace(name)) + "\" is not in [colors]: " + std::string(where) +
                " takes a colour of the theme or a #hex");
}

// The direction of `to <side>` as an angle: to top is 0deg, to right 90deg, clockwise as in CSS.
std::optional<double> SideAngle(std::string_view text) {
    std::string lowered = ToLowerAscii(text);
    std::vector<std::string_view> words = SplitWords(lowered);
    if (words.size() < 2 || words.size() > 3 || words[0] != "to") return std::nullopt;
    bool top = false, bottom = false, left = false, right = false;
    for (size_t i = 1; i < words.size(); i++) {
        if (words[i] == "top") top = true;
        else if (words[i] == "bottom") bottom = true;
        else if (words[i] == "left") left = true;
        else if (words[i] == "right") right = true;
        else return std::nullopt;
    }
    if ((top && bottom) || (left && right)) return std::nullopt;
    if (top) return right ? 45.0 : left ? 315.0 : 0.0;
    if (bottom) return right ? 135.0 : left ? 225.0 : 180.0;
    if (right) return 90.0;
    if (left) return 270.0;
    return std::nullopt;
}

// linear-gradient(...) to "x1,y1,x2,y2;#AARRGGBB@offset;...". The points are relative to the element: the line goes
// through the centre at the CSS angle, long enough to reach the corners of a square, as CSS draws it.
Conversion ConvertGradient(std::string_view inside, const ValueContext& context) {
    std::vector<std::string_view> pieces = SplitArguments(inside);
    double angle = 180;  // CSS default: to bottom
    size_t first = 0;
    if (!pieces.empty()) {
        std::string head = ToLowerAscii(pieces[0]);
        if (head.starts_with("to ")) {
            std::optional<double> side = SideAngle(pieces[0]);
            if (!side) return Fail("unreadable gradient direction: \"" + std::string(pieces[0]) + "\"");
            angle = *side;
            first = 1;
        } else if (!head.empty() && head.front() != '#' && head.front() != '$' && !head.starts_with("transparent")) {
            std::optional<double> degrees = ParseDegrees(pieces[0]);
            if (!degrees) {
                return Fail("unreadable gradient angle: \"" + std::string(pieces[0]) + "\" (in degrees, like 135deg)");
            }
            angle = *degrees;
            first = 1;
        }
    }

    struct Stop {
        std::string color;
        std::optional<double> offset;
    };
    std::vector<Stop> stops;
    for (size_t i = first; i < pieces.size(); i++) {
        std::vector<std::string_view> words = SplitWords(pieces[i]);
        if (words.empty() || words.size() > 2) {
            return Fail("unreadable gradient color: \"" + std::string(pieces[i]) + "\"");
        }
        Conversion color = ConvertColor(words[0], context);
        if (!color.value) return color;
        if (color.value->kind == ValueKind::ColorKey) return FailOnAppColour(words[0], "a gradient");
        Stop stop{color.value->text, std::nullopt};
        if (words.size() == 2) {
            stop.offset = ParsePercent(words[1]);
            if (!stop.offset || *stop.offset < 0 || *stop.offset > 1) {
                return Fail("unreadable gradient position: \"" + std::string(words[1]) + "\" (from 0% to 100%)");
            }
        }
        stops.push_back(std::move(stop));
    }
    if (stops.size() < 2) return Fail("a gradient needs at least two colors");

    // The CSS rules for missing positions: the first is 0%, the last 100%, the others share the space between their
    // neighbours evenly. A position before an earlier one moves up to it.
    if (!stops.front().offset) stops.front().offset = 0.0;
    if (!stops.back().offset) stops.back().offset = 1.0;
    for (size_t i = 1; i < stops.size(); i++) {
        if (stops[i].offset) {
            stops[i].offset = std::max(*stops[i].offset, *stops[i - 1].offset);
            continue;
        }
        size_t next = i;
        while (!stops[next].offset) next++;
        double from = *stops[i - 1].offset;
        double to = std::max(*stops[next].offset, from);
        for (size_t j = i; j < next; j++) {
            stops[j].offset = from + (to - from) * static_cast<double>(j - i + 1) / static_cast<double>(next - i + 1);
        }
    }

    const double radians = angle * 3.14159265358979323846 / 180;
    double dx = std::sin(radians);
    double dy = -std::cos(radians);  // the screen's y axis points down
    double half = (std::abs(dx) + std::abs(dy)) / 2;
    std::string text = FormatFraction(0.5 - dx * half) + "," + FormatFraction(0.5 - dy * half) + "," +
                       FormatFraction(0.5 + dx * half) + "," + FormatFraction(0.5 + dy * half);
    for (const Stop& stop : stops) text += ";" + stop.color + "@" + FormatFraction(*stop.offset);
    return Ok(ValueKind::Gradient, std::move(text));
}

// The fit words of `url(...) <fit>` and `artwork <fit>`.
std::optional<ImageFit> FitOf(std::string_view word) {
    if (word == "cover") return ImageFit::UniformToFill;
    if (word == "contain") return ImageFit::Uniform;
    if (word == "fill") return ImageFit::Fill;
    return std::nullopt;
}

}  // namespace

namespace detail {

// A plain colour: `#hex`, `$palette` name (or a Fluent key, Apple Music's colour for it) or `transparent`.
Conversion ConvertColor(std::string_view raw, const ValueContext& context) {
    std::string_view text = TrimSpace(raw);
    if (text.empty()) return Fail("empty color");

    if (text.front() == '$') {
        std::optional<Argb> color = context.palette.Find(text.substr(1));
        if (color) return Ok(ValueKind::Color, ToXamlColor(*color));
        // A Fluent key the theme leaves alone (or a snippet with no theme): Apple Music's own colour for it.
        if (std::optional<std::string_view> key = FindColourKey(text.substr(1))) {
            return Ok(ValueKind::ColorKey, std::string(*key));
        }
        return Fail("color \"" + std::string(text) + "\" is not in [colors], nor a Fluent colour key");
    }

    if (ToLowerAscii(text) == "transparent") return Ok(ValueKind::Color, "#00FFFFFF");

    std::optional<Argb> color = ParseColor(text);
    if (!color) return Fail("unreadable color: \"" + std::string(text) + "\"");
    return Ok(ValueKind::Color, ToXamlColor(*color));
}

// url(image), and an optional fit after it: cover, contain or fill.
Conversion ConvertImage(std::string_view text, const ValueContext& context) {
    size_t close = text.find(')');
    std::optional<std::string> path =
        close == std::string_view::npos ? std::nullopt : UrlArgument(text.substr(0, close + 1));
    if (!path) return Fail("unreadable image: \"" + std::string(text) + "\"");
    if (!context.allowImages) return Fail("images are turned off (assets part of the theme)");
    if (!IsSafeRelativePath(*path)) return Fail("image path not allowed: " + *path);
    if (!context.files.Exists(*path)) return Fail("image not found: " + *path);

    std::string fit = ToLowerAscii(TrimSpace(text.substr(close + 1)));
    std::optional<ImageFit> chosen = fit.empty() ? ImageFit::Default : FitOf(fit);
    if (!chosen) return Fail("unknown image fit: \"" + fit + "\" (cover, contain or fill)");
    return Ok(ValueKind::Image, EncodeImage({context.folder + "/" + *path, *chosen}));
}

// artwork [cover|contain|fill]: the artwork of the song playing, as Apple Music shows it in its play bar. The backend
// follows it: the image changes with the song.
Conversion ConvertArtwork(std::string_view text, const ValueContext& context) {
    if (!context.allowImages) return Fail("images are turned off (assets part of the theme)");
    std::string fit = ToLowerAscii(TrimSpace(text.substr(std::string_view("artwork").size())));
    std::optional<ImageFit> chosen = fit.empty() ? ImageFit::UniformToFill : FitOf(fit);
    if (!chosen) return Fail("unknown image fit: \"" + fit + "\" (cover, contain or fill)");
    return Ok(ValueKind::Image, EncodeImage({std::string(kArtworkImage), *chosen}));
}

Conversion ConvertBrush(std::string_view raw, const ValueContext& context) {
    std::string_view text = TrimSpace(raw);
    if (text.empty()) return Fail("empty color");

    if (ToLowerAscii(text.substr(0, 7)) == "artwork" && (text.size() == 7 || text[7] == ' ')) return ConvertArtwork(text, context);
    if (ToLowerAscii(text.substr(0, 4)) == "url(") return ConvertImage(text, context);
    if (std::optional<std::string_view> inside = FunctionArguments(text, "linear-gradient")) {
        return ConvertGradient(*inside, context);
    }
    return ConvertColor(text, context);
}

// acrylic(<tint>, <opacity>): the in-app acrylic of WinUI, a tint over a blur of what lies behind the element.
// liquid-glass(<tint>, <opacity>[, <blur>[, <saturation>]]): Ampify's glass (src/win/glass_brush.*), a lighter blur
// of what lies behind, its colours made more vivid, the tint, and a sheen from the top edge.
Conversion ConvertBackdrop(std::string_view raw, const ValueContext& context) {
    constexpr double kGlassBlur = 12;
    constexpr double kGlassSaturation = 1.8;
    constexpr double kMaxGlassBlur = 40;
    constexpr double kMaxGlassSaturation = 4;
    std::optional<std::string_view> inside = FunctionArguments(raw, "acrylic");
    const bool glass = !inside;
    if (glass) inside = FunctionArguments(raw, "liquid-glass");
    if (!inside) return Fail("a backdrop is written acrylic(tint, opacity) or liquid-glass(tint, opacity[, blur[, saturation]])");
    std::vector<std::string_view> pieces = SplitArguments(*inside);
    if (glass ? pieces.size() < 2 || pieces.size() > 4 : pieces.size() != 2) {
        return Fail(glass ? "liquid-glass(tint, opacity[, blur[, saturation]]) takes two to four values"
                          : "acrylic(tint, opacity) takes two values");
    }

    Conversion tint = ConvertColor(pieces[0], context);
    if (!tint.value) return tint;
    if (tint.value->kind == ValueKind::ColorKey) return FailOnAppColour(pieces[0], "a backdrop tint");
    std::optional<double> opacity = ParseNumber(pieces[1]);
    if (!opacity || *opacity < 0 || *opacity > 1) {
        return Fail("backdrop opacity outside 0 to 1: \"" + std::string(pieces[1]) + "\"");
    }
    std::string text = tint.value->text + ";" + FormatNumber(*opacity);
    if (!glass) return Ok(ValueKind::Acrylic, text);
    std::optional<double> blur = pieces.size() > 2 ? ParsePixels(pieces[2]) : kGlassBlur;
    if (!blur || *blur < 0 || *blur > kMaxGlassBlur) {
        return Fail("liquid-glass blur outside 0 to 40 px: \"" + std::string(pieces[2]) + "\"");
    }
    std::optional<double> saturation = pieces.size() > 3 ? ParseNumber(pieces[3]) : kGlassSaturation;
    if (!saturation || *saturation < 0 || *saturation > kMaxGlassSaturation) {
        return Fail("liquid-glass saturation outside 0 to 4: \"" + std::string(pieces[3]) + "\"");
    }
    return Ok(ValueKind::Acrylic, text + ";" + std::string(kLiquidGlass) + "," + FormatNumber(*blur) + "," +
                                      FormatNumber(*saturation));
}

}  // namespace detail

GradientLine FitGradient(const GradientLine& square, double width, double height) {
    // Stretched to the element, relative lines square to (x, y) end square to the screen direction only when the line
    // runs along (x * width, y * height); its ends then sit on the lines through the two far corners.
    const double x = (square.x2 - square.x1) * width, y = (square.y2 - square.y1) * height;
    const double length = x * x + y * y;
    if (width <= 0 || height <= 0 || length <= 0) return square;
    const double reach = (std::abs(x) + std::abs(y)) / 2 / length;
    return {0.5 - x * reach, 0.5 - y * reach, 0.5 + x * reach, 0.5 + y * reach};
}

}  // namespace ampify::theme
