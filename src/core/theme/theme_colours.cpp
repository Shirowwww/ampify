// A theme's colours: colors.ini, the accent, the derived resources and resources.xaml.

#include "theme.hpp"
#include "theme_detail.hpp"

namespace ampify::theme {

using namespace detail;

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
        if (std::optional<Argb> color = palette.Find(key)) return RelativeLuminance(*color) < kDarkBelowLuminance;
    }
    return std::nullopt;
}

// Light or dark, from the background the scheme gives (Spicetify's main, or the app's page background), else from the
// scheme's name.
std::optional<bool> DarkOf(const Palette& palette, const std::string& scheme) {
    std::optional<bool> dark = DarkFromPalette(palette);
    return dark ? dark : DarkByName(scheme);
}

// A brush the theme does not name, worked out from one it does. The theme's resources.xaml has the last word: a key
// it defines, or the one the colour comes from, is left to it.
void DefineDerived(Colours& colours, const std::set<std::string>& owned, const char* from, const char* key,
                   Argb color) {
    if (owned.count(from) || owned.count(key)) return;
    colours.Define(key, color);
}

// Fluent's accent under the pointer and while pressed is the accent at 90% and 80%. A theme that names only the
// accent would otherwise show Apple Music's own colour on hover.
void DefineAccentStates(Colours& colours, const std::set<std::string>& owned) {
    constexpr std::pair<const char*, double> kStates[] = {{"AccentFillColorSecondaryBrush", 0.9},
                                                          {"AccentFillColorTertiaryBrush", 0.8}};
    std::optional<Argb> accent = colours.theme.palette.Find(kAccent);
    if (!accent) return;
    for (const auto& [key, opacity] : kStates) {
        Argb color = *accent;
        color.a = static_cast<std::uint8_t>(std::lround(accent->a * opacity));
        DefineDerived(colours, owned, kAccent, key, color);
    }
}

// Apple Music lays white text on the accent whatever its colour. On an accent where white reads under 3:1 (a light
// cyan, Spotify's green, a yellow) and that names no colour for its text, the text goes black, as in Fluent's dark
// mode. Any other accent keeps Apple Music's white.
void DefineTextOnAccent(Colours& colours, const std::set<std::string>& owned) {
    const Theme& theme = colours.theme;
    constexpr const char* kPrimary = "TextOnAccentFillColorPrimaryBrush";
    constexpr const char* kSecondary = "TextOnAccentFillColorSecondaryBrush";
    constexpr double kMinimumWhiteContrast = 3.0;
    constexpr Argb kBlack{0xFF, 0, 0, 0};
    constexpr Argb kBlackSecondary{0xB3, 0, 0, 0};  // 70%, as Fluent's secondary text on the accent
    std::optional<Argb> accent = theme.palette.Find(kAccent);
    if (!accent || theme.palette.Find(kPrimary) || owned.count(kPrimary)) return;
    if (1.05 / (RelativeLuminance(*accent) + 0.05) >= kMinimumWhiteContrast) return;
    for (const auto& [key, color] : {std::pair{kPrimary, kBlack}, std::pair{kSecondary, kBlackSecondary}}) {
        DefineDerived(colours, owned, kAccent, key, color);
    }
}

// WinUI's toggles, sliders, check boxes, selection marks, links and system accent brushes read keys of their own,
// which its dictionaries set to the accent once, when they load: a theme's accent alone left them in Apple Music's
// blue. The list is every key the inspector found still holding it. Brushes only: writing the SystemAccentColor
// colours instead left the theme's accent in place after the theme went.
void DefineAccentControls(Colours& colours, const std::set<std::string>& owned) {
    const Theme& theme = colours.theme;
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
        DefineDerived(colours, owned, derived.from, derived.key, *color);
    }
}

// Ampify's extension panels and WinUI's flyouts lie over the page in Fluent's grey acrylic, foreign to any theme. An
// opaque card colour takes its place; a see-through one would let the page show through them.
void DefineAcrylicSurfaces(Colours& colours, const std::set<std::string>& owned) {
    const Theme& theme = colours.theme;
    constexpr const char* kCard = "CardBackgroundFillColorDefaultBrush";
    std::optional<Argb> card = theme.palette.Find(kCard);
    if (!card || card->a != 0xFF) return;
    for (const char* key : {"AcrylicInAppFillColorDefaultBrush", "AcrylicBackgroundFillColorDefaultBrush"}) {
        DefineDerived(colours, owned, kCard, key, *card);
    }
}

// The selected rows of song lists and cards of grids stay in Apple Music's grey unless a theme names them: Spicetify's
// selected-row when the theme gives one, otherwise a veil of the accent (#33, #44, #55 as the themes that set them).
void DefineSelectedRows(Colours& colours, const std::set<std::string>& owned) {
    const Theme& theme = colours.theme;
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
        DefineDerived(colours, owned, from, key, color);
    }
}

// The frost of a page header (a BackDrop in Apple's PageTemplates) is tinted by ContentAreaBackgroundBrush from its
// first frame; the theme's pass that hides it comes a frame later, so in Apple's grey it flashed over a darker page.
void DefinePageHeaderFrost(Colours& colours, const std::set<std::string>& owned) {
    const Theme& theme = colours.theme;
    constexpr const char* kPage = "ApplicationPageBackgroundThemeBrush";
    if (std::optional<Argb> page = theme.palette.Find(kPage)) {
        DefineDerived(colours, owned, kPage, "ContentAreaBackgroundBrush", *page);
    }
}

// Apple's shelf headers, captions and the search field's hint read Apple's own SecondaryTextBrush, white at 51%: on a
// theme's page lighter than Apple's it fell under 4.5:1, so it takes the theme's secondary text. Only for a dark
// scheme: Apple's dark-mode captions over artwork read the same key, where a light scheme's dark text would sink.
void DefineAppleSecondaryText(Colours& colours, const std::set<std::string>& owned) {
    const Theme& theme = colours.theme;
    constexpr const char* kSecondary = "TextFillColorSecondaryBrush";
    std::optional<Argb> secondary = theme.palette.Find(kSecondary);
    std::optional<bool> dark = DarkFromPalette(theme.palette);
    if (!secondary || !dark || !*dark) return;
    DefineDerived(colours, owned, kSecondary, "SecondaryTextBrush", *secondary);
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

}  // namespace

namespace detail {

// The colours file: colors.ini, or color.ini as Spicetify names it. Empty name when there is none.
std::pair<std::string, std::optional<std::string>> ReadColorsFile(FileSource& files) {
    for (const char* name : kColorsFiles) {
        if (std::optional<std::string> text = files.Read(name)) return {name, std::move(text)};
    }
    return {};
}

// The colours a theme leaves to Ampify, once its own resources.xaml is known.
void DefineDerivedColors(Colours& colours) {
    std::vector<std::string> keys = XamlKeys(colours.theme.resourceDictionary);
    std::set<std::string> owned(keys.begin(), keys.end());
    for (auto define : {DefineAccentStates, DefineTextOnAccent, DefineAccentControls, DefineSelectedRows,
                        DefineAcrylicSurfaces, DefinePageHeaderFrost, DefineAppleSecondaryText}) {
        define(colours, owned);
    }
}

std::set<std::string> LoadColors(FileSource& files, const std::string& wanted, Colours& colours) {
    Theme& theme = colours.theme;
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
        if (!colours.Define(entry.key, *color)) {
            theme.problems.push_back(file + " line " + std::to_string(entry.line) + ": \"" + entry.key +
                                     "\" defined twice, ignored");
        }
    }

    // The app brushes that play the part of a Spicetify colour, unless the scheme gives them itself.
    for (const SpicetifyResource& mapping : SpicetifyResources()) {
        std::optional<Argb> color = spicetify.count(mapping.spicetify) ? theme.palette.Find(mapping.spicetify) : std::nullopt;
        if (color) colours.Define(mapping.resource, *color);
    }
    theme.dark = DarkOf(theme.palette, theme.scheme);
    return spicetify;
}

// theme.accent over the scheme: its accent and Spicetify's button and playback bar take the chosen colour. The colours a
// scheme gives along with its own accent (hover and pressed accents, text on the accent, links) would clash with it:
// they go, and Ampify derives them from the chosen accent (DefineDerivedColors). Keys of the theme's resources.xaml
// stay: Ampify does not rewrite a theme's own XAML.
void ChooseAccent(Colours& colours, Argb accent) {
    Theme& theme = colours.theme;
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
    colours.Set(kAccent, accent);
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

    // Without resource-keys.txt, the allowed keys are the colour keys Apple Music and WinUI hold (colour_keys.cpp).
    std::span<const std::string_view> known = ColourResourceKeys();
    auto allowed = [&](const std::string& key) {
        return options.allowedKeys ? options.allowedKeys->count(key) > 0
                                   : std::binary_search(known.begin(), known.end(), std::string_view(key));
    };
    bool allAllowed = true;
    for (const std::string& key : XamlKeys(*xaml)) {
        if (allowed(key)) continue;
        theme.problems.push_back("resources.xaml: key \"" + key + "\" is not in the allowed list, theme refused");
        allAllowed = false;
    }
    if (!allAllowed) return false;

    theme.resourceDictionary = std::move(*xaml);
    return true;
}

}  // namespace detail

}  // namespace ampify::theme
