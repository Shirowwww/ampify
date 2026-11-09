#include "properties.hpp"

#include "properties_detail.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <vector>

#include "../text_util.hpp"
#include "color_matrix.hpp"
#include "colour_keys.hpp"
#include "css.hpp"
#include "css_values.hpp"
#include "value_codec.hpp"

namespace ampify::theme {

using namespace detail;

namespace {

constexpr std::array<PropertySpec, 60> kProperties{{
    {"background", "Background", PropertyKind::Brush},
    {"backdrop", "Background", PropertyKind::Backdrop},
    {"color", "Foreground", PropertyKind::Brush},
    {"foreground", "Foreground", PropertyKind::Brush},
    {"border-color", "BorderBrush", PropertyKind::Brush},
    // A row of a list (ListViewItemPresenter) draws its light under the pointer and its selection itself, from these
    // five, and not from a state.
    {"hover-background", "PointerOverBackground", PropertyKind::Brush},
    {"active-background", "PressedBackground", PropertyKind::Brush},
    {"selected-background", "SelectedBackground", PropertyKind::Brush},
    {"selected-hover-background", "SelectedPointerOverBackground", PropertyKind::Brush},
    {"selected-active-background", "SelectedPressedBackground", PropertyKind::Brush},
    // A toggle button's text or icon while it is on (shuffle): the ToggleButtonForegroundChecked keys of the control.
    {"checked-color", kCheckedForegroundProperty, PropertyKind::Brush},
    {"border-width", "BorderThickness", PropertyKind::Thickness},
    {"corner-radius", "CornerRadius", PropertyKind::Thickness},
    {"border-radius", "CornerRadius", PropertyKind::Thickness},  // the name CSS and Spicetify themes use
    {"font-family", "FontFamily", PropertyKind::Text},
    {"font-size", "FontSize", PropertyKind::Length},
    {"font-weight", "FontWeight", PropertyKind::Number},
    {"font-style", "FontStyle", PropertyKind::Keyword},
    {"letter-spacing", "CharacterSpacing", PropertyKind::LetterSpacing},
    {"line-height", "LineHeight", PropertyKind::LineHeight},
    {"text-align", "TextAlignment", PropertyKind::Keyword},
    {"text-overflow", "TextTrimming", PropertyKind::Keyword},
    {"white-space", "TextWrapping", PropertyKind::Keyword},
    {"margin", "Margin", PropertyKind::Thickness},
    {"padding", "Padding", PropertyKind::Thickness},
    {"width", "Width", PropertyKind::Length},
    {"height", "Height", PropertyKind::Length},
    {"min-width", "MinWidth", PropertyKind::Length},
    {"max-width", "MaxWidth", PropertyKind::Length},
    {"min-height", "MinHeight", PropertyKind::Length},
    {"max-height", "MaxHeight", PropertyKind::Length},
    {"compact-pane-length", "CompactPaneLength", PropertyKind::Length},  // NavigationView, the folded sidebar
    // Apple's progress line (ScrubberControl): the height and corners of its track, and its thumb, inside the control
    {"track-height", "TrackHeight", PropertyKind::Length},
    {"track-radius", "TrackRectRadius", PropertyKind::Length},
    {"thumb-visibility", "ThumbVisibility", PropertyKind::Visibility},
    {"opacity", "Opacity", PropertyKind::Number},
    {"visibility", "Visibility", PropertyKind::Visibility},
    {"display", "Visibility", PropertyKind::Visibility},  // so the `display: none` of Spicetify snippets works
    {"template", "Template", PropertyKind::Template},
    {"flow-direction", "FlowDirection", PropertyKind::FlowDirection},
    {"direction", "FlowDirection", PropertyKind::FlowDirection},
    {"color-scheme", "RequestedTheme", PropertyKind::ColorScheme},
    {"order", kOrderProperty, PropertyKind::Order},
    {"move-to", kMoveToProperty, PropertyKind::Target},
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
    {"filter", "Blur", PropertyKind::Filter},
    {"overflow", "Clip", PropertyKind::Clip},   // hidden: the element's own rectangle
    {"clip-path", "Clip", PropertyKind::Clip},  // inset(...) only
    {"transition", "Transition", PropertyKind::Transition},
    {"animation", "Animation", PropertyKind::Animation},
    {"content", kContentProperty, PropertyKind::Content},  // ::before and ::after only
}};

// What a decor takes (AppliesToDecor). Image has no CornerRadius, Padding or brush of its own; Foreground is the
// colour of bars(n), and an image ignores it. A transition animates a decor whose values change while its picture
// stays (a setting switched).
constexpr std::array<std::string_view, 22> kDecorProperties{
    kContentProperty, "Width", "Height", "MinWidth", "MaxWidth", "MinHeight", "MaxHeight", "Margin", "Opacity",
    "Visibility", "HorizontalAlignment", "VerticalAlignment", "Grid.Row", "Grid.Column", "Grid.RowSpan",
    "Grid.ColumnSpan", "Canvas.ZIndex", "RenderTransform", "RenderTransformOrigin", "Animation", "Foreground",
    "Transition",
};

// The words of the Keyword properties and the XAML member each one becomes. XAML aligns `Left` to the start of the
// line (TextAlignment.Start is the same member), so `start` is `Left` and `end` is `Right`.
struct KeywordSpec {
    std::string_view css;
    std::string_view word;
    std::string_view xaml;
};

constexpr std::array<KeywordSpec, 13> kKeywords{{
    {"text-align", "left", "Left"},
    {"text-align", "start", "Left"},
    {"text-align", "center", "Center"},
    {"text-align", "right", "Right"},
    {"text-align", "end", "Right"},
    {"text-align", "justify", "Justify"},
    {"font-style", "normal", "Normal"},
    {"font-style", "italic", "Italic"},
    {"font-style", "oblique", "Oblique"},
    {"text-overflow", "ellipsis", "CharacterEllipsis"},
    {"text-overflow", "clip", "None"},
    {"white-space", "nowrap", "NoWrap"},
    {"white-space", "normal", "Wrap"},
}};

// The values of `display` that show an element: XAML has no layout modes to switch, only Visible and Collapsed.
constexpr std::array<std::string_view, 11> kShownDisplays{
    "block", "inline", "inline-block", "flex", "inline-flex", "grid", "inline-grid", "flow-root", "contents",
    "list-item", "table",
};

constexpr std::string_view kNotInXaml = "not available in Apple Music's XAML";

struct PropertyAdvice {
    std::string_view css;
    std::string_view advice;
};

// What Spicetify themes write most that has no property of the same name here (PropertyHint).
constexpr std::array<PropertyAdvice, 29> kAdvice{{
    {"background-color", "use background"},
    {"background-image", "use background: url(images/file.png)"},
    {"backdrop-filter", "use backdrop: acrylic(tint, opacity) or liquid-glass(tint, opacity)"},
    {"-webkit-backdrop-filter", "use backdrop: acrylic(tint, opacity) or liquid-glass(tint, opacity)"},
    {"justify-content", "use horizontal-alignment on the children"},
    {"align-items", "use vertical-alignment on the children"},
    {"align-content", "use vertical-alignment on the children"},
    {"justify-self", "use horizontal-alignment"},
    {"align-self", "use vertical-alignment"},
    {"gap", "use margin on the children"},
    {"row-gap", "use margin on the children"},
    {"column-gap", "use margin on the children"},
    {"border", "use border-width and border-color"},
    {"outline", "use border-width and border-color"},
    {"font", "use font-family, font-size and font-weight, or font-role"},
    {"fill", "use color"},
    {"position", "not available: use margin, transform: translate(...) or grid-row and grid-column"},
    {"top", "not available: use margin or transform: translate(...)"},
    {"left", "not available: use margin or transform: translate(...)"},
    {"right", "not available: use margin or transform: translate(...)"},
    {"bottom", "not available: use margin or transform: translate(...)"},
    {"text-transform", "not available: XAML's TextBlock has no letter case"},
    {"text-shadow", "not available: box-shadow gives an element a depth shadow"},
    {"pointer-events", kNotInXaml},
    {"cursor", kNotInXaml},
    {"overflow-x", "use overflow: hidden, which clips both sides, or clip-path: inset(...)"},
    {"overflow-y", "use overflow: hidden, which clips both sides, or clip-path: inset(...)"},
    {"user-select", kNotInXaml},
    {"mix-blend-mode", kNotInXaml},
}};

// Letter spacing and line height beyond these are typing mistakes, and would overflow XAML's whole number.
constexpr double kMaxSpacingEm = 10;

constexpr double kMaxSpacingPx = 200;

constexpr double kMaxLineFactor = 10;

Conversion ConvertLength(std::string_view raw) {
    std::string_view text = TrimSpace(raw);
    if (ToLowerAscii(text) == "auto") return Ok(ValueKind::Length, "NaN");

    std::optional<double> pixels = ParsePixels(text);
    if (!pixels) return Fail("unreadable length: \"" + std::string(text) + "\"");
    return Ok(ValueKind::Length, FormatNumber(*pixels));
}

// Pixels, or a size relative to the one the element had before the theme (IsRelativeSize): a theme that enlarges
// every text keeps the titles above the body text.
Conversion ConvertFontSize(std::string_view raw) {
    std::string_view text = TrimSpace(raw);
    std::string lowered = ToLowerAscii(text);
    std::optional<double> factor = ParsePercent(text);
    if (!factor && lowered.ends_with("em") && !lowered.ends_with("rem")) {
        factor = ParseNumber(TrimSpace(text.substr(0, text.size() - 2)));
    }
    if (!factor) return ConvertLength(raw);
    if (!(*factor > 0 && *factor <= 10)) {
        return Fail("font size out of range: \"" + std::string(text) + "\" (above 0%, up to 1000%)");
    }
    return Ok(ValueKind::Length, FormatNumber(std::round(*factor * 10000) / 100) + "%");
}

Conversion ConvertDisplay(std::string_view raw) {
    std::string value = ToLowerAscii(TrimSpace(raw));
    if (value == "none") return Ok(ValueKind::Visibility, std::string(kCollapsed));
    if (std::find(kShownDisplays.begin(), kShownDisplays.end(), value) != kShownDisplays.end()) {
        return Ok(ValueKind::Visibility, std::string(kVisible));
    }
    return Fail("unknown display: \"" + std::string(TrimSpace(raw)) +
                "\" (none hides the element; block, flex, grid and the like show it)");
}

Conversion ConvertVisibility(const PropertySpec& spec, std::string_view raw) {
    if (spec.css == "display") return ConvertDisplay(raw);
    std::string value = ToLowerAscii(TrimSpace(raw));
    if (value == "visible") return Ok(ValueKind::Visibility, std::string(kVisible));
    if (value == "collapsed" || value == "hidden" || value == "none") {
        return Ok(ValueKind::Visibility, std::string(kCollapsed));
    }
    return Fail("unknown visibility: \"" + std::string(TrimSpace(raw)) + "\" (visible or collapsed)");
}

Conversion ConvertNumber(const PropertySpec& spec, std::string_view raw) {
    std::string value = ToLowerAscii(TrimSpace(raw));
    if (spec.css == "font-weight") {
        if (value == "normal") return Ok(ValueKind::Number, "400");
        if (value == "bold") return Ok(ValueKind::Number, "700");
    }

    std::optional<double> number = ParseNumber(value);
    if (!number) return Fail("unreadable number: \"" + std::string(TrimSpace(raw)) + "\"");
    if (spec.css == "opacity" && (*number < 0 || *number > 1)) return Fail("opacity outside 0 to 1");
    if (spec.css == "font-weight" && (*number < 1 || *number > 1000)) return Fail("weight outside 1 to 1000");
    return Ok(ValueKind::Number, FormatNumber(*number));
}

Conversion ConvertKeyword(const PropertySpec& spec, std::string_view raw) {
    std::string value = ToLowerAscii(TrimSpace(raw));
    std::string words;
    for (const KeywordSpec& keyword : kKeywords) {
        if (keyword.css != spec.css) continue;
        if (keyword.word == value) return Ok(ValueKind::Keyword, std::string(keyword.xaml));
        words += (words.empty() ? "" : ", ") + std::string(keyword.word);
    }
    return Fail("unknown " + std::string(spec.css) + ": \"" + std::string(TrimSpace(raw)) + "\" (" + words + ")");
}

// The number before a unit of `text` (lower case), when it ends with that unit. `em` is not the end of `rem`.
std::optional<double> NumberWithUnit(std::string_view text, std::string_view unit) {
    if (!text.ends_with(unit) || (unit == "em" && text.ends_with("rem"))) return std::nullopt;
    return ParseNumber(TrimSpace(text.substr(0, text.size() - unit.size())));
}

// XAML's CharacterSpacing counts thousandths of an em: an em converts here, pixels once the engine knows the element's
// font size (ApplyFontSize).
Conversion ConvertLetterSpacing(std::string_view raw) {
    std::string value = ToLowerAscii(TrimSpace(raw));
    if (value == "normal" || value == "0") return Ok(ValueKind::Number, "0");
    if (std::optional<double> em = NumberWithUnit(value, "em"); em && std::abs(*em) <= kMaxSpacingEm) {
        return Ok(ValueKind::Number, std::to_string(std::lround(*em * 1000)));
    }
    if (std::optional<double> px = NumberWithUnit(value, "px"); px && std::abs(*px) <= kMaxSpacingPx) {
        return Ok(ValueKind::Number, FormatNumber(*px) + "px");
    }
    return Fail("unreadable letter spacing: \"" + std::string(TrimSpace(raw)) +
                "\" (in em, like 0.05em, or px, like 1px, up to 10em or 200px)");
}

// XAML's LineHeight is in pixels, 0 for the font's own spacing. A plain number, a percentage or an em is a factor of
// the font size, as in CSS: kept as "1.5em" until the engine knows that size (ApplyFontSize).
Conversion ConvertLineHeight(std::string_view raw) {
    std::string value = ToLowerAscii(TrimSpace(raw));
    if (value == "normal") return Ok(ValueKind::Length, "0");
    if (std::optional<double> px = NumberWithUnit(value, "px")) {
        if (*px < 0) return Fail("negative line height: \"" + std::string(TrimSpace(raw)) + "\"");
        return Ok(ValueKind::Length, FormatNumber(*px));
    }
    std::optional<double> factor = ParsePercent(value);
    if (!factor) factor = NumberWithUnit(value, "em");
    if (!factor) factor = ParseNumber(value);
    if (!factor || !(*factor > 0 && *factor <= kMaxLineFactor)) {
        return Fail("unreadable line height: \"" + std::string(TrimSpace(raw)) +
                    "\" (pixels like 20px, or a factor of the font size above 0 and up to 10, like 1.5 or 150%)");
    }
    return Ok(ValueKind::Length, FormatNumber(std::round(*factor * 10000) / 10000) + "em");
}

// url(fonts/file.ttf#Family Name): a font shipped in the theme folder. The value is the file URI and the family, as
// XAML's FontFamily reads it, the way images are given by path (an ms-appx address would point into Apple Music's
// package, not into the theme).
Conversion ConvertFontFile(std::string_view argument, const ValueContext& context) {
    size_t hash = argument.find('#');
    if (hash == std::string_view::npos || TrimSpace(argument.substr(hash + 1)).empty()) {
        return Fail("family name missing after \"#\" (url(fonts/file.ttf#Font name))");
    }
    std::string path(TrimSpace(argument.substr(0, hash)));
    std::string family(TrimSpace(argument.substr(hash + 1)));
    if (!context.allowImages) return Fail("fonts are turned off (assets part of the theme)");
    if (!IsSafeRelativePath(path)) return Fail("font path not allowed: " + path);
    size_t dot = path.find_last_of('.');
    std::string extension = dot == std::string::npos ? std::string() : ToLowerAscii(std::string_view(path).substr(dot));
    if (extension != ".ttf" && extension != ".otf" && extension != ".ttc") {
        return Fail("unsupported font: " + path + " (ttf, otf or ttc)");
    }
    if (!context.files.Exists(path)) return Fail("font not found: " + path);

    std::string file = context.folder + "/" + path;
    return Ok(ValueKind::Text, (file.starts_with("/") ? "file://" : "file:///") + file + "#" + family);
}

Conversion ConvertText(std::string_view raw, const ValueContext& context) {
    if (std::optional<std::string> argument = UrlArgument(raw)) return ConvertFontFile(*argument, context);

    std::string_view text = Unquoted(TrimSpace(raw));
    if (text.empty()) return Fail("empty font name");
    return Ok(ValueKind::Text, std::string(text));
}

// The direction in which text and children are laid out. A right-to-left language turns the whole page around.
Conversion ConvertFlowDirection(std::string_view raw) {
    std::string value = ToLowerAscii(TrimSpace(raw));
    if (value == "ltr" || value == "left-to-right" || value == "lefttoright") return Ok(ValueKind::Keyword, "LeftToRight");
    if (value == "rtl" || value == "right-to-left" || value == "righttoleft") return Ok(ValueKind::Keyword, "RightToLeft");
    return Fail("unknown direction: \"" + std::string(TrimSpace(raw)) + "\" (ltr or rtl)");
}

// The mode an element and its children draw in (FrameworkElement.RequestedTheme): Apple's text and control brushes
// switch with it, so a dark wash on a light theme keeps readable text without naming every brush.
Conversion ConvertColorScheme(std::string_view raw) {
    std::string value = ToLowerAscii(TrimSpace(raw));
    if (value == "dark") return Ok(ValueKind::Keyword, "Dark");
    if (value == "light") return Ok(ValueKind::Keyword, "Light");
    if (value == "normal" || value == "default") return Ok(ValueKind::Keyword, "Default");
    return Fail("unknown colour scheme: \"" + std::string(TrimSpace(raw)) + "\" (light, dark or normal)");
}

// A selector naming the element to move into, quoted or not. It must parse now: a bad one would never move anything.
Conversion ConvertMoveTo(std::string_view raw) {
    std::string_view text = Unquoted(TrimSpace(raw));
    std::string problem;
    if (text.empty() || !ParseSelector(text, &problem)) {
        return Fail("unreadable selector: \"" + std::string(TrimSpace(raw)) + "\" (the element to move into)");
    }
    return Ok(ValueKind::Text, std::string(text));
}

Conversion ConvertOrder(std::string_view raw) {
    std::string_view text = TrimSpace(raw);
    if (text.empty() || text.size() > 4 || text.find_first_not_of("0123456789") != std::string_view::npos) {
        return Fail("unreadable position: \"" + std::string(text) + "\" (a whole number from 0)");
    }
    return Ok(ValueKind::Number, std::to_string(std::stoi(std::string(text))));
}

Conversion ConvertAlignment(const PropertySpec& spec, std::string_view raw) {
    std::string value = ToLowerAscii(TrimSpace(raw));
    bool vertical = spec.css == "vertical-alignment";
    if (value == "center") return Ok(ValueKind::Keyword, "Center");
    if (value == "stretch") return Ok(ValueKind::Keyword, "Stretch");
    if (!vertical && value == "left") return Ok(ValueKind::Keyword, "Left");
    if (!vertical && value == "right") return Ok(ValueKind::Keyword, "Right");
    if (vertical && value == "top") return Ok(ValueKind::Keyword, "Top");
    if (vertical && value == "bottom") return Ok(ValueKind::Keyword, "Bottom");
    return Fail("unknown alignment: \"" + std::string(TrimSpace(raw)) + "\" (" +
                (vertical ? "top, center, bottom" : "left, center, right") + " or stretch)");
}

// A Grid row or column from 0, a span from 1, a z-index that may be negative.
Conversion ConvertInteger(const PropertySpec& spec, std::string_view raw) {
    std::string_view text = TrimSpace(raw);
    std::string_view digits = !text.empty() && text.front() == '-' ? text.substr(1) : text;
    if (digits.empty() || digits.size() > 6 || digits.find_first_not_of("0123456789") != std::string_view::npos) {
        return Fail("unreadable whole number: \"" + std::string(text) + "\"");
    }
    int number = std::stoi(std::string(text));
    int minimum = spec.css == "z-index" ? -999999 : spec.css.ends_with("-span") ? 1 : 0;
    if (number < minimum) {
        return Fail("\"" + std::string(spec.css) + "\" starts at " + std::to_string(minimum) + ": \"" +
                    std::string(text) + "\"");
    }
    return Ok(ValueKind::Number, std::to_string(number));
}

// `50%` or `0.5`, from 0 to 10; nothing otherwise.
std::optional<double> FilterFactor(std::string_view word) {
    constexpr double kMaxFactor = 10;
    std::optional<double> number = ParsePercent(word);
    if (!number) number = ParseNumber(TrimSpace(word));
    if (!number || *number < 0 || *number > kMaxFactor) return std::nullopt;
    return number;
}

std::optional<Argb> FilterColour(std::string_view word, const ValueContext& context) {
    Conversion converted = ConvertColor(word, context);
    if (!converted.value || converted.value->kind != ValueKind::Color) return std::nullopt;
    return ParseColor(converted.value->text);
}

// The matrix of one colour function of `filter`; nothing when it is not one, or its arguments do not read.
std::optional<ColorMatrix> FilterColourStep(const std::string& name, const std::vector<std::string_view>& args,
                                            const ValueContext& context) {
    const bool one = args.size() == 1;
    if (name == "hue-rotate" && one) {
        std::optional<double> degrees = ParseDegrees(args[0]);
        return degrees ? std::optional(HueRotateMatrix(*degrees)) : std::nullopt;
    }
    if (name == "tint" && (one || args.size() == 2)) {
        std::optional<Argb> ink = FilterColour(args[0], context);
        std::optional<double> amount = one ? 1.0 : FilterFactor(args[1]);
        return ink && amount && *amount <= 1 ? std::optional(TintMatrix(*ink, *amount)) : std::nullopt;
    }
    if (name == "duotone" && args.size() == 2) {
        std::optional<Argb> dark = FilterColour(args[0], context), light = FilterColour(args[1], context);
        return dark && light ? std::optional(DuotoneMatrix(*dark, *light)) : std::nullopt;
    }
    std::optional<double> amount = one ? FilterFactor(args[0]) : std::nullopt;
    if (!amount) return std::nullopt;
    if (name == "saturate") return SaturateMatrix(*amount);
    if (name == "grayscale") return GrayscaleMatrix(*amount);
    if (name == "sepia") return SepiaMatrix(*amount);
    if (name == "brightness") return BrightnessMatrix(*amount);
    if (name == "contrast") return ContrastMatrix(*amount);
    if (name == "invert") return InvertMatrix(*amount);
    return std::nullopt;
}

}  // namespace

namespace detail {

Conversion Fail(std::string problem) {
    return {std::nullopt, std::move(problem)};
}

Conversion Ok(ValueKind kind, std::string text) {
    return {Value{kind, std::move(text)}, {}};
}

// `url(path)` with optional quotes. Nothing when the value is not a url.
std::optional<std::string> UrlArgument(std::string_view raw) {
    std::string_view text = TrimSpace(raw);
    if (ToLowerAscii(text.substr(0, 4)) != "url(" || text.back() != ')') return std::nullopt;

    return std::string(Unquoted(TrimSpace(text.substr(4, text.size() - 5))));
}

// Strips a trailing `px`, the only unit the engine accepts.
std::optional<double> ParsePixels(std::string_view text) {
    if (ToLowerAscii(text).ends_with("px")) text.remove_suffix(2);
    return ParseNumber(TrimSpace(text));
}

// A number followed by `%`, as a fraction: `50%` is 0.5.
std::optional<double> ParsePercent(std::string_view text) {
    text = TrimSpace(text);
    if (text.empty() || text.back() != '%') return std::nullopt;
    std::optional<double> number = ParseNumber(TrimSpace(text.substr(0, text.size() - 1)));
    if (!number) return std::nullopt;
    return *number / 100;
}

// An angle in degrees: `135deg`, or `0` alone, as CSS allows.
std::optional<double> ParseDegrees(std::string_view text) {
    text = TrimSpace(text);
    if (ToLowerAscii(text).ends_with("deg")) return ParseNumber(TrimSpace(text.substr(0, text.size() - 3)));
    if (text == "0") return 0.0;
    return std::nullopt;
}

// `name(arguments)` taking the whole text, the name in any case. Nothing for another form.
std::optional<std::string_view> FunctionArguments(std::string_view raw, std::string_view name) {
    std::string_view text = TrimSpace(raw);
    if (text.size() < name.size() + 2 || ToLowerAscii(text.substr(0, name.size())) != name) return std::nullopt;
    std::string_view rest = TrimSpace(text.substr(name.size()));
    if (rest.size() < 2 || rest.front() != '(' || rest.back() != ')') return std::nullopt;
    return rest.substr(1, rest.size() - 2);
}

// Four decimals are plenty for a point relative to an element, and keep the text the same from one load to the next.
std::string FormatFraction(double number) {
    number = std::round(number * 10000) / 10000;
    if (number == 0) number = 0;  // no "-0"
    return FormatNumber(number);
}

// As in CSS, only a margin goes below zero. A negative padding, border or corner reaches a template's Border, which
// refuses it at the next layout: WinUI then stops Apple Music.
Conversion ConvertThickness(std::string_view raw, bool allowNegative) {
    std::string_view text = TrimSpace(raw);
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
        if (!pixels) return Fail("unreadable thickness: \"" + std::string(piece) + "\"");
        if (*pixels < 0 && !allowNegative) return Fail("negative value: \"" + std::string(piece) + "\" (only a margin can be)");
        if (count == parts.size()) return Fail("more than four values: \"" + std::string(text) + "\"");
        parts[count++] = FormatNumber(*pixels);
    }
    if (count == 0) return Fail("empty value");

    // Expand the CSS shorthand: a missing side takes the value of the opposite side.
    std::string top = parts[0];
    std::string right = count >= 2 ? parts[1] : top;
    std::string bottom = count >= 3 ? parts[2] : top;
    std::string left = count >= 4 ? parts[3] : right;

    // XAML order is left, top, right, bottom. For corner-radius, whose CSS order is top-left, top-right, bottom-right,
    // bottom-left, the same expansion gives bottom-left, top-left, top-right, bottom-right (xaml_backend.cpp).
    return Ok(ValueKind::Thickness, left + "," + top + "," + right + "," + bottom);
}

// Reads one function of `filter` into `parts`; the problem when it does not read.
std::optional<std::string> ReadFilterItem(std::string_view item, const ValueContext& context, FilterParts& parts) {
    constexpr double kMaxBlur = 20;
    constexpr double kMaxEdge = 0.5;
    const size_t open = item.find('(');
    const std::string name = open == std::string_view::npos ? "" : ToLowerAscii(TrimSpace(item.substr(0, open)));
    std::optional<std::string_view> inside = name.empty() ? std::nullopt : FunctionArguments(item, name);
    if (!inside) return "unreadable filter function \"" + std::string(item) + "\"";
    const std::vector<std::string_view> args = SplitArguments(*inside);
    if (name == "blur" && args.size() == 1 && !parts.radius) {
        parts.radius = ParsePixels(TrimSpace(args[0]));
        if (!parts.radius || *parts.radius < 0 || *parts.radius > kMaxBlur) return "blur outside 0 to 20 px";
        return std::nullopt;
    }
    if (name == "edges") {
        if (!parts.radius || !parts.shares.empty() || args.size() != 1) return "edges() goes once, after blur()";
        for (std::string_view percent : SplitWords(args[0])) {
            std::optional<double> share = ParsePercent(percent);
            if (!share || *share < 0 || *share > kMaxEdge) return "edges of a blur at most 50% each";
            parts.shares.push_back(*share);
        }
        if (parts.shares.empty() || parts.shares.size() > 2 || parts.shares.front() + parts.shares.back() <= 0) {
            return "edges(<top %> [<bottom %>]) takes one or two shares, not both 0";
        }
        return std::nullopt;
    }
    std::optional<ColorMatrix> step = FilterColourStep(name, args, context);
    if (!step) return "unreadable " + name + "(): \"" + std::string(item) + "\"";
    parts.matrix = Then(parts.matrix, *step);
    return std::nullopt;
}

}  // namespace detail

std::span<const PropertySpec> KnownProperties() {
    return kProperties;
}

const PropertySpec* FindProperty(std::string_view css) {
    std::string key = ToLowerAscii(css);
    for (const PropertySpec& spec : KnownProperties()) {
        if (spec.css == key) return &spec;
    }
    return nullptr;
}

std::string_view PropertyHint(std::string_view css) {
    std::string key = ToLowerAscii(TrimSpace(css));
    for (const PropertyAdvice& advice : kAdvice) {
        if (advice.css == key) return advice.advice;
    }
    return {};
}

bool AppliesToDecor(std::string_view xaml) {
    return std::find(kDecorProperties.begin(), kDecorProperties.end(), xaml) != kDecorProperties.end();
}

Conversion ConvertValue(const PropertySpec& spec, std::string_view raw, const ValueContext& context) {
    switch (spec.kind) {
        case PropertyKind::Brush:
            return ConvertBrush(raw, context);
        case PropertyKind::Backdrop:
            return ConvertBackdrop(raw, context);
        case PropertyKind::Length:
            // No limit: XAML's MaxWidth and MaxHeight default to infinity, MinWidth and MinHeight to 0 (NaN throws).
            if (spec.css.starts_with("max-") && ToLowerAscii(TrimSpace(raw)) == "none") return Ok(ValueKind::Length, "inf");
            if (spec.css.starts_with("min-") && ToLowerAscii(TrimSpace(raw)) == "auto") return Ok(ValueKind::Length, "0");
            if (spec.xaml == "FontSize") return ConvertFontSize(raw);
            return ConvertLength(raw);
        case PropertyKind::Thickness:
            return ConvertThickness(raw, spec.xaml == "Margin");
        case PropertyKind::Visibility:
            return ConvertVisibility(spec, raw);
        case PropertyKind::Number:
            return ConvertNumber(spec, raw);
        case PropertyKind::Keyword:
            return ConvertKeyword(spec, raw);
        case PropertyKind::LetterSpacing:
            return ConvertLetterSpacing(raw);
        case PropertyKind::LineHeight:
            return ConvertLineHeight(raw);
        case PropertyKind::Text:
            return ConvertText(raw, context);
        case PropertyKind::Template:
            return ConvertTemplate(raw, context);
        case PropertyKind::FlowDirection:
            return ConvertFlowDirection(raw);
        case PropertyKind::ColorScheme:
            return ConvertColorScheme(raw);
        case PropertyKind::Order:
            return ConvertOrder(raw);
        case PropertyKind::Target:
            return ConvertMoveTo(raw);
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
        case PropertyKind::Filter:
            return ConvertFilter(raw, context);
        case PropertyKind::Transition:
            return ConvertTransition(raw);
        case PropertyKind::Clip:
            return ConvertClip(spec, raw);
        case PropertyKind::Animation: {
            static const KeyframeSets kNone;
            std::string problem;
            std::optional<std::string> text = AnimationValue(raw, context.keyframes ? *context.keyframes : kNone, &problem);
            if (!text) return Fail(problem);
            return Ok(ValueKind::Animation, std::move(*text));
        }
        case PropertyKind::Content:
            return ConvertContent(raw, context);
    }
    return Fail("unknown property type");
}

std::string FormatNumber(double number) {
    std::array<char, 32> buffer{};
    auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    if (error != std::errc()) return "0";
    return std::string(buffer.data(), end);
}

std::string ShownText(std::string_view property, const Value& value) {
    if (property != "CornerRadius" || value.kind != ValueKind::Thickness) return value.text;
    return CornersInCssOrder(value.text).value_or(value.text);
}

bool IsRelativeSize(const Value& value) {
    return value.kind == ValueKind::Length && value.text.ends_with('%');
}

std::optional<Value> ScaleFontSize(const Value& relative, const Value& size) {
    std::optional<double> factor = ParsePercent(relative.text);
    std::optional<double> pixels = ParseNumber(size.text);
    if (!IsRelativeSize(relative) || !factor || !pixels || !(*pixels > 0)) return std::nullopt;
    return Value{ValueKind::Length, FormatNumber(std::round(*pixels * *factor * 100) / 100)};
}

bool NeedsFontSize(const Value& value) {
    return (value.kind == ValueKind::Number && value.text.ends_with("px")) ||
           (value.kind == ValueKind::Length && value.text.ends_with("em"));
}

std::optional<Value> ApplyFontSize(const Value& value, const Value& fontSize) {
    std::optional<double> size = ParseNumber(fontSize.text);
    if (!NeedsFontSize(value) || !size || !(*size > 0)) return std::nullopt;
    std::optional<double> amount = ParseNumber(std::string_view(value.text).substr(0, value.text.size() - 2));
    if (!amount) return std::nullopt;
    if (value.kind == ValueKind::Number) {
        return Value{ValueKind::Number, std::to_string(std::lround(*amount / *size * 1000))};  // thousandths of an em
    }
    return Value{ValueKind::Length, FormatNumber(std::round(*size * *amount * 100) / 100)};
}

bool SameValue(const Value& a, const Value& b) {
    if (a == b) return true;
    const bool numeric = a.kind == ValueKind::Length || a.kind == ValueKind::Number || a.kind == ValueKind::Thickness ||
                         a.kind == ValueKind::Point || a.kind == ValueKind::Transform || a.kind == ValueKind::Shadow ||
                         a.kind == ValueKind::Clip || a.kind == ValueKind::Blur;
    if (a.kind != b.kind || !numeric) return false;

    std::string_view left = a.text, right = b.text;
    while (true) {
        size_t endLeft = std::min(left.find(','), left.size()), endRight = std::min(right.find(','), right.size());
        std::optional<double> x = ParseNumber(left.substr(0, endLeft));
        std::optional<double> y = ParseNumber(right.substr(0, endRight));
        if (!x || !y || std::abs(*x - *y) > 1e-5 * std::max({1.0, std::abs(*x), std::abs(*y)})) return false;
        if (endLeft == left.size() || endRight == right.size()) return endLeft == left.size() && endRight == right.size();
        left.remove_prefix(endLeft + 1);
        right.remove_prefix(endRight + 1);
    }
}

}  // namespace ampify::theme
