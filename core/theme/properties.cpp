#include "properties.hpp"

#include <array>
#include <cctype>
#include <cmath>

namespace amk::theme {

namespace {

constexpr std::array<PropertySpec, 19> kProperties{{
    {"background", "Background", PropertyKind::Brush},
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

Conversion ConvertBrush(std::string_view raw, const ValueContext& context) {
    std::string_view text = Trimmed(raw);
    if (text.empty()) return Fail("couleur vide");

    if (std::optional<std::string> path = UrlArgument(text)) {
        if (!context.allowImages) return Fail("images désactivées (partie assets du thème)");
        if (!IsSafeRelativePath(*path)) return Fail("chemin d'image non autorisé : " + *path);
        if (!context.files.Exists(*path)) return Fail("image introuvable : " + *path);
        return Ok(ValueKind::Image, context.folder + "/" + *path);
    }

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

    // XAML order is left, top, right, bottom.
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

Conversion ConvertText(std::string_view raw) {
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
        case PropertyKind::Length:
            return ConvertLength(raw);
        case PropertyKind::Thickness:
            return ConvertThickness(raw);
        case PropertyKind::Visibility:
            return ConvertVisibility(raw);
        case PropertyKind::Number:
            return ConvertNumber(spec, raw);
        case PropertyKind::Text:
            return ConvertText(raw);
        case PropertyKind::Template:
            return ConvertTemplate(raw, context);
        case PropertyKind::FlowDirection:
            return ConvertFlowDirection(raw);
        case PropertyKind::Order:
            return ConvertOrder(raw);
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
