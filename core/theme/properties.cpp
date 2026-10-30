#include "properties.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <vector>

namespace amk::theme {

namespace {

constexpr std::array<PropertySpec, 30> kProperties{{
    {"background", "Background", PropertyKind::Brush},
    {"backdrop", "Background", PropertyKind::Backdrop},
    {"color", "Foreground", PropertyKind::Brush},
    {"foreground", "Foreground", PropertyKind::Brush},
    {"border-color", "BorderBrush", PropertyKind::Brush},
    {"border-width", "BorderThickness", PropertyKind::Thickness},
    {"corner-radius", "CornerRadius", PropertyKind::Thickness},
    {"font-family", "FontFamily", PropertyKind::Text},
    {"font-size", "FontSize", PropertyKind::Length},
    {"font-weight", "FontWeight", PropertyKind::Number},
    {"margin", "Margin", PropertyKind::Thickness},
    {"padding", "Padding", PropertyKind::Thickness},
    {"width", "Width", PropertyKind::Length},
    {"height", "Height", PropertyKind::Length},
    {"opacity", "Opacity", PropertyKind::Number},
    {"visibility", "Visibility", PropertyKind::Visibility},
    {"template", "Template", PropertyKind::Template},
    {"flow-direction", "FlowDirection", PropertyKind::FlowDirection},
    {"direction", "FlowDirection", PropertyKind::FlowDirection},
    {"order", kOrderProperty, PropertyKind::Order},
    {"horizontal-alignment", "HorizontalAlignment", PropertyKind::Alignment},
    {"vertical-alignment", "VerticalAlignment", PropertyKind::Alignment},
    {"grid-row", "Grid.Row", PropertyKind::Integer},
    {"grid-column", "Grid.Column", PropertyKind::Integer},
    {"grid-row-span", "Grid.RowSpan", PropertyKind::Integer},
    {"grid-column-span", "Grid.ColumnSpan", PropertyKind::Integer},
    {"z-index", "Canvas.ZIndex", PropertyKind::Integer},
    {"transform", "RenderTransform", PropertyKind::Transform},
    {"transform-origin", "RenderTransformOrigin", PropertyKind::TransformOrigin},
    {"box-shadow", "Shadow", PropertyKind::Shadow},
}};

std::string_view Trimmed(std::string_view text) {
    size_t first = 0;
    size_t last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) first++;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) last--;
    return text.substr(first, last - first);
}

std::string Lowered(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

Conversion Fail(std::string problem) {
    return {std::nullopt, std::move(problem)};
}

Conversion Ok(ValueKind kind, std::string text) {
    return {Value{kind, std::move(text)}, {}};
}

// `url(path)` with optional quotes. Nothing when the value is not a url.
std::optional<std::string> UrlArgument(std::string_view raw) {
    std::string_view text = Trimmed(raw);
    if (Lowered(text.substr(0, 4)) != "url(" || text.back() != ')') return std::nullopt;

    std::string_view inside = Trimmed(text.substr(4, text.size() - 5));
    if (inside.size() >= 2 && (inside.front() == '"' || inside.front() == '\'') && inside.back() == inside.front()) {
        inside = inside.substr(1, inside.size() - 2);
    }
    return std::string(inside);
}

std::optional<double> ParseNumber(std::string_view text) {
    double value = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    auto [end, error] = std::from_chars(first, last, value);
    if (error != std::errc() || end != last || !std::isfinite(value)) return std::nullopt;
    return value;
}

// Strips a trailing `px`, the only unit the engine accepts.
std::optional<double> ParsePixels(std::string_view text) {
    if (Lowered(text).ends_with("px")) text.remove_suffix(2);
    return ParseNumber(Trimmed(text));
}

// A number followed by `%`, as a fraction: `50%` is 0.5.
std::optional<double> ParsePercent(std::string_view text) {
    text = Trimmed(text);
    if (text.empty() || text.back() != '%') return std::nullopt;
    std::optional<double> number = ParseNumber(Trimmed(text.substr(0, text.size() - 1)));
    if (!number) return std::nullopt;
    return *number / 100;
}

// An angle in degrees: `135deg`, or `0` alone, as CSS allows.
std::optional<double> ParseDegrees(std::string_view text) {
    text = Trimmed(text);
    if (Lowered(text).ends_with("deg")) return ParseNumber(Trimmed(text.substr(0, text.size() - 3)));
    if (text == "0") return 0.0;
    return std::nullopt;
}

// `name(arguments)` taking the whole text, the name in any case. Nothing for another form.
std::optional<std::string_view> FunctionArguments(std::string_view raw, std::string_view name) {
    std::string_view text = Trimmed(raw);
    if (text.size() < name.size() + 2 || Lowered(text.substr(0, name.size())) != name) return std::nullopt;
    std::string_view rest = Trimmed(text.substr(name.size()));
    if (rest.size() < 2 || rest.front() != '(' || rest.back() != ')') return std::nullopt;
    return rest.substr(1, rest.size() - 2);
}

// Splits on commas outside parentheses; each piece trimmed.
std::vector<std::string_view> SplitArguments(std::string_view text) {
    std::vector<std::string_view> pieces;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i == text.size() || (text[i] == ',' && depth == 0)) {
            pieces.push_back(Trimmed(text.substr(start, i - start)));
            start = i + 1;
        } else if (text[i] == '(') {
            depth++;
        } else if (text[i] == ')') {
            depth--;
        }
    }
    return pieces;
}

// Splits on whitespace; empty pieces are dropped.
std::vector<std::string_view> SplitWords(std::string_view text) {
    std::vector<std::string_view> words;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) i++;
        size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) i++;
        if (i > start) words.push_back(text.substr(start, i - start));
    }
    return words;
}

// Four decimals are plenty for a point relative to an element, and keep the text the same from one load to the next.
std::string FormatFraction(double number) {
    number = std::round(number * 10000) / 10000;
    if (number == 0) number = 0;  // no "-0"
    return FormatNumber(number);
}

// A plain colour: `#hex`, `$palette` name or `transparent`.
Conversion ConvertColor(std::string_view raw, const ValueContext& context) {
    std::string_view text = Trimmed(raw);
    if (text.empty()) return Fail("couleur vide");

    if (text.front() == '$') {
        std::optional<Argb> color = context.palette.Find(text.substr(1));
        if (!color) return Fail("couleur « " + std::string(text) + " » absente de [colors]");
        return Ok(ValueKind::Color, ToXamlColor(*color));
    }

    if (Lowered(text) == "transparent") return Ok(ValueKind::Color, "#00FFFFFF");

    std::optional<Argb> color = ParseColor(text);
    if (!color) return Fail("couleur illisible : « " + std::string(text) + " »");
    return Ok(ValueKind::Color, ToXamlColor(*color));
}

// The direction of `to <side>` as an angle: to top is 0deg, to right 90deg, clockwise as in CSS.
std::optional<double> SideAngle(std::string_view text) {
    std::string lowered = Lowered(text);
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
        std::string head = Lowered(pieces[0]);
        if (head.starts_with("to ")) {
            std::optional<double> side = SideAngle(pieces[0]);
            if (!side) return Fail("direction de dégradé illisible : « " + std::string(pieces[0]) + " »");
            angle = *side;
            first = 1;
        } else if (!head.empty() && head.front() != '#' && head.front() != '$' && !head.starts_with("transparent")) {
            std::optional<double> degrees = ParseDegrees(pieces[0]);
            if (!degrees) {
                return Fail("angle de dégradé illisible : « " + std::string(pieces[0]) + " » (en degrés, comme 135deg)");
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
            return Fail("couleur de dégradé illisible : « " + std::string(pieces[i]) + " »");
        }
        Conversion color = ConvertColor(words[0], context);
        if (!color.value) return color;
        Stop stop{color.value->text, std::nullopt};
        if (words.size() == 2) {
            stop.offset = ParsePercent(words[1]);
            if (!stop.offset || *stop.offset < 0 || *stop.offset > 1) {
                return Fail("position de dégradé illisible : « " + std::string(words[1]) + " » (de 0% à 100%)");
            }
        }
        stops.push_back(std::move(stop));
    }
    if (stops.size() < 2) return Fail("un dégradé demande au moins deux couleurs");

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

// url(image), and an optional fit after it: cover, contain or fill.
Conversion ConvertImage(std::string_view text, const ValueContext& context) {
    size_t close = text.find(')');
    std::optional<std::string> path =
        close == std::string_view::npos ? std::nullopt : UrlArgument(text.substr(0, close + 1));
    if (!path) return Fail("image illisible : « " + std::string(text) + " »");
    if (!context.allowImages) return Fail("images désactivées (partie assets du thème)");
    if (!IsSafeRelativePath(*path)) return Fail("chemin d'image non autorisé : " + *path);
    if (!context.files.Exists(*path)) return Fail("image introuvable : " + *path);

    std::string fit = Lowered(Trimmed(text.substr(close + 1)));
    std::string stretch;
    if (fit == "cover") {
        stretch = "|UniformToFill";
    } else if (fit == "contain") {
        stretch = "|Uniform";
    } else if (fit == "fill") {
        stretch = "|Fill";
    } else if (!fit.empty()) {
        return Fail("ajustement d'image inconnu : « " + fit + " » (cover, contain ou fill)");
    }
    return Ok(ValueKind::Image, context.folder + "/" + *path + stretch);
}

Conversion ConvertBrush(std::string_view raw, const ValueContext& context) {
    std::string_view text = Trimmed(raw);
    if (text.empty()) return Fail("couleur vide");

    if (Lowered(text.substr(0, 4)) == "url(") return ConvertImage(text, context);
    if (std::optional<std::string_view> inside = FunctionArguments(text, "linear-gradient")) {
        return ConvertGradient(*inside, context);
    }
    return ConvertColor(text, context);
}

// acrylic(<tint>, <opacity>): the in-app acrylic of WinUI, a tint over a blur of what lies behind the element.
Conversion ConvertBackdrop(std::string_view raw, const ValueContext& context) {
    std::optional<std::string_view> inside = FunctionArguments(raw, "acrylic");
    if (!inside) return Fail("un fond s'écrit acrylic(teinte, opacité)");
    std::vector<std::string_view> pieces = SplitArguments(*inside);
    if (pieces.size() != 2) return Fail("acrylic(teinte, opacité) attend deux valeurs");

    Conversion tint = ConvertColor(pieces[0], context);
    if (!tint.value) return tint;
    std::optional<double> opacity = ParseNumber(pieces[1]);
    if (!opacity || *opacity < 0 || *opacity > 1) {
        return Fail("opacité d'acrylique hors de 0 à 1 : « " + std::string(pieces[1]) + " »");
    }
    return Ok(ValueKind::Acrylic, tint.value->text + ";" + FormatNumber(*opacity));
}

Conversion ConvertLength(std::string_view raw) {
    std::string_view text = Trimmed(raw);
    if (Lowered(text) == "auto") return Ok(ValueKind::Length, "NaN");

    std::optional<double> pixels = ParsePixels(text);
    if (!pixels) return Fail("longueur illisible : « " + std::string(text) + " »");
    return Ok(ValueKind::Length, FormatNumber(*pixels));
}

Conversion ConvertThickness(std::string_view raw) {
    std::string_view text = Trimmed(raw);
    std::array<std::string, 4> parts;  // CSS order: top, right, bottom, left
    size_t count = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view piece = text.substr(start, end - start);
        start = end + 1;
        if (piece.empty()) continue;

        std::optional<double> pixels = ParsePixels(piece);
        if (!pixels) return Fail("épaisseur illisible : « " + std::string(piece) + " »");
        if (count == parts.size()) return Fail("plus de quatre valeurs : « " + std::string(text) + " »");
        parts[count++] = FormatNumber(*pixels);
    }
    if (count == 0) return Fail("valeur vide");

    // Expand the CSS shorthand: a missing side takes the value of the opposite side.
    std::string top = parts[0];
    std::string right = count >= 2 ? parts[1] : top;
    std::string bottom = count >= 3 ? parts[2] : top;
    std::string left = count >= 4 ? parts[3] : right;

    // XAML order is left, top, right, bottom. For corner-radius, whose CSS order is top-left, top-right, bottom-right,
    // bottom-left, the same expansion gives bottom-left, top-left, top-right, bottom-right (xaml_backend.cpp).
    return Ok(ValueKind::Thickness, left + "," + top + "," + right + "," + bottom);
}

Conversion ConvertVisibility(std::string_view raw) {
    std::string value = Lowered(Trimmed(raw));
    if (value == "visible") return Ok(ValueKind::Visibility, "Visible");
    if (value == "collapsed" || value == "hidden" || value == "none") return Ok(ValueKind::Visibility, "Collapsed");
    return Fail("visibilité inconnue : « " + std::string(Trimmed(raw)) + " » (visible ou collapsed)");
}

Conversion ConvertNumber(const PropertySpec& spec, std::string_view raw) {
    std::string value = Lowered(Trimmed(raw));
    if (spec.css == "font-weight") {
        if (value == "normal") return Ok(ValueKind::Number, "400");
        if (value == "bold") return Ok(ValueKind::Number, "700");
    }

    std::optional<double> number = ParseNumber(value);
    if (!number) return Fail("nombre illisible : « " + std::string(Trimmed(raw)) + " »");
    if (spec.css == "opacity" && (*number < 0 || *number > 1)) return Fail("opacité hors de 0 à 1");
    if (spec.css == "font-weight" && (*number < 1 || *number > 1000)) return Fail("graisse hors de 1 à 1000");
    return Ok(ValueKind::Number, FormatNumber(*number));
}

// url(fonts/file.ttf#Family Name): a font shipped in the theme folder. The value is the file URI and the family, as
// XAML's FontFamily reads it, the way images are given by path (an ms-appx address would point into Apple Music's
// package, not into the theme).
Conversion ConvertFontFile(std::string_view argument, const ValueContext& context) {
    size_t hash = argument.find('#');
    if (hash == std::string_view::npos || Trimmed(argument.substr(hash + 1)).empty()) {
        return Fail("nom de famille manquant après « # » (url(fonts/fichier.ttf#Nom de la police))");
    }
    std::string path(Trimmed(argument.substr(0, hash)));
    std::string family(Trimmed(argument.substr(hash + 1)));
    if (!context.allowImages) return Fail("polices désactivées (partie assets du thème)");
    if (!IsSafeRelativePath(path)) return Fail("chemin de police non autorisé : " + path);
    size_t dot = path.find_last_of('.');
    std::string extension = dot == std::string::npos ? std::string() : Lowered(std::string_view(path).substr(dot));
    if (extension != ".ttf" && extension != ".otf" && extension != ".ttc") {
        return Fail("police non prise en charge : " + path + " (ttf, otf ou ttc)");
    }
    if (!context.files.Exists(path)) return Fail("police introuvable : " + path);

    std::string file = context.folder + "/" + path;
    return Ok(ValueKind::Text, (file.starts_with("/") ? "file://" : "file:///") + file + "#" + family);
}

Conversion ConvertText(std::string_view raw, const ValueContext& context) {
    if (std::optional<std::string> argument = UrlArgument(raw)) return ConvertFontFile(*argument, context);

    std::string_view text = Trimmed(raw);
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front()) {
        text = text.substr(1, text.size() - 2);
    }
    if (text.empty()) return Fail("nom de police vide");
    return Ok(ValueKind::Text, std::string(text));
}

// The direction in which text and children are laid out. A right-to-left language turns the whole page around.
Conversion ConvertFlowDirection(std::string_view raw) {
    std::string value = Lowered(Trimmed(raw));
    if (value == "ltr" || value == "left-to-right" || value == "lefttoright") return Ok(ValueKind::Keyword, "LeftToRight");
    if (value == "rtl" || value == "right-to-left" || value == "righttoleft") return Ok(ValueKind::Keyword, "RightToLeft");
    return Fail("sens inconnu : « " + std::string(Trimmed(raw)) + " » (ltr ou rtl)");
}

Conversion ConvertOrder(std::string_view raw) {
    std::string_view text = Trimmed(raw);
    if (text.empty() || text.size() > 4 || text.find_first_not_of("0123456789") != std::string_view::npos) {
        return Fail("position illisible : « " + std::string(text) + " » (un nombre entier à partir de 0)");
    }
    return Ok(ValueKind::Number, std::to_string(std::stoi(std::string(text))));
}

// HorizontalAlignment or VerticalAlignment, by the CSS name of the property.
Conversion ConvertAlignment(const PropertySpec& spec, std::string_view raw) {
    std::string value = Lowered(Trimmed(raw));
    bool vertical = spec.css == "vertical-alignment";
    if (value == "center") return Ok(ValueKind::Keyword, "Center");
    if (value == "stretch") return Ok(ValueKind::Keyword, "Stretch");
    if (!vertical && value == "left") return Ok(ValueKind::Keyword, "Left");
    if (!vertical && value == "right") return Ok(ValueKind::Keyword, "Right");
    if (vertical && value == "top") return Ok(ValueKind::Keyword, "Top");
    if (vertical && value == "bottom") return Ok(ValueKind::Keyword, "Bottom");
    return Fail("alignement inconnu : « " + std::string(Trimmed(raw)) + " » (" +
                (vertical ? "top, center, bottom" : "left, center, right") + " ou stretch)");
}

// A Grid row or column from 0, a span from 1, a z-index that may be negative.
Conversion ConvertInteger(const PropertySpec& spec, std::string_view raw) {
    std::string_view text = Trimmed(raw);
    std::string_view digits = !text.empty() && text.front() == '-' ? text.substr(1) : text;
    if (digits.empty() || digits.size() > 6 || digits.find_first_not_of("0123456789") != std::string_view::npos) {
        return Fail("nombre entier illisible : « " + std::string(text) + " »");
    }
    int number = std::stoi(std::string(text));
    int minimum = spec.css == "z-index" ? -999999 : spec.css.ends_with("-span") ? 1 : 0;
    if (number < minimum) {
        return Fail("« " + std::string(spec.css) + " » commence à " + std::to_string(minimum) + " : « " +
                    std::string(text) + " »");
    }
    return Ok(ValueKind::Number, std::to_string(number));
}

// scale(x[, y]) rotate(<deg>) translate(x[, y]), and scaleX, scaleY, translateX, translateY: one CompositeTransform.
// XAML applies its parts in a fixed order (scale, then rotation, then translation), whatever the order written.
Conversion ConvertTransform(std::string_view raw) {
    std::string_view text = Trimmed(raw);
    if (Lowered(text) == "none") return Ok(ValueKind::Transform, "1,1,0,0,0");

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
        if (close == std::string_view::npos) return Fail("transformation illisible : « " + std::string(text) + " »");
        std::string name = Lowered(Trimmed(text.substr(i, open - i)));
        std::string_view inside = text.substr(open + 1, close - open - 1);
        i = close + 1;
        any = true;

        // Arguments are separated by commas, spaces, or both.
        std::vector<std::string_view> arguments;
        for (std::string_view piece : SplitArguments(inside)) {
            for (std::string_view word : SplitWords(piece)) arguments.push_back(word);
        }
        auto bad = [&]() { return Fail("arguments de « " + name + " » illisibles : « " + std::string(inside) + " »"); };

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
            if (!degrees) return Fail("rotate() attend un angle en degrés, comme rotate(5deg)");
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
            return Fail("transformation « " + name + " » inconnue (scale, rotate, translate)");
        }
    }
    if (!any) return Fail("transformation vide");
    return Ok(ValueKind::Transform, FormatNumber(scaleX) + "," + FormatNumber(scaleY) + "," + FormatNumber(rotation) +
                                        "," + FormatNumber(translateX) + "," + FormatNumber(translateY));
}

// One or two percentages or keywords, x then y: `50% 50%`, `center`, `left top`. RenderTransformOrigin is relative
// to the element, so pixels are not accepted.
Conversion ConvertTransformOrigin(std::string_view raw) {
    std::vector<std::string_view> words = SplitWords(Trimmed(raw));
    auto fail = [&]() {
        return Fail("origine illisible : « " + std::string(Trimmed(raw)) +
                    " » (pourcentages, ou left, center, right, top, bottom)");
    };
    if (words.empty() || words.size() > 2) return fail();

    // A keyword says which axis it is on; a percentage takes the axis of its place.
    struct Part {
        double value;
        int axis;  // 0 either, 1 horizontal, 2 vertical
    };
    std::vector<Part> parts;
    for (std::string_view word : words) {
        std::string lowered = Lowered(word);
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
    std::string_view text = Trimmed(raw);
    if (Lowered(text) == "none") return Ok(ValueKind::Shadow, "0");
    std::optional<double> depth = ParsePixels(text);
    if (!depth) {
        return Fail("profondeur d'ombre illisible : « " + std::string(text) + " » (un nombre de pixels, comme 16px)");
    }
    if (*depth < 0 || *depth > 100) return Fail("profondeur d'ombre hors de 0 à 100 : « " + std::string(text) + " »");
    return Ok(ValueKind::Shadow, FormatNumber(*depth));
}

Conversion ConvertTemplate(std::string_view raw, const ValueContext& context) {
    std::optional<std::string> path = UrlArgument(raw);
    if (!path) return Fail("un modèle s'écrit url(fichier.xaml)");
    if (!IsSafeRelativePath(*path)) return Fail("chemin de modèle non autorisé : " + *path);

    std::optional<std::string> xaml = context.files.Read(*path);
    if (!xaml) return Fail("modèle introuvable : " + *path);
    if (xaml->find('<') == std::string::npos) return Fail("le modèle « " + *path + " » n'est pas du XAML");
    return Ok(ValueKind::Template, std::move(*xaml));
}

}  // namespace

std::span<const PropertySpec> KnownProperties() {
    return kProperties;
}

const PropertySpec* FindProperty(std::string_view css) {
    std::string key = Lowered(css);
    for (const PropertySpec& spec : KnownProperties()) {
        if (spec.css == key) return &spec;
    }
    return nullptr;
}

Conversion ConvertValue(const PropertySpec& spec, std::string_view raw, const ValueContext& context) {
    switch (spec.kind) {
        case PropertyKind::Brush:
            return ConvertBrush(raw, context);
        case PropertyKind::Backdrop:
            return ConvertBackdrop(raw, context);
        case PropertyKind::Length:
            return ConvertLength(raw);
        case PropertyKind::Thickness:
            return ConvertThickness(raw);
        case PropertyKind::Visibility:
            return ConvertVisibility(raw);
        case PropertyKind::Number:
            return ConvertNumber(spec, raw);
        case PropertyKind::Text:
            return ConvertText(raw, context);
        case PropertyKind::Template:
            return ConvertTemplate(raw, context);
        case PropertyKind::FlowDirection:
            return ConvertFlowDirection(raw);
        case PropertyKind::Order:
            return ConvertOrder(raw);
        case PropertyKind::Alignment:
            return ConvertAlignment(spec, raw);
        case PropertyKind::Integer:
            return ConvertInteger(spec, raw);
        case PropertyKind::Transform:
            return ConvertTransform(raw);
        case PropertyKind::TransformOrigin:
            return ConvertTransformOrigin(raw);
        case PropertyKind::Shadow:
            return ConvertShadow(raw);
    }
    return Fail("type de propriété inconnu");
}

std::string FormatNumber(double number) {
    std::array<char, 32> buffer{};
    auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    if (error != std::errc()) return "0";
    return std::string(buffer.data(), end);
}

}  // namespace amk::theme
