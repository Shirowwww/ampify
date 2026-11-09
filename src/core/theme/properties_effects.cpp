// The CSS values that move or draw over an element: transforms, shadows, filters, clips, transitions, content.

#include "properties.hpp"
#include "properties_detail.hpp"

namespace ampify::theme {

using namespace detail;

namespace {

constexpr std::array<std::pair<std::string_view, BarShape>, 4> kBarShapes{{
    {"bars", BarShape::Bars}, {"line", BarShape::Line}, {"circle", BarShape::Circle}, {"mirror", BarShape::Mirror}}};

// bars(), bars(12), bars(12, line), bars(12, circle, 60%), bars(12, 60%, thick): the count first, then a shape, a reach
// and a width (thin or thick) in any order. `lowered` is `text` in lower case.
Conversion ConvertBars(std::string_view lowered, std::string_view text) {
    const auto failed = [&] {
        return Fail("bars() takes a whole number of bars from " + std::to_string(kMinBars) + " to " +
                    std::to_string(kMaxBars) + ", then maybe a shape (bars, line, circle, mirror), a height from " +
                    std::to_string(kMinBarsReach) + "% to 100% and thin or thick, like bars(12) or "
                    "bars(12, line, 60%) (\"" + std::string(text) + "\")");
    };
    BarsContent bars;
    std::vector<std::string_view> parts = SplitTopLevel(lowered.substr(5, lowered.size() - 6), ',');
    if (parts.size() > 4) return failed();
    bool shape = false;
    bool reach = false;
    bool width = false;
    for (size_t i = 0; i < parts.size(); i++) {
        std::string_view part = TrimSpace(parts[i]);
        if (i == 0) {
            if (part.empty() && parts.size() == 1) break;
            auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), bars.count);
            if (error != std::errc() || end != part.data() + part.size() || bars.count < kMinBars ||
                bars.count > kMaxBars) {
                return failed();
            }
        } else if (part.ends_with('%') && !reach) {
            reach = true;
            std::string_view digits = TrimSpace(part.substr(0, part.size() - 1));
            std::optional<double> percent = ParseNumber(digits);
            if (!percent || *percent < kMinBarsReach || *percent > 100) return failed();
            bars.reach = static_cast<int>(std::lround(*percent));
        } else if ((part == "thin" || part == "thick") && !width) {
            width = true;
            bars.thick = part == "thick";
        } else {
            auto known = std::find_if(kBarShapes.begin(), kBarShapes.end(), [&](const auto& s) { return s.first == part; });
            if (known == kBarShapes.end() || shape) return failed();
            shape = true;
            bars.shape = known->second;
        }
    }
    return Ok(ValueKind::Image, WriteBarsContent(bars));
}

}  // namespace

namespace detail {

// scale(x[, y]) rotate(<deg>) translate(x[, y]), and scaleX, scaleY, translateX, translateY: one CompositeTransform.
// XAML applies its parts in a fixed order (scale, then rotation, then translation), whatever the order written.
Conversion ConvertTransform(std::string_view raw) {
    std::string_view text = TrimSpace(raw);
    if (ToLowerAscii(text) == "none") return Ok(ValueKind::Transform, "1,1,0,0,0");

    double scaleX = 1, scaleY = 1, rotation = 0, translateX = 0, translateY = 0;
    size_t i = 0;
    bool any = false;
    while (i < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) {
            i++;
            continue;
        }
        size_t open = text.find('(', i);
        size_t close = open == std::string_view::npos ? open : text.find(')', open);
        if (close == std::string_view::npos) return Fail("unreadable transform: \"" + std::string(text) + "\"");
        std::string name = ToLowerAscii(TrimSpace(text.substr(i, open - i)));
        std::string_view inside = text.substr(open + 1, close - open - 1);
        i = close + 1;
        any = true;

        // Arguments are separated by commas, spaces, or both.
        std::vector<std::string_view> arguments;
        for (std::string_view piece : SplitArguments(inside)) {
            for (std::string_view word : SplitWords(piece)) arguments.push_back(word);
        }
        auto bad = [&]() { return Fail("unreadable arguments for \"" + name + "\": \"" + std::string(inside) + "\""); };

        if (name == "scale" || name == "scalex" || name == "scaley") {
            size_t most = name == "scale" ? 2 : 1;
            if (arguments.empty() || arguments.size() > most) return bad();
            std::optional<double> x = ParseNumber(arguments[0]);
            std::optional<double> y = arguments.size() == 2 ? ParseNumber(arguments[1]) : x;
            if (!x || !y) return bad();
            if (name != "scaley") scaleX *= *x;
            if (name != "scalex") scaleY *= *y;
        } else if (name == "rotate") {
            std::optional<double> degrees = arguments.size() == 1 ? ParseDegrees(arguments[0]) : std::nullopt;
            if (!degrees) return Fail("rotate() takes an angle in degrees, like rotate(5deg)");
            rotation += *degrees;
        } else if (name == "translate" || name == "translatex" || name == "translatey") {
            size_t most = name == "translate" ? 2 : 1;
            if (arguments.empty() || arguments.size() > most) return bad();
            std::optional<double> x = ParsePixels(arguments[0]);
            std::optional<double> y = arguments.size() == 2 ? ParsePixels(arguments[1]) : std::optional<double>(0.0);
            if (!x || !y) return bad();
            if (name == "translatey") {
                translateY += *x;
            } else {
                translateX += *x;
                translateY += *y;
            }
        } else {
            return Fail("unknown transform \"" + name + "\" (scale, rotate, translate)");
        }
    }
    if (!any) return Fail("empty transform");
    return Ok(ValueKind::Transform, FormatNumber(scaleX) + "," + FormatNumber(scaleY) + "," + FormatNumber(rotation) +
                                        "," + FormatNumber(translateX) + "," + FormatNumber(translateY));
}

// One or two percentages or keywords, x then y: `50% 50%`, `center`, `left top`. RenderTransformOrigin is relative
// to the element, so pixels are not accepted.
Conversion ConvertTransformOrigin(std::string_view raw) {
    std::vector<std::string_view> words = SplitWords(TrimSpace(raw));
    auto fail = [&]() {
        return Fail("unreadable origin: \"" + std::string(TrimSpace(raw)) +
                    "\" (percentages, or left, center, right, top, bottom)");
    };
    if (words.empty() || words.size() > 2) return fail();

    // A keyword says which axis it is on; a percentage takes the axis of its place.
    struct Part {
        double value;
        int axis;  // 0 either, 1 horizontal, 2 vertical
    };
    std::vector<Part> parts;
    for (std::string_view word : words) {
        std::string lowered = ToLowerAscii(word);
        if (lowered == "left") parts.push_back({0, 1});
        else if (lowered == "right") parts.push_back({1, 1});
        else if (lowered == "top") parts.push_back({0, 2});
        else if (lowered == "bottom") parts.push_back({1, 2});
        else if (lowered == "center") parts.push_back({0.5, 0});
        else if (std::optional<double> percent = ParsePercent(word)) parts.push_back({*percent, 0});
        else return fail();
    }
    if (parts.size() == 1) parts.push_back({0.5, 0});
    if (parts[0].axis == 2 || parts[1].axis == 1) std::swap(parts[0], parts[1]);
    if (parts[0].axis == 2 || parts[1].axis == 1) return fail();  // two keywords on the same axis
    return Ok(ValueKind::Point, FormatFraction(parts[0].value) + "," + FormatFraction(parts[1].value));
}

// A depth in pixels: the ThemeShadow of WinUI grows with the Z of the element. `none` or 0: no shadow.
Conversion ConvertShadow(std::string_view raw) {
    std::string_view text = TrimSpace(raw);
    if (ToLowerAscii(text) == "none") return Ok(ValueKind::Shadow, "0");
    std::optional<double> depth = ParsePixels(text);
    if (!depth) {
        return Fail("unreadable shadow depth: \"" + std::string(text) + "\" (a number of pixels, like 16px)");
    }
    if (*depth < 0 || *depth > 100) return Fail("shadow depth outside 0 to 100: \"" + std::string(text) + "\"");
    return Ok(ValueKind::Shadow, FormatNumber(*depth));
}

// `filter`: a list of functions, as in CSS. `blur(4px)` is the standard deviation of a Gaussian blur (XAML has none:
// src/win/element_blur.*); Ampify's `edges(30%)` or `edges(10% 50%)` after it blurs only towards the top and bottom,
// over those shares of the height, sharp in the middle (a column of lyrics around the sung line). The colour filters
// (saturate, grayscale, sepia, brightness, contrast, invert, hue-rotate, and Ampify's tint and duotone) fold into one
// colour matrix (color_matrix.hpp), applied in the order written. ValueKind::Blur says how it is written.
Conversion ConvertFilter(std::string_view raw, const ValueContext& context) {
    const std::string text(TrimSpace(raw));
    if (ToLowerAscii(text) == "none") return Ok(ValueKind::Blur, "0");
    FilterParts parts;
    for (std::string_view item : SplitArguments(text, ' ', true)) {
        if (std::optional<std::string> problem = ReadFilterItem(item, context, parts)) {
            return Fail(*problem + " in filter \"" + text +
                        "\" (blur(<px>) [edges(<%>)], saturate, grayscale, sepia, brightness, contrast, invert, "
                        "hue-rotate, tint(<color>[, <amount>]), duotone(<dark>, <light>), or none)");
        }
    }
    const bool edged = !parts.shares.empty();
    const bool coloured = !IsIdentity(parts.matrix);
    std::string result = FormatNumber(parts.radius.value_or(0));
    if (edged || coloured) {
        result += "," + FormatNumber(edged ? parts.shares.front() : 0) + "," +
                  FormatNumber(edged ? parts.shares.back() : 0);
    }
    if (coloured) result += "," + FormatMatrix(parts.matrix);
    return Ok(ValueKind::Blur, result);
}

// `overflow: hidden` cuts the element to its own rectangle; `clip-path: inset(...)` to a rectangle inset from its
// edges, in pixels and in CSS order. XAML clips to rectangles only: no `round`, circle or polygon.
Conversion ConvertClip(const PropertySpec& spec, std::string_view raw) {
    std::string lowered = ToLowerAscii(TrimSpace(raw));
    if (spec.css == "overflow") {
        if (lowered == "hidden" || lowered == "clip") return Ok(ValueKind::Clip, "0,0,0,0");
        if (lowered == "visible") return Ok(ValueKind::Clip, "");
        return Fail("unreadable overflow: \"" + std::string(TrimSpace(raw)) +
                    "\" (hidden, clip or visible; scrolling is Apple Music's own)");
    }
    if (lowered == "none") return Ok(ValueKind::Clip, "");
    std::optional<std::string_view> inside = FunctionArguments(raw, "inset");
    if (!inside) {
        return Fail("unreadable clip-path: \"" + std::string(TrimSpace(raw)) + "\" (only inset(...) or none)");
    }
    if (ToLowerAscii(*inside).find("round") != std::string::npos) {
        return Fail("clip-path: inset(... round) is not available: put corner-radius on the element instead");
    }
    Conversion insets = ConvertThickness(*inside, true);
    if (!insets.value) return Fail("clip-path: " + insets.problem + " (pixels only)");
    return Ok(ValueKind::Clip, insets.value->text);
}

// `transform 200ms ease, opacity .15s`: which changes animate, and for how long. The easing is Ampify's own (a quick
// ease-out); easing words and delays are accepted and ignored. Only what XAML animates cheaply is offered, and
// `width`, which glides the widths Apple writes (the smooth progress line): named on its own, never by `all`, since
// it runs on the interface thread, frame by frame.
Conversion ConvertTransition(std::string_view raw) {
    constexpr double kMaxMs = 5000;
    constexpr size_t kAll = 4;  // `all` stops before width
    std::string lowered = ToLowerAscii(TrimSpace(raw));
    if (lowered == "none") return Ok(ValueKind::Transition, "");
    std::array<std::optional<double>, 5> durations;  // transform, opacity, background, filter, width
    constexpr std::array<std::string_view, 5> kNames{"transform", "opacity", "background", "filter", "width"};
    std::string_view rest = lowered;
    while (!rest.empty()) {
        size_t comma = rest.find(',');
        std::string_view item = TrimSpace(rest.substr(0, comma));
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        std::vector<std::string_view> words = SplitWords(item);
        if (words.empty()) return Fail("empty transition");
        std::optional<double> ms;
        for (size_t i = 1; i < words.size() && !ms; i++) {
            std::string_view word = words[i];
            double scale = word.ends_with("ms") ? 1 : (word.ends_with("s") ? 1000 : 0);
            if (scale == 0) continue;
            std::optional<double> number = ParseNumber(word.substr(0, word.size() - (scale == 1 ? 2 : 1)));
            if (number) ms = *number * scale;
        }
        if (!ms) return Fail("transition without a duration: \"" + std::string(item) + "\" (like transform 200ms)");
        if (*ms < 0 || *ms > kMaxMs) return Fail("transition duration outside 0 to 5 s: \"" + std::string(item) + "\"");
        bool known = false;
        for (size_t k = 0; k < kNames.size(); k++) {
            if (words[0] == kNames[k] || (words[0] == "all" && k < kAll)) {
                durations[k] = *ms;
                known = true;
            }
        }
        if (!known) {
            return Fail("transition animates transform, opacity, background, filter, width or all, not \"" +
                        std::string(words[0]) + "\"");
        }
    }
    std::string text;
    for (size_t k = 0; k < kNames.size(); k++) {
        if (!durations[k]) continue;
        text += (text.empty() ? "" : ";") + std::string(kNames[k]) + ":" + FormatNumber(*durations[k]);
    }
    return Ok(ValueKind::Transition, text);
}

// `content` of ::before or ::after: an image of the theme folder, as for a background, or none. Text, counters and
// remote addresses are not offered: the decor is an Image element.
Conversion ConvertContent(std::string_view raw, const ValueContext& context) {
    std::string_view text = TrimSpace(raw);
    std::string lowered = ToLowerAscii(text);
    if (lowered == "none" || lowered == "normal") return Ok(ValueKind::Image, "");
    if (lowered.starts_with("bars(") && lowered.back() == ')') return ConvertBars(lowered, text);
    if (lowered.starts_with("artwork") && (lowered.size() == 7 || lowered[7] == ' ')) return ConvertArtwork(text, context);
    if (lowered == "immersion") {
        // The song's artwork, as `artwork` is: off with the theme's assets.
        if (!context.allowImages) return Fail("images are turned off (assets part of the theme)");
        return Ok(ValueKind::Image, std::string(kImmersionContent));
    }
    if (!lowered.starts_with("url(")) {
        return Fail("content takes url(image), artwork, immersion, bars(n) or none: text is not supported (\"" +
                    std::string(text) + "\")");
    }
    Conversion image = ConvertImage(text, context);
    if (!image.value) return image;
    std::string path = image.value->text.substr(0, image.value->text.find('|'));
    size_t dot = path.find_last_of("./");
    std::string extension = dot == std::string::npos || path[dot] != '.' ? std::string() : ToLowerAscii(path.substr(dot));
    if (std::find(kDecorImageTypes.begin(), kDecorImageTypes.end(), extension) == kDecorImageTypes.end()) {
        return Fail("unsupported image for content: " + path + " (png, jpg, gif, bmp, webp or ico)");
    }
    return image;
}

Conversion ConvertTemplate(std::string_view raw, const ValueContext& context) {
    std::optional<std::string> path = UrlArgument(raw);
    if (!path) return Fail("a template is written url(file.xaml)");
    if (!IsSafeRelativePath(*path)) return Fail("template path not allowed: " + *path);

    std::optional<std::string> xaml = context.files.Read(*path);
    if (!xaml) return Fail("template not found: " + *path);
    if (xaml->find('<') == std::string::npos) return Fail("the template \"" + *path + "\" is not XAML");
    return Ok(ValueKind::Template, std::move(*xaml));
}

}  // namespace detail

std::string WriteBarsContent(const BarsContent& bars) {
    std::string text = std::string(kBarsContent) + std::to_string(bars.count);
    if (bars.shape == BarShape::Bars && bars.reach == 100 && !bars.thick) return text;
    auto named = std::find_if(kBarShapes.begin(), kBarShapes.end(), [&](const auto& s) { return s.second == bars.shape; });
    return text + ":" + std::string(named->first) + ":" + std::to_string(bars.reach) + (bars.thick ? ":thick" : "");
}

std::optional<BarsContent> ReadBarsContent(std::string_view image) {
    if (!image.starts_with(kBarsContent)) return std::nullopt;
    std::vector<std::string_view> parts = SplitTopLevel(image.substr(kBarsContent.size()), ':');
    const auto number = [](std::string_view text, int& out) {
        auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
        return error == std::errc() && end == text.data() + text.size();
    };
    BarsContent bars;
    if ((parts.size() != 1 && parts.size() != 3 && parts.size() != 4) || !number(parts[0], bars.count)) {
        return std::nullopt;
    }
    bars.count = std::clamp(bars.count, kMinBars, kMaxBars);
    if (parts.size() == 1) return bars;
    auto known = std::find_if(kBarShapes.begin(), kBarShapes.end(), [&](const auto& s) { return s.first == parts[1]; });
    if (known == kBarShapes.end() || !number(parts[2], bars.reach)) return std::nullopt;
    if (parts.size() == 4 && parts[3] != "thick") return std::nullopt;
    bars.thick = parts.size() == 4;
    bars.shape = known->second;
    bars.reach = std::clamp(bars.reach, kMinBarsReach, 100);
    return bars;
}

}  // namespace ampify::theme
