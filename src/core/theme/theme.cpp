#include "theme.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

#include "../json.hpp"
#include "../manifest.hpp"
#include "../text_util.hpp"
#include "fonts.hpp"
#include "ini.hpp"
#include "spicetify.hpp"

namespace ampify::theme {

namespace {

// Picks the scheme to use: the requested one (case-insensitive), or the first section when none is requested.
const Ini::Section* PickScheme(const Ini& ini, const std::string& wanted, Theme& theme) {
    if (!wanted.empty()) {
        if (const Ini::Section* section = ini.FindSection(wanted)) return section;
        theme.problems.push_back("colors.ini: scheme \"" + wanted + "\" not found, the first one is used");
    }
    for (const Ini::Section& section : ini.Sections()) {
        if (!section.name.empty()) return &section;
    }
    return nullptr;
}

// The colours file: colors.ini, or color.ini as Spicetify names it. Empty name when there is none.
std::pair<std::string, std::optional<std::string>> ReadColorsFile(FileSource& files) {
    for (const char* name : {"colors.ini", "color.ini"}) {
        if (std::optional<std::string> text = files.Read(name)) return {name, std::move(text)};
    }
    return {};
}

// A Spicetify colour is written in lowercase (main, main-elevated, main_fg); an app brush in PascalCase.
bool IsSpicetifyName(std::string_view key) {
    for (char c : key) {
        if (std::isupper(static_cast<unsigned char>(c))) return false;
    }
    return !key.empty();
}

// Light or dark, from the background the scheme gives: Spicetify's main, or the app's page background.
std::optional<bool> DarkFromPalette(const Palette& palette) {
    for (const char* key : {"main", "SolidBackgroundFillColorBaseBrush", "ApplicationPageBackgroundThemeBrush"}) {
        if (std::optional<Argb> color = palette.Find(key)) {
            double luminance = 0.2126 * color->r + 0.7152 * color->g + 0.0722 * color->b;
            return luminance < 128.0;
        }
    }
    return std::nullopt;
}

// WCAG relative luminance, 0 (black) to 1 (white).
double RelativeLuminance(Argb color) {
    auto linear = [](std::uint8_t channel) {
        double value = channel / 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
}

constexpr const char* kAccent = "AccentFillColorDefaultBrush";

// A brush the theme does not name, worked out from one it does. The theme's resources.xaml has the last word: a key
// it defines, or the one the colour comes from, is left to it.
void DefineDerived(Theme& theme, const std::set<std::string>& owned, const char* from, const char* key, Argb color,
                   bool applied) {
    if (owned.count(from) || owned.count(key) || !theme.palette.Define(key, color)) return;
    if (applied) theme.resources.push_back({key, {ValueKind::Color, ToXamlColor(color)}});
}

// Fluent's accent under the pointer and while pressed is the accent at 90% and 80%. A theme that names only the
// accent would otherwise show Apple Music's own colour on hover.
void DefineAccentStates(Theme& theme, const std::set<std::string>& owned, bool applied) {
    constexpr std::pair<const char*, double> kStates[] = {{"AccentFillColorSecondaryBrush", 0.9},
                                                          {"AccentFillColorTertiaryBrush", 0.8}};
    std::optional<Argb> accent = theme.palette.Find(kAccent);
    if (!accent) return;
    for (const auto& [key, opacity] : kStates) {
        Argb color = *accent;
        color.a = static_cast<std::uint8_t>(std::lround(accent->a * opacity));
        DefineDerived(theme, owned, kAccent, key, color, applied);
    }
}

// Apple Music lays white text on the accent whatever its colour. On an accent where white reads under 3:1 (a light
// cyan, Spotify's green, a yellow) and that names no colour for its text, the text goes black, as in Fluent's dark
// mode. Any other accent keeps Apple Music's white.
void DefineTextOnAccent(Theme& theme, const std::set<std::string>& owned, bool applied) {
    constexpr const char* kPrimary = "TextOnAccentFillColorPrimaryBrush";
    constexpr const char* kSecondary = "TextOnAccentFillColorSecondaryBrush";
    constexpr double kMinimumWhiteContrast = 3.0;
    constexpr Argb kBlack{0xFF, 0, 0, 0};
    constexpr Argb kBlackSecondary{0xB3, 0, 0, 0};  // 70%, as Fluent's secondary text on the accent
    std::optional<Argb> accent = theme.palette.Find(kAccent);
    if (!accent || theme.palette.Find(kPrimary) || owned.count(kPrimary)) return;
    if (1.05 / (RelativeLuminance(*accent) + 0.05) >= kMinimumWhiteContrast) return;
    for (const auto& [key, color] : {std::pair{kPrimary, kBlack}, std::pair{kSecondary, kBlackSecondary}}) {
        DefineDerived(theme, owned, kAccent, key, color, applied);
    }
}

// WinUI's toggles, sliders, check boxes, selection marks, links and the system accent brushes read keys of their own,
// which its dictionaries set to the accent once, when they load: a theme's accent alone left them in Apple Music's
// blue (the Settings pages, the sidebar's selection mark, "Show the whole album", the playing row's bars). The list is
// every key the inspector found still holding Apple's accent under a theme. Brushes only: WinUI builds its own brushes
// from the SystemAccentColor colours once, so writing those left them in the theme's accent after the theme went.
void DefineAccentControls(Theme& theme, const std::set<std::string>& owned, bool applied) {
    constexpr const char* kHover = "AccentFillColorSecondaryBrush";
    constexpr const char* kPressed = "AccentFillColorTertiaryBrush";
    // A theme that names no accent text colour has its links in the accent itself.
    const char* kLink = theme.palette.Find("AccentTextFillColorPrimaryBrush") ? "AccentTextFillColorPrimaryBrush" : kAccent;
    constexpr double kDisabled = 0.35;  // Fluent's disabled accent, #59 alpha
    struct Derived {
        const char* key;
        const char* from;
        double opacity;
    };
    constexpr const char* kOnAccent = "TextOnAccentFillColorPrimaryBrush";
    const Derived kControls[] = {
        {"AccentButtonBackground", kAccent, 1.0},
        {"AccentButtonBackgroundPointerOver", kHover, 1.0},
        {"AccentButtonBackgroundPressed", kPressed, 1.0},
        {"AccentButtonForeground", kOnAccent, 1.0},
        {"AccentButtonForegroundPointerOver", kOnAccent, 1.0},
        {"AccentButtonForegroundPressed", kOnAccent, 1.0},
        {"AccentFillColorDisabledBrush", kAccent, kDisabled},
        {"AccentFillColorSelectedTextBackgroundBrush", kAccent, 1.0},
        {"TextControlSelectionHighlightColor", kAccent, 1.0},
        {"ListViewItemSelectionIndicatorBrush", kAccent, 1.0},
        {"ListViewItemSelectionIndicatorPointerOverBrush", kHover, 1.0},
        {"ListViewItemSelectionIndicatorPressedBrush", kPressed, 1.0},
        {"ListViewItemSelectionIndicatorDisabledBrush", kAccent, kDisabled},
        {"NavigationViewSelectionIndicatorForeground", kAccent, 1.0},
        {"ListViewItemCheckBoxSelectedBrush", kAccent, 1.0},
        {"ListViewItemCheckBoxSelectedDisabledBrush", kAccent, kDisabled},
        {"GridViewItemCheckBoxSelectedBrush", kAccent, 1.0},
        {"GridViewItemCheckBoxSelectedDisabledBrush", kAccent, kDisabled},
        {"GridViewItemSelectedBorderBrush", kAccent, 1.0},
        {"GridViewItemSelectedDisabledBorderBrush", kAccent, kDisabled},
        {"ListBoxItemSelectedBackgroundThemeBrush", kAccent, 1.0},
        {"ComboBoxItemPillFillBrush", kAccent, 1.0},
        {"CalendarViewSelectedBorderBrush", kAccent, 1.0},
        {"CalendarViewSelectedDisabledBorderBrush", kAccent, kDisabled},
        {"PersonPictureEllipseBadgeFillThemeBrush", kAccent, 1.0},
        {"ProgressRingForegroundThemeBrush", kAccent, 1.0},
        {"VerticalThumbBrush", kAccent, 1.0},
        {"HoverUIButtonBackgroundPointerOverBrush", kHover, 1.0},
        {"HoverUIButtonBackgroundPressedBrush", kPressed, 1.0},
        {"SystemControlBackgroundAccentBrush", kAccent, 1.0},
        {"SystemControlForegroundAccentBrush", kAccent, 1.0},
        {"SystemControlDisabledAccentBrush", kAccent, kDisabled},
        {"SystemControlHighlightAccentBrush", kAccent, 1.0},
        {"SystemControlHighlightAltAccentBrush", kAccent, 1.0},
        {"SystemControlHighlightListAccentLowBrush", kAccent, 1.0},
        {"SystemControlHighlightListAccentMediumLowBrush", kAccent, 1.0},
        {"SystemControlHighlightListAccentMediumBrush", kAccent, 1.0},
        {"SystemControlHighlightListAccentHighBrush", kAccent, 1.0},
        {"SystemControlHighlightListAccentVeryHighBrush", kAccent, 1.0},
        {"SystemControlHighlightAltListAccentLowBrush", kAccent, 1.0},
        {"SystemControlHighlightAltListAccentMediumBrush", kAccent, 1.0},
        {"SystemControlHighlightAltListAccentHighBrush", kAccent, 1.0},
        {"SystemControlHyperlinkTextBrush", kLink, 1.0},
        {"ButtonAccentTextFillColorPrimaryBrush", kLink, 1.0},
        {"ContentDialogBorderThemeBrush", kAccent, 1.0},
        {"SystemControlBackgroundAccentRevealBorderBrush", kAccent, 1.0},
        {"SystemControlHighlightAccentRevealBorderBrush", kAccent, 1.0},
        {"SystemControlHighlightAccent3RevealBackgroundBrush", kAccent, 1.0},
        {"SystemControlHighlightAccent3RevealAccent2BackgroundBrush", kAccent, 1.0},
        {"ToggleButtonBackgroundChecked", kAccent, 1.0},
        {"ToggleButtonBackgroundCheckedPointerOver", kHover, 1.0},
        {"ToggleButtonBackgroundCheckedPressed", kPressed, 1.0},
        {"ToggleButtonBorderBrushChecked", kAccent, 1.0},
        {"ToggleSwitchFillOn", kAccent, 1.0},
        {"ToggleSwitchFillOnPointerOver", kHover, 1.0},
        {"ToggleSwitchFillOnPressed", kPressed, 1.0},
        {"ToggleSwitchStrokeOn", kAccent, 1.0},
        {"SliderTrackValueFill", kAccent, 1.0},
        {"SliderTrackValueFillPointerOver", kHover, 1.0},
        {"SliderTrackValueFillPressed", kPressed, 1.0},
        {"SliderThumbBackground", kAccent, 1.0},
        {"SliderThumbBackgroundPointerOver", kHover, 1.0},
        {"SliderThumbBackgroundPressed", kPressed, 1.0},
        {"SliderTrackValueFillDisabled", kAccent, 0.4},
        {"SliderThumbBackgroundDisabled", kAccent, 0.4},
        {"CheckBoxCheckBackgroundFillChecked", kAccent, 1.0},
        {"CheckBoxCheckBackgroundFillCheckedPointerOver", kHover, 1.0},
        {"CheckBoxCheckBackgroundFillCheckedPressed", kPressed, 1.0},
        {"CheckBoxCheckBackgroundStrokeChecked", kAccent, 1.0},
        {"RadioButtonOuterEllipseCheckedFill", kAccent, 1.0},
        {"RadioButtonOuterEllipseCheckedFillPointerOver", kHover, 1.0},
        {"RadioButtonOuterEllipseCheckedFillPressed", kPressed, 1.0},
        {"RadioButtonOuterEllipseCheckedStroke", kAccent, 1.0},
        {"ProgressBarForeground", kAccent, 1.0},
        {"HyperlinkButtonForeground", kLink, 1.0},
        {"HyperlinkButtonForegroundPointerOver", kLink, 0.9},
        {"HyperlinkButtonForegroundPressed", kLink, 0.8},
    };
    for (const Derived& derived : kControls) {
        std::optional<Argb> color = theme.palette.Find(derived.from);
        if (!color) continue;
        color->a = static_cast<std::uint8_t>(std::lround(color->a * derived.opacity));
        DefineDerived(theme, owned, derived.from, derived.key, *color, applied);
    }
}

// Ampify's extension panels and WinUI's flyouts lie over the page in Fluent's grey acrylic, foreign to any theme. An
// opaque card colour takes its place; a see-through one would let the page show through them.
void DefineAcrylicSurfaces(Theme& theme, const std::set<std::string>& owned, bool applied) {
    constexpr const char* kCard = "CardBackgroundFillColorDefaultBrush";
    std::optional<Argb> card = theme.palette.Find(kCard);
    if (!card || card->a != 0xFF) return;
    for (const char* key : {"AcrylicInAppFillColorDefaultBrush", "AcrylicBackgroundFillColorDefaultBrush"}) {
        DefineDerived(theme, owned, kCard, key, *card, applied);
    }
}

// The colours a theme leaves to Ampify, once its own resources.xaml is known.
void DefineDerivedColors(Theme& theme, bool applied) {
    std::vector<std::string> keys = XamlKeys(theme.resourceDictionary);
    std::set<std::string> owned(keys.begin(), keys.end());
    DefineAccentStates(theme, owned, applied);
    DefineTextOnAccent(theme, owned, applied);
    DefineAccentControls(theme, owned, applied);
    DefineAcrylicSurfaces(theme, owned, applied);
}

// A Spicetify colour: a $name, a --spice-name variable as in Spicetify, and the app brushes that play its part
// (SpicetifyResources) unless the scheme names them itself. Returns the Spicetify name.
std::string DefineSpicetifyColor(const Ini::Entry& entry, Argb color, Theme& theme) {
    std::string key = SpicetifyKey(entry.key);
    if (key.empty()) key = ToLowerAscii(entry.key);  // a colour of the theme's own, such as accent
    if (!theme.palette.Define(key, color)) {
        theme.problems.push_back("colors.ini line " + std::to_string(entry.line) + ": \"" + entry.key +
                                 "\" defined twice, ignored");
        return {};
    }
    if (ToLowerAscii(entry.key) != key) theme.palette.Define(ToLowerAscii(entry.key), color);  // main_fg stays usable too
    theme.variables["spice-" + key] = ToXamlColor(color);
    return key;
}

std::set<std::string> LoadColors(FileSource& files, const std::string& wanted, bool applied, Theme& theme) {
    std::set<std::string> spicetify;
    auto [file, text] = ReadColorsFile(files);
    if (!text) return spicetify;

    Ini ini;
    for (const std::string& problem : ini.Parse(*text)) theme.problems.push_back(file + " " + problem);
    for (const Ini::Section& section : ini.Sections()) {
        if (!section.name.empty()) theme.schemes.push_back(section.name);
    }

    const Ini::Section* scheme = PickScheme(ini, wanted, theme);
    if (!scheme) {
        theme.problems.push_back(file + ": no scheme (a [Base] section, for example)");
        return spicetify;
    }
    theme.scheme = scheme->name;

    for (const Ini::Entry& entry : scheme->entries) {
        std::optional<Argb> color = ParseColor(entry.value);
        if (!color) {
            theme.problems.push_back(file + " line " + std::to_string(entry.line) + ": color \"" + entry.value +
                                     "\" is unreadable");
            continue;
        }
        if (IsSpicetifyName(entry.key)) {
            std::string key = DefineSpicetifyColor(entry, *color, theme);
            if (!key.empty()) spicetify.insert(key);
            continue;
        }
        if (!theme.palette.Define(entry.key, *color)) {
            theme.problems.push_back(file + " line " + std::to_string(entry.line) + ": \"" + entry.key +
                                     "\" defined twice, ignored");
            continue;
        }
        if (applied) theme.resources.push_back({entry.key, {ValueKind::Color, ToXamlColor(*color)}});
    }

    // The app brushes that play the part of a Spicetify colour, unless the scheme gives them itself.
    for (const SpicetifyResource& mapping : SpicetifyResources()) {
        std::optional<Argb> color = spicetify.count(mapping.spicetify) ? theme.palette.Find(mapping.spicetify) : std::nullopt;
        if (!color || theme.palette.Find(mapping.resource)) continue;
        theme.palette.Define(mapping.resource, *color);
        if (applied) theme.resources.push_back({mapping.resource, {ValueKind::Color, ToXamlColor(*color)}});
    }
    theme.dark = DarkFromPalette(theme.palette);
    return spicetify;
}

// Returns false when the theme must be refused. Refusing is the rule the API sets: a bad key breaks the theme.
bool LoadDictionary(FileSource& files, const LoadOptions& options, Theme& theme) {
    std::optional<std::string> xaml = files.Read("resources.xaml");
    if (!xaml) return true;

    if (xaml->find("<ResourceDictionary") == std::string::npos) {
        theme.problems.push_back("resources.xaml: no <ResourceDictionary>, theme refused");
        return false;
    }

    if (XamlKeys(*xaml).empty()) {
        // No key to check: implicit styles only, or an empty dictionary such as the one `ampify new snippet` writes.
    } else if (!options.allowedKeys) {
        theme.problems.push_back("resources.xaml: list of allowed keys missing, check not done");
    } else {
        bool allAllowed = true;
        for (const std::string& key : XamlKeys(*xaml)) {
            if (options.allowedKeys->count(key)) continue;
            theme.problems.push_back("resources.xaml: key \"" + key + "\" is not in the allowed list, theme refused");
            allAllowed = false;
        }
        if (!allAllowed) return false;
    }

    theme.resourceDictionary = std::move(*xaml);
    return true;
}

std::string AtLine(const std::string& source, int line) {
    return source + " line " + std::to_string(line);
}

// What the rules of one file need besides the theme they fill.
struct RuleSource {
    std::string file = "user.css";  // for the log and the problems
    const Theme* base = nullptr;    // the active theme, for a snippet: its font roles fill what the snippet leaves out
    int layer = 0;                  // -1 for the rules translated from color.ini: the theme's user.css wins over them
};

// `--font-title`, `--font-title-weight`, `--font-title-size`: checks the value and records the role part.
void DefineFontPart(const std::string& name, const std::string& value, int line, const ValueContext& context,
                    const RuleSource& source, Theme& theme) {
    std::string role = name.substr(5);  // after "font-"
    const char* css = "font-family";
    std::string FontRole::*part = &FontRole::family;
    for (auto [suffix, property, member] : {std::tuple{"-weight", "font-weight", &FontRole::weight},
                                            std::tuple{"-size", "font-size", &FontRole::size}}) {
        std::string_view end(suffix);
        if (role.size() > end.size() && role.ends_with(end)) {
            role.resize(role.size() - end.size());
            css = property;
            part = member;
            break;
        }
    }
    if (role.empty()) {
        theme.problems.push_back(AtLine(source.file, line) + ": font role without a name (--font-<role>)");
        return;
    }

    Conversion conversion = ConvertValue(*FindProperty(css), value, context);
    if (conversion.value && IsRelativeSize(*conversion.value)) conversion = {std::nullopt, "a font role takes pixels"};
    if (!conversion.value) {
        theme.problems.push_back(AtLine(source.file, line) + ": --" + name + ": " + conversion.problem + ", ignored");
        return;
    }
    theme.fonts[CanonicalFontRole(role)].*part = conversion.value->text;
}

// A rule inside @setting counts only while its condition holds. A key the theme does not declare never holds.
bool SettingHolds(const Rule& rule, const std::string& file, Theme& theme) {
    if (!rule.setting) return true;
    auto it = theme.settings.find(rule.setting->key);
    if (it == theme.settings.end()) {
        theme.problems.push_back(AtLine(file, rule.line) + ": @setting " + rule.setting->key +
                                 ": the theme's manifest declares no such setting, ignored");
        return false;
    }
    if (!rule.setting->value) return it->second == "true";
    return ToLowerAscii(it->second) == ToLowerAscii(*rule.setting->value);
}

// The settings manifest.json declares, with their value now: variables setting-<key>, and the @setting conditions.
void LoadSettings(FileSource& files, const LoadOptions& options, Theme& theme) {
    std::optional<std::string> text = files.Read("manifest.json");
    if (!text) return;
    std::vector<std::string> errors;
    for (const SettingSpec& spec : DeclaredSettings(*text, &errors)) {
        auto stored = options.settings.find(spec.key);
        std::string value = SettingValue(spec, stored == options.settings.end() ? std::nullopt
                                                                                : std::optional<std::string>(stored->second));
        theme.settings[spec.key] = value;
        theme.variables["setting-" + spec.key] = value;
    }
    for (const std::string& error : errors) theme.problems.push_back("manifest.json: " + error + ", settings ignored");
}

// The declarations of the :root blocks, in file order. A variable can use the ones defined before it.
void LoadVariables(const Stylesheet& sheet, const ValueContext& context, const RuleSource& source, Theme& theme) {
    for (const Rule& rule : sheet.rules) {
        if (!rule.root || !SettingHolds(rule, source.file, theme)) continue;
        for (const Declaration& declaration : rule.declarations) {
            if (!declaration.property.starts_with("--") || declaration.property.size() == 2) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + ": \"" + declaration.property +
                                         "\" inside :root, only --name variables are read");
                continue;
            }
            std::string problem;
            std::optional<std::string> value = SubstituteVariables(declaration.value, theme.variables, &problem);
            if (value) value = EvaluateCalc(*value, &problem);
            if (!value) {
                theme.problems.push_back(AtLine(source.file, declaration.line) + ": " + problem + ", ignored");
                continue;
            }
            std::string name = ToLowerAscii(std::string_view(declaration.property).substr(2));
            theme.variables[name] = *value;
            if (name.starts_with("font-")) DefineFontPart(name, *value, declaration.line, context, source, theme);
        }
    }
}

void AddAssignment(const Rule& rule, const PropertySpec& spec, const Value& value, const Declaration& declaration,
                   int order, const RuleSource& source, Theme& theme) {
    for (const Selector& selector : rule.selectors) {
        // A move is not a value that can come and go with a state: the other items would shift each time.
        if (selector.trigger != Trigger::Always && spec.kind == PropertyKind::Order) {
            theme.problems.push_back(AtLine(source.file, declaration.line) + ": \"" + declaration.property +
                                     "\" is not possible under " + TriggerName(selector.trigger) + " (" + selector.text +
                                     "), ignored");
            continue;
        }
        Assignment assignment{selector, SpecificityOf(selector), std::string(spec.xaml), value, declaration.important,
                              order, declaration.line};
        assignment.source = source.file;
        assignment.layer = source.layer;
        assignment.media = rule.media;
        theme.assignments.push_back(std::move(assignment));
    }
}

// `font-role: title` sets the family, the weight and the size of the role at once.
bool AddFontRole(const Rule& rule, const Declaration& declaration, const std::string& value, int order,
                 const RuleSource& source, bool applied, Theme& theme) {
    std::optional<FontStyle> style = ResolveFontRole(std::vector<const Theme*>{&theme, source.base}, value);
    if (!style) {
        theme.problems.push_back(AtLine(source.file, declaration.line) + ": font role \"" + value +
                                 "\" is unknown, ignored");
        return false;
    }
    if (!applied) return true;
    AddAssignment(rule, *FindProperty("font-family"), {ValueKind::Text, style->family}, declaration, order, source, theme);
    AddAssignment(rule, *FindProperty("font-weight"), {ValueKind::Number, style->weight}, declaration, order, source, theme);
    AddAssignment(rule, *FindProperty("font-size"), {ValueKind::Length, style->size}, declaration, order, source, theme);
    return true;
}

void LoadRuleText(const std::string& css, const ValueContext& context, Theme& theme, bool applied,
                  const RuleSource& source) {
    Stylesheet sheet = ParseStylesheet(css);
    for (const std::string& problem : sheet.problems) theme.problems.push_back(source.file + " " + problem);

    // Variables first: as in CSS, a rule may use a variable that a :root block defines further down. The @keyframes
    // too: an `animation` may name a block written after it.
    LoadVariables(sheet, context, source, theme);
    for (const Keyframes& block : sheet.keyframes) {
        std::vector<std::pair<int, std::string>> problems;
        theme.keyframes[block.name] = ReadKeyframes(block, context, theme.variables, problems);
        for (const auto& [line, problem] : problems) {
            theme.problems.push_back(AtLine(source.file, line) + ": " + problem + ", ignored");
        }
    }
    ValueContext rules = context;
    rules.keyframes = &theme.keyframes;

    int order = 0;
    for (const Rule& rule : sheet.rules) {
        if (rule.root) continue;
        RuleSource from = source;
        if (!rule.origin.empty()) from.file = rule.origin;
        if (!SettingHolds(rule, from.file, theme)) continue;
        for (const Declaration& declaration : rule.declarations) {
            if (declaration.property.starts_with("--")) {
                theme.problems.push_back(AtLine(from.file, declaration.line) + ": variable \"" +
                                         declaration.property + "\" outside :root, ignored");
                continue;
            }

            std::string problem;
            std::optional<std::string> raw = SubstituteVariables(declaration.value, theme.variables, &problem);
            if (raw) raw = EvaluateCalc(*raw, &problem);
            if (!raw) {
                theme.problems.push_back(AtLine(from.file, declaration.line) + ": " + problem + ", ignored");
                continue;
            }

            if (ToLowerAscii(declaration.property) == "font-role") {
                if (AddFontRole(rule, declaration, *raw, order, from, applied, theme)) order++;
                continue;
            }

            const PropertySpec* spec = FindProperty(declaration.property);
            if (!spec) {
                theme.problems.push_back(AtLine(from.file, declaration.line) + ": property \"" +
                                         declaration.property + "\" is unknown, ignored");
                continue;
            }

            Conversion conversion = ConvertValue(*spec, *raw, rules);
            if (!conversion.value) {
                theme.problems.push_back(AtLine(from.file, declaration.line) + ": " + conversion.problem +
                                         ", ignored");
                continue;
            }

            if (applied) AddAssignment(rule, *spec, *conversion.value, declaration, order, from, theme);
            order++;
        }
    }
}

void LoadRules(FileSource& files, const ValueContext& context, Theme& theme, bool applied, const RuleSource& source) {
    if (std::optional<std::string> css = files.Read("user.css")) LoadRuleText(*css, context, theme, applied, source);
}

double LargestCorner(std::string_view corners) {
    double largest = 0;
    while (!corners.empty()) {
        size_t comma = corners.find(',');
        std::string_view text = TrimSpace(corners.substr(0, comma));
        double value = 0;
        std::from_chars(text.data(), text.data() + text.size(), value);
        largest = std::max(largest, value);
        corners = comma == std::string_view::npos ? std::string_view{} : corners.substr(comma + 1);
    }
    return largest;
}

bool Sets(const Theme& theme, std::string_view selector, std::string_view property) {
    return std::any_of(theme.assignments.begin(), theme.assignments.end(), [&](const Assignment& assignment) {
        return !assignment.media && assignment.selector.trigger == Trigger::Always &&
               assignment.selector.text == selector && assignment.property == property;
    });
}

// The progress line runs along the bottom of the song's display, up to its right edge: a theme that rounds the display
// cut its end off with the corner. It stops before the corner.
std::string ScrubberClearOfCorners(const Theme& theme) {
    if (Sets(theme, ".scrubber", "Margin")) return {};
    double radius = 0;
    int order = -1;
    for (const Assignment& assignment : theme.assignments) {
        if (assignment.media || assignment.selector.trigger != Trigger::Always) continue;
        if (assignment.selector.text == ".lcd" && assignment.property == "CornerRadius" && assignment.order >= order) {
            order = assignment.order;
            radius = LargestCorner(assignment.value.text);
        }
    }
    if (radius <= 0) return {};
    return ".scrubber { margin: 0 " + FormatNumber(radius) + " 0 0; }\n";
}

// Apple Music paints an album's artist in its own blue, in code: the theme's link colour takes over, as WinUI's links
// do (DefineAccentControls). On a light theme, Apple's light blue barely read.
std::string ArtistInLinkColour(const Theme& theme) {
    if (Sets(theme, ".album-artist", "Foreground")) return {};
    for (const char* key : {"AccentTextFillColorPrimaryBrush", kAccent}) {
        if (theme.palette.Find(key)) return ".album-artist { color: $" + std::string(key) + "; }\n";
    }
    return {};
}

// Apple Music paints the tiles of a search's top results in its own grey, in code: the theme's card colour instead.
std::string SearchTilesInCardColour(const Theme& theme) {
    if (Sets(theme, ".search-top-result", "Background")) return {};
    if (!theme.palette.Find("CardBackgroundFillColorDefaultBrush")) return {};
    return ".search-top-result { background: $CardBackgroundFillColorDefaultBrush; }\n";
}

// Rules a theme's own choices call for, below its user.css so that a theme which sets the same thing keeps its value.
void AddDerivedRules(const ValueContext& context, Theme& theme, const ThemeParts& parts) {
    bool applied = parts.resources;
    std::string css = ScrubberClearOfCorners(theme);
    if (parts.colors) css += ArtistInLinkColour(theme) + SearchTilesInCardColour(theme);
    if (css.empty()) return;
    RuleSource source;
    source.file = "Ampify (derived from the theme)";
    source.layer = -1;
    LoadRuleText(css, context, theme, applied, source);
}

}  // namespace

Theme LoadTheme(const std::string& id, const std::string& folder, FileSource& files, const LoadOptions& options) {
    Theme theme;
    theme.id = id;
    theme.name = id;

    std::set<std::string> spicetify = LoadColors(files, options.scheme, options.parts.colors, theme);

    // A part that is off is still checked, so the problems show up before the part is turned on.
    bool dictionaryOk = LoadDictionary(files, options, theme);
    bool hasRules = files.Exists("user.css");

    bool isTheme = !theme.schemes.empty() || files.Exists("resources.xaml") || hasRules;
    if (!isTheme) {
        theme.problems.push_back("no theme file (colors.ini, resources.xaml or user.css): not a theme");
        return theme;
    }

    theme.usable = dictionaryOk;
    if (!dictionaryOk) return theme;

    if (!options.parts.resources) theme.resourceDictionary.clear();
    DefineDerivedColors(theme, options.parts.colors);
    LoadSettings(files, options, theme);
    for (const auto& [name, value] : options.artwork) theme.variables[name] = value;
    ValueContext context{theme.palette, files, folder, options.parts.assets};
    // Spicetify's colours paint Apple Music's own elements first; the theme's user.css comes on top.
    if (std::string translated = SpicetifyStylesheet(spicetify); !translated.empty()) {
        RuleSource source;
        source.file = "color.ini (traduction Spicetify)";
        source.layer = -1;
        LoadRuleText(translated, context, theme, options.parts.colors, source);
    }
    LoadRules(files, context, theme, options.parts.resources, RuleSource{});
    AddDerivedRules(context, theme, options.parts);
    return theme;
}

Theme LoadSnippet(const std::string& id, const std::string& folder, FileSource& files, const Theme* base,
                  const LoadOptions& options) {
    Theme snippet;
    snippet.id = id;
    snippet.name = id;
    if (base) {
        snippet.palette = base->palette;
        snippet.variables = base->variables;
        snippet.settings = base->settings;
        snippet.keyframes = base->keyframes;
    }

    if (files.Exists("colors.ini")) {
        snippet.problems.push_back("colors.ini ignored: a snippet uses the colors of the active theme ($name)");
    }

    bool dictionaryOk = LoadDictionary(files, options, snippet);
    bool isSnippet = files.Exists("resources.xaml") || files.Exists("user.css");
    if (!isSnippet) {
        snippet.problems.push_back("no snippet file (resources.xaml or user.css): not a snippet");
        return snippet;
    }
    snippet.usable = dictionaryOk;
    if (!dictionaryOk) return snippet;
    if (IsEmptyDictionary(snippet.resourceDictionary)) snippet.resourceDictionary.clear();

    ValueContext context{snippet.palette, files, folder, true};
    RuleSource source;
    source.file = "snippets/" + id + "/user.css";
    source.base = base;
    LoadRules(files, context, snippet, true, source);
    return snippet;
}

std::vector<std::string> ReadSchemes(FileSource& files) {
    std::vector<std::string> schemes;
    std::optional<std::string> text = ReadColorsFile(files).second;
    if (!text) return schemes;

    Ini ini;
    ini.Parse(*text);
    for (const Ini::Section& section : ini.Sections()) {
        if (!section.name.empty()) schemes.push_back(section.name);
    }
    return schemes;
}

std::string ReadDefaultScheme(FileSource& files) {
    std::optional<std::string> text = files.Read("manifest.json");
    std::optional<Json> manifest = text ? Json::Parse(*text) : std::nullopt;
    const Json* scheme = manifest ? manifest->Find("defaultScheme") : nullptr;
    const std::string* name = scheme ? scheme->AsString() : nullptr;
    return name ? *name : std::string();
}

namespace {

// The position of the bracket that closes the one at `open`, or npos.
size_t ClosingBracket(std::string_view text, size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); i++) {
        if (text[i] == '(') depth++;
        if (text[i] == ')' && --depth == 0) return i;
    }
    return std::string_view::npos;
}

}  // namespace

std::optional<std::string> SubstituteVariables(std::string_view value, const std::map<std::string, std::string>& variables,
                                               std::string* problem) {
    std::string text(value);
    // Each pass replaces the first var(...). A fallback may hold another var(), so a few passes may be needed; the
    // limit stops a value that would grow forever.
    for (int pass = 0; pass < 32; pass++) {
        std::string lowered = ToLowerAscii(text);
        size_t start = lowered.find("var(");
        if (start == std::string::npos) return text;

        size_t close = ClosingBracket(text, start + 3);
        if (close == std::string::npos) {
            if (problem) *problem = "var( without a closing parenthesis";
            return std::nullopt;
        }

        std::string_view inside = std::string_view(text).substr(start + 4, close - start - 4);
        size_t comma = inside.find(',');
        std::string name = ToLowerAscii(TrimSpace(inside.substr(0, comma)));
        if (!name.starts_with("--") || name.size() == 2) {
            if (problem) *problem = "var() needs a --variable name";
            return std::nullopt;
        }

        std::string replacement;
        auto it = variables.find(name.substr(2));
        if (it != variables.end()) {
            replacement = it->second;
        } else if (comma != std::string_view::npos) {
            replacement = std::string(TrimSpace(inside.substr(comma + 1)));
        } else {
            if (problem) *problem = "variable \"" + name + "\" is not defined";
            return std::nullopt;
        }
        text = text.substr(0, start) + replacement + text.substr(close + 1);
    }
    if (problem) *problem = "too many nested var()";
    return std::nullopt;
}

namespace {

struct Quantity {
    double number = 0;
    std::string unit;  // empty for a plain number
};

// Reads the inside of one calc(): sums of products of numbers, brackets and nested calc(), in CSS's order.
class CalcReader {
public:
    explicit CalcReader(std::string_view text) : m_text(text) {}

    std::optional<Quantity> Read() {
        std::optional<Quantity> result = Sum();
        SkipSpaces();
        if (result && m_pos != m_text.size()) return Fail("calc() could not read \"" + std::string(m_text) + "\"");
        return result;
    }

    const std::string& Problem() const { return m_problem; }

private:
    std::optional<Quantity> Fail(std::string problem) {
        if (m_problem.empty()) m_problem = std::move(problem);
        return std::nullopt;
    }

    void SkipSpaces() {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) m_pos++;
    }

    bool Take(char c) {
        SkipSpaces();
        if (m_pos >= m_text.size() || m_text[m_pos] != c) return false;
        m_pos++;
        return true;
    }

    std::optional<Quantity> Sum() {
        std::optional<Quantity> left = Product();
        while (left) {
            bool add = Take('+');
            if (!add && !Take('-')) break;
            std::optional<Quantity> right = Product();
            if (!right) return std::nullopt;
            if (!left->unit.empty() && !right->unit.empty() && left->unit != right->unit) {
                return Fail("calc() adds " + right->unit + " to " + left->unit);
            }
            left->number += add ? right->number : -right->number;
            if (left->unit.empty()) left->unit = right->unit;
        }
        return left;
    }

    std::optional<Quantity> Product() {
        std::optional<Quantity> left = Factor();
        while (left) {
            bool multiply = Take('*');
            if (!multiply && !Take('/')) break;
            std::optional<Quantity> right = Factor();
            if (!right) return std::nullopt;
            if (multiply) {
                if (!left->unit.empty() && !right->unit.empty()) return Fail("calc() multiplies two lengths");
                left->number *= right->number;
                if (left->unit.empty()) left->unit = right->unit;
                continue;
            }
            if (!right->unit.empty()) return Fail("calc() divides by a length");
            if (right->number == 0) return Fail("calc() divides by zero");
            left->number /= right->number;
        }
        return left;
    }

    std::optional<Quantity> Factor() {
        // A sign before a bracket or a number: -(1 + 2), +4.
        if (Take('+')) return Factor();
        if (Take('-')) {
            std::optional<Quantity> inner = Factor();
            if (inner) inner->number = -inner->number;
            return inner;
        }
        SkipSpaces();
        if (ToLowerAscii(m_text.substr(m_pos, 5)) == "calc(") m_pos += 4;
        if (Take('(')) {
            std::optional<Quantity> inner = Sum();
            if (!inner) return std::nullopt;
            if (!Take(')')) return Fail("calc() is missing a closing parenthesis");
            return inner;
        }

        Quantity quantity;
        const char* begin = m_text.data() + m_pos;
        auto [end, error] = std::from_chars(begin, m_text.data() + m_text.size(), quantity.number);
        if (error != std::errc() || !std::isfinite(quantity.number)) {
            return Fail("calc() expects a number in \"" + std::string(m_text) + "\"");
        }
        m_pos += end - begin;
        while (m_pos < m_text.size() && (std::isalpha(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '%')) {
            quantity.unit += static_cast<char>(std::tolower(static_cast<unsigned char>(m_text[m_pos++])));
        }
        return quantity;
    }

    std::string_view m_text;
    size_t m_pos = 0;
    std::string m_problem;
};

std::string FormatQuantity(const Quantity& quantity) {
    // Four decimals are plenty for pixels and milliseconds, and keep 0.1 + 0.2 from printing as 0.30000000000000004.
    double rounded = std::round(quantity.number * 10000) / 10000;
    if (rounded == 0) rounded = 0;  // no "-0"
    char buffer[32];
    auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), rounded);
    return std::string(buffer, error == std::errc() ? end : buffer) + quantity.unit;
}

}  // namespace

std::optional<std::string> EvaluateCalc(std::string_view value, std::string* problem) {
    std::string text(value);
    for (int pass = 0; pass < 32; pass++) {
        size_t start = ToLowerAscii(text).find("calc(");
        if (start == std::string::npos) return text;

        size_t close = ClosingBracket(text, start + 4);
        if (close == std::string::npos) {
            if (problem) *problem = "calc( without a closing parenthesis";
            return std::nullopt;
        }

        CalcReader reader(std::string_view(text).substr(start + 5, close - start - 5));
        std::optional<Quantity> result = reader.Read();
        if (!result) {
            if (problem) *problem = reader.Problem();
            return std::nullopt;
        }
        text = text.substr(0, start) + FormatQuantity(*result) + text.substr(close + 1);
    }
    if (problem) *problem = "too many calc()";
    return std::nullopt;
}

bool IsEmptyDictionary(std::string_view xaml) {
    size_t open = xaml.find("<ResourceDictionary");
    if (open == std::string_view::npos) return TrimSpace(xaml).empty();
    size_t end = xaml.find('>', open);
    if (end == std::string_view::npos) return false;
    if (end > 0 && xaml[end - 1] == '/') return true;

    size_t close = xaml.rfind("</ResourceDictionary>");
    if (close == std::string_view::npos || close < end) return false;
    std::string body(xaml.substr(end + 1, close - end - 1));
    // Comments do not count as entries.
    for (size_t comment = body.find("<!--"); comment != std::string::npos; comment = body.find("<!--")) {
        size_t stop = body.find("-->", comment);
        body.erase(comment, stop == std::string::npos ? std::string::npos : stop + 3 - comment);
    }
    return TrimSpace(body).empty();
}

std::vector<std::string> XamlKeys(std::string_view xaml) {
    std::vector<std::string> keys;
    const std::string_view marker = "x:Key=";
    size_t pos = 0;
    while ((pos = xaml.find(marker, pos)) != std::string_view::npos) {
        pos += marker.size();
        if (pos >= xaml.size()) break;

        char quote = xaml[pos];
        if (quote != '"' && quote != '\'') continue;

        size_t end = xaml.find(quote, pos + 1);
        if (end == std::string_view::npos) break;
        keys.emplace_back(xaml.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    return keys;
}

}  // namespace ampify::theme
