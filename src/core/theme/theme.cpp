#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

#include "../json.hpp"
#include "../manifest.hpp"
#include "../text_util.hpp"
#include "builtins.hpp"
#include "css_values.hpp"
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

// The selected rows of song lists and cards of grids stay in Apple Music's grey unless a theme names them: Spicetify's
// selected-row when the theme gives one, otherwise a veil of the accent (#33, #44, #55 as the themes that set them).
void DefineSelectedRows(Theme& theme, const std::set<std::string>& owned, bool applied) {
    std::optional<Argb> row = theme.palette.Find("selected-row");
    const char* from = row ? "selected-row" : kAccent;
    std::optional<Argb> base = row ? row : theme.palette.Find(kAccent);
    if (!base) return;
    constexpr std::pair<const char*, std::uint8_t> kStates[] = {
        {"ListViewItemBackgroundSelected", 0x33},           {"ListViewItemBackgroundSelectedPointerOver", 0x44},
        {"ListViewItemBackgroundSelectedPressed", 0x55},    {"GridViewItemBackgroundSelected", 0x33},
        {"GridViewItemBackgroundSelectedPointerOver", 0x44}, {"GridViewItemBackgroundSelectedPressed", 0x55},
    };
    for (const auto& [key, alpha] : kStates) {
        Argb color = *base;
        if (!row) color.a = alpha;
        DefineDerived(theme, owned, from, key, color, applied);
    }
}

// The frost of a page header (a BackDrop in Apple's PageTemplates) is tinted by ContentAreaBackgroundBrush from its
// first frame; the theme's pass that hides it comes a frame later, so in Apple's grey it flashed over a darker page.
void DefinePageHeaderFrost(Theme& theme, const std::set<std::string>& owned, bool applied) {
    constexpr const char* kPage = "ApplicationPageBackgroundThemeBrush";
    if (std::optional<Argb> page = theme.palette.Find(kPage)) {
        DefineDerived(theme, owned, kPage, "ContentAreaBackgroundBrush", *page, applied);
    }
}

// Apple's shelf headers, captions and the search field's hint read Apple's own SecondaryTextBrush, white at 51%: on a
// theme's page lighter than Apple's it fell under 4.5:1, so it takes the theme's secondary text. Only for a dark
// scheme: Apple's dark-mode captions over artwork read the same key, where a light scheme's dark text would sink.
void DefineAppleSecondaryText(Theme& theme, const std::set<std::string>& owned, bool applied) {
    constexpr const char* kSecondary = "TextFillColorSecondaryBrush";
    std::optional<Argb> secondary = theme.palette.Find(kSecondary);
    std::optional<bool> dark = DarkFromPalette(theme.palette);
    if (!secondary || !dark || !*dark) return;
    DefineDerived(theme, owned, kSecondary, "SecondaryTextBrush", *secondary, applied);
}

// The colours a theme leaves to Ampify, once its own resources.xaml is known.
void DefineDerivedColors(Theme& theme, bool applied) {
    std::vector<std::string> keys = XamlKeys(theme.resourceDictionary);
    std::set<std::string> owned(keys.begin(), keys.end());
    DefineAccentStates(theme, owned, applied);
    DefineTextOnAccent(theme, owned, applied);
    DefineAccentControls(theme, owned, applied);
    DefineSelectedRows(theme, owned, applied);
    DefineAcrylicSurfaces(theme, owned, applied);
    DefinePageHeaderFrost(theme, owned, applied);
    DefineAppleSecondaryText(theme, owned, applied);
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

// theme.accent over the scheme: its accent and Spicetify's button and playback bar take the chosen colour. The colours a
// scheme gives along with its own accent (hover and pressed accents, text on the accent, links) would clash with it:
// they go, and Ampify derives them from the chosen accent (DefineDerivedColors). Keys of the theme's resources.xaml
// stay: Ampify does not rewrite a theme's own XAML.
void ChooseAccent(Theme& theme, Argb accent, bool applied) {
    static const std::set<std::string> kFollowers = {
        ToLowerAscii(kAccent),
        "accentfillcolorsecondarybrush",
        "accentfillcolortertiarybrush",
        "textonaccentfillcolorprimarybrush",
        "textonaccentfillcolorsecondarybrush",
        "accenttextfillcolorprimarybrush",
        "accenttextfillcolorsecondarybrush",
        "accenttextfillcolortertiarybrush",
    };
    for (const std::string& key : kFollowers) theme.palette.Remove(key);
    theme.palette.Remove("button-active");
    std::erase_if(theme.resources, [](const ResourceOverride& resource) {
        return kFollowers.count(ToLowerAscii(resource.key)) > 0;
    });
    theme.palette.Set(kAccent, accent);
    if (applied) theme.resources.push_back({kAccent, {ValueKind::Color, ToXamlColor(accent)}});
    for (const char* spicetify : {"button", "playback-bar"}) {
        if (!theme.palette.Find(spicetify)) continue;
        theme.palette.Set(spicetify, accent);
        theme.variables[std::string("spice-") + spicetify] = ToXamlColor(accent);
    }
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

// A rule inside @setting blocks counts only while all their conditions hold. A key the theme does not declare never
// holds; every such key is reported, even when another condition already fails.
bool SettingHolds(const Rule& rule, const std::string& file, Theme& theme) {
    bool holds = true;
    for (const SettingCondition& condition : rule.settings) {
        auto it = theme.settings.find(condition.key);
        if (it == theme.settings.end()) {
            theme.problems.push_back(AtLine(file, rule.line) + ": @setting " + condition.key +
                                     ": the theme's manifest declares no such setting, ignored");
            holds = false;
            continue;
        }
        bool equal = condition.value ? ToLowerAscii(it->second) == ToLowerAscii(*condition.value)
                                     : it->second == "true";
        holds = holds && equal != condition.negated;
    }
    return holds;
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
            std::optional<std::string> value = ResolveValue(declaration.value, theme.variables, &problem);
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

// Why a declaration cannot go with this selector, or nothing. A move is not a value that can come and go with a state:
// the other items would shift each time. `content` makes the image of ::before and ::after, which takes only what an
// image has.
std::optional<std::string> MisplacedProperty(const Selector& selector, const PropertySpec& spec,
                                             const Declaration& declaration) {
    const std::string name = "\"" + declaration.property + "\"";
    const bool moves = spec.kind == PropertyKind::Order || spec.kind == PropertyKind::Target;
    if (selector.trigger != Trigger::Always && moves) {
        return name + " is not possible under " + TriggerName(selector.trigger) + " (" + selector.text + ")";
    }
    if (selector.pseudo == PseudoElement::None && spec.kind == PropertyKind::Content) {
        return name + " only applies to ::before and ::after (" + selector.text + ")";
    }
    if (selector.pseudo != PseudoElement::None && !AppliesToDecor(spec.xaml)) {
        return name + " does not apply to " + PseudoElementName(selector.pseudo) + " (" + selector.text +
               "): content, size, margin, alignment, grid cell, z-index, opacity, visibility, transform and animation do";
    }
    return std::nullopt;
}

void AddAssignment(const Rule& rule, const PropertySpec& spec, const Value& value, const Declaration& declaration,
                   int order, const RuleSource& source, Theme& theme) {
    for (const Selector& selector : rule.selectors) {
        if (std::optional<std::string> problem = MisplacedProperty(selector, spec, declaration)) {
            theme.problems.push_back(AtLine(source.file, declaration.line) + ": " + *problem + ", ignored");
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
    Stylesheet sheet = ParseThemeStylesheet(css);
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
            std::optional<std::string> raw = ResolveValue(declaration.value, theme.variables, &problem);
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
                std::string_view hint = PropertyHint(declaration.property);
                theme.problems.push_back(AtLine(from.file, declaration.line) + ": property \"" +
                                         declaration.property + "\" is unknown, ignored" +
                                         (hint.empty() ? std::string() : " (" + std::string(hint) + ")"));
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
    for (std::string_view corner : SplitArguments(corners)) {
        largest = std::max(largest, ParseNumber(corner).value_or(0));
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
// cut its end off with the corner. It stops where the corner's curve meets the line, 2 above the display's edge: a
// margin of the whole radius left a gap at the end that read as a line too short.
constexpr double kScrubberLift = 2;

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
    if (radius <= kScrubberLift) return {};
    // Half of it: the line's template repeats its margin inside (see .scrubber in builtins.cpp).
    double curve = radius - std::sqrt(radius * radius - (radius - kScrubberLift) * (radius - kScrubberLift));
    double inset = std::ceil(curve / 2);
    return ".scrubber { margin: 0 " + FormatNumber(inset) + " 0 0; }\n";
}

// An element of Apple Music's whose height Apple fixes below what its template asks for: WinUI then clips the template
// and all it holds to that height, so a ::before or ::after image put inside showed as a sliver (a Nyan Cat on the
// progress line, 6 high, whose template asks for 18). Apple's bottom-aligned slots grow upwards: moved down by what
// they gained, their first `height` pixels stay where they were.
struct FixedSlot {
    std::string_view name;      // the stable name (builtins.cpp)
    std::string_view selector;  // the element it names, for a theme that writes it in full
    double height;              // Apple's
    double asked;               // what its template asks for: the least room that clips nothing
};

constexpr FixedSlot kFixedSlots[] = {
    {".scrubber", "ScrubberControl#LCDScrubber", 6, 18},
};

bool InSlot(const std::string& selector, const FixedSlot& slot) {
    std::string_view element = slot.selector.substr(slot.selector.find('#') + 1);
    return selector.find(slot.name) != std::string::npos || selector.find(element) != std::string::npos;
}

double LengthOrZero(std::string_view text) {
    double value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && std::isfinite(value) ? value : 0;
}

// The height a decor takes in its panel's layout: its own, plus its top and bottom margins (negative ones give room
// back, as the progress mascot's do, so it takes none).
double DecorLayoutHeight(const Theme& theme, const Assignment& content) {
    double height = 0;
    double margins = 0;
    for (const Assignment& assignment : theme.assignments) {
        if (assignment.selector.text != content.selector.text || assignment.selector.pseudo != content.selector.pseudo) {
            continue;
        }
        if (assignment.property == "Height") height = LengthOrZero(assignment.value.text);
        if (assignment.property == "Margin") {
            std::string_view sides = assignment.value.text;  // left,top,right,bottom
            size_t top = sides.find(','), right = sides.find(',', top + 1), bottom = sides.find(',', right + 1);
            if (bottom == std::string_view::npos) continue;
            margins = LengthOrZero(sides.substr(top + 1, right - top - 1)) + LengthOrZero(sides.substr(bottom + 1));
        }
    }
    return std::max(0.0, height + margins);
}

std::string RoomForDecors(const Theme& theme) {
    std::string css;
    for (const FixedSlot& slot : kFixedSlots) {
        double room = 0;
        for (const Assignment& assignment : theme.assignments) {
            if (assignment.selector.pseudo == PseudoElement::None || assignment.property != kContentProperty ||
                assignment.value.text.empty() || !InSlot(assignment.selector.text, slot)) {
                continue;
            }
            room = std::max({room, slot.asked, DecorLayoutHeight(theme, assignment)});
        }
        const std::string full(slot.selector);
        if (room <= slot.height || Sets(theme, slot.name, "Height") || Sets(theme, full, "Height") ||
            Sets(theme, slot.name, "RenderTransform") || Sets(theme, full, "RenderTransform")) {
            continue;
        }
        css += std::string(slot.name) + " { height: " + FormatNumber(room) + "; transform: translateY(" +
               FormatNumber(room - slot.height) + "px); }\n";
    }
    return css;
}

// The volume slider takes the accent through WinUI's slider keys (DefineAccentControls), but Apple Music's progress line
// reads keys of its own and stays white: it takes the accent too, as Spicetify's playback bar takes `button`.
std::string ScrubberInAccent(const Theme& theme) {
    if (Sets(theme, ".scrubber", "Foreground") || Sets(theme, "ScrubberControl#LCDScrubber", "Foreground")) return {};
    if (!theme.palette.Find(kAccent)) return {};
    return std::string(".scrubber { color: $") + kAccent + "; }\n";
}

// Apple's "now playing" bars take the accent once, when their row is built: a new song's row is built before its
// accent comes (theme.accent = artwork) and kept the colour of the song before. Set on the bars, it follows each change.
std::string NowPlayingBarsInAccent(const Theme& theme) {
    if (Sets(theme, ".now-playing-indicator", "Foreground")) return {};
    if (!theme.palette.Find(kAccent)) return {};
    return std::string(".now-playing-indicator { color: $") + kAccent + "; }\n";
}

// Apple Music's shuffle icon, while on, takes a blue of Apple's own (its button's ToggleButtonForegroundChecked keys),
// while repeat and both dots take the accent: shuffle takes the accent too.
std::string ShuffleInAccent(const Theme& theme) {
    if (Sets(theme, ".shuffle-button", kCheckedForegroundProperty)) return {};
    if (!theme.palette.Find(kAccent)) return {};
    return std::string(".shuffle-button { checked-color: $") + kAccent + "; }\n";
}

// Apple Music's Up Next paints "Clear" in its own red and the autoplay toggle, while on, in its own blue, colours of
// their templates that no theme key reaches: both take the accent, so the pane reads in the theme's colours.
std::string UpNextInAccent(const Theme& theme) {
    if (!theme.palette.Find(kAccent)) return {};
    std::string css;
    if (!Sets(theme, ".queue-clear", "Foreground")) css += std::string(".queue-clear { color: $") + kAccent + "; }\n";
    if (!Sets(theme, ".queue-autoplay", kCheckedForegroundProperty)) {
        css += std::string(".queue-autoplay { checked-color: $") + kAccent + "; }\n";
    }
    return css;
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

// A tile's subtitle that links to the artist keeps the colour Apple gives its link in code, for the mode the page was
// built in: white on a light theme's page, unreadable. It takes the theme's secondary text, as the plain subtitles do,
// at full opacity: Apple fades it to 70% for its own grey, which put a theme's secondary text under 4.5:1.
std::string CardLinksInSecondaryText(const Theme& theme) {
    if (!theme.palette.Find("TextFillColorSecondaryBrush")) return {};
    std::string declarations;
    if (!Sets(theme, ".card-subtitle", "Foreground")) declarations += " color: $TextFillColorSecondaryBrush;";
    if (!Sets(theme, ".card-subtitle", "Opacity")) declarations += " opacity: 1;";
    return declarations.empty() ? std::string() : ".card-subtitle {" + declarations + " }\n";
}

// Apple Music paints the tiles of a search's top results in its own grey, in code: the theme's card colour instead.
std::string SearchTilesInCardColour(const Theme& theme) {
    if (Sets(theme, ".search-top-result", "Background")) return {};
    if (!theme.palette.Find("CardBackgroundFillColorDefaultBrush")) return {};
    return ".search-top-result { background: $CardBackgroundFillColorDefaultBrush; }\n";
}

// Apple Music's lists give their rows' selection colours in their own style, which never reads the ListViewItem keys
// of colors.ini: the rows take them from these rules instead (DefineSelectedRows fills the keys a theme leaves out).
std::string SelectedRowsInThemeColour(const Theme& theme) {
    constexpr std::pair<const char*, const char*> kStates[] = {
        {"selected-background", "ListViewItemBackgroundSelected"},
        {"selected-hover-background", "ListViewItemBackgroundSelectedPointerOver"},
        {"selected-active-background", "ListViewItemBackgroundSelectedPressed"},
    };
    std::string declarations;
    for (const auto& [property, key] : kStates) {
        if (!theme.palette.Find(key) || Sets(theme, ".list-item", FindProperty(property)->xaml)) continue;
        declarations += std::string(" ") + property + ": $" + key + ";";
    }
    return declarations.empty() ? std::string() : ".list-item {" + declarations + " }\n";
}

// A theme that lights a song of a list under the pointer (.track-row:hover) showed two lights: its own, and Apple's
// list row across the whole width, a square edge out of the theme's. The theme's light stays alone.
std::string OneLightPerSong(const Theme& theme) {
    bool lit = std::any_of(theme.assignments.begin(), theme.assignments.end(), [](const Assignment& assignment) {
        const Selector& selector = assignment.selector;
        return selector.trigger == Trigger::Hover && assignment.property == "Background" && !selector.parts.empty() &&
               selector.parts.back().type == "TracklistGrid" && selector.parts.back().name == "RootGrid";
    });
    if (!lit || Sets(theme, ".track-list-item", "PointerOverBackground")) return {};
    return ".track-list-item { hover-background: transparent; active-background: transparent; }\n";
}

// A theme that lights a tile under the pointer (.card:hover) drew its light right against the artwork and the titles:
// the tile keeps room inside, as Spotify's do.
std::string RoomInsideALitCard(const Theme& theme) {
    bool lit = std::any_of(theme.assignments.begin(), theme.assignments.end(), [](const Assignment& assignment) {
        const Selector& selector = assignment.selector;
        return selector.trigger == Trigger::Hover && assignment.property == "Background" && !selector.parts.empty() &&
               selector.parts.back().type == "HoverGrid" && selector.parts.back().name == "SquareLockupContent";
    });
    if (!lit || Sets(theme, ".card", "Padding")) return {};
    return ".card { padding: 8; }\n";
}

// Rules a theme's own choices call for, below its user.css so that a theme which sets the same thing keeps its value.
void AddDerivedRules(const ValueContext& context, Theme& theme, const ThemeParts& parts) {
    bool applied = parts.resources;
    std::string css = ScrubberClearOfCorners(theme) + RoomForDecors(theme) + OneLightPerSong(theme) +
                      RoomInsideALitCard(theme);
    if (parts.colors) {
        css += ScrubberInAccent(theme) + NowPlayingBarsInAccent(theme) + ShuffleInAccent(theme) + UpNextInAccent(theme) +
               ArtistInLinkColour(theme) +
               CardLinksInSecondaryText(theme) +
               SearchTilesInCardColour(theme) + SelectedRowsInThemeColour(theme);
    }
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
    if (options.accent) ChooseAccent(theme, *options.accent, options.parts.colors);
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

namespace {

// The files of a theme that has none.
class NoFiles final : public FileSource {
public:
    std::optional<std::string> Read(std::string_view) override { return std::nullopt; }
    bool Exists(std::string_view) override { return false; }
};

}  // namespace

Theme LoadAccentOnly(const LoadOptions& options) {
    Theme theme;
    theme.id = kAccentOnlyId;
    theme.name = "accent colour";
    theme.usable = true;
    if (!options.accent) return theme;
    ChooseAccent(theme, *options.accent, options.parts.colors);
    DefineDerivedColors(theme, options.parts.colors);
    NoFiles files;
    ValueContext context{theme.palette, files, "", false};
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

    // Its own settings, on top of the theme's: a key both declare is the snippet's in its user.css.
    LoadSettings(files, options, snippet);
    // The artwork's colours, also with no theme to bring them (Album colours over Apple Music's own look).
    for (const auto& [name, value] : options.artwork) snippet.variables[name] = value;
    ValueContext context{snippet.palette, files, folder, true};
    RuleSource source;
    source.file = "snippets/" + id + "/user.css";
    source.base = base;
    LoadRules(files, context, snippet, true, source);
    if (std::string css = RoomForDecors(snippet); !css.empty()) {
        RuleSource derived;
        derived.file = "Ampify (derived from the snippet)";
        derived.layer = -1;
        LoadRuleText(css, context, snippet, true, derived);
    }
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
