#include "theme.hpp"

#include "theme_detail.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

#include "../json.hpp"
#include "../manifest.hpp"
#include "../text_util.hpp"
#include "builtins.hpp"
#include "colour_keys.hpp"
#include "css_values.hpp"
#include "fonts.hpp"
#include "glass.hpp"
#include "ini.hpp"
#include "release_facts.hpp"
#include "scheme_choice.hpp"
#include "spicetify.hpp"

namespace ampify::theme {

using namespace detail;

namespace {

constexpr const char* kPlaybarSetting = "bar-position";

constexpr const char* kPlaybarLayoutId = "(playbar)";

// LoadPlaybarLayout: the bar at the bottom for a theme that leaves it to Ampify. Apple paints the page column a shade
// lighter than the sidebar, and the bar runs under both: the column takes the window's colour, as floating panels do.
constexpr std::string_view kPlaybarLayout = R"css(
@import "ampify:bottom-bar";

@setting bar-position = Centred {
    @import "ampify:centre-bar";
}

.main-column {
    background: transparent;
    border-width: 0;
}
)css";

std::string AtLine(const std::string& source, int line) {
    return source + " line " + std::to_string(line);
}

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
        if (condition.key == kPlaybarSetting) theme.placesPlaybar = true;
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

bool NamesPlace(std::string_view place) { return place == "top" || place == "bottom" || place == "centred"; }

// bar-position, which every stylesheet can read. Themes made before 1.3.0 declare it themselves: their value stands
// while apple.playbar leaves the place to the theme, and so does the theme's place a snippet was given.
void SetPlaybarPlace(const LoadOptions& options, const std::optional<std::string>& theirs, Theme& theme) {
    if (!NamesPlace(ToLowerAscii(options.playbar)) && theme.settings.contains(kPlaybarSetting)) return;
    std::string place = PlaybarPlace(options.playbar, theirs);
    theme.settings[kPlaybarSetting] = place;
    theme.variables[std::string("setting-") + kPlaybarSetting] = place;
}

// The settings manifest.json declares, with their value now: variables setting-<key>, and the @setting conditions.
void LoadSettings(FileSource& files, const LoadOptions& options, Theme& theme) {
    std::optional<std::string> text = files.Read("manifest.json");
    std::optional<std::string> theirs;
    if (text) {
        std::vector<std::string> errors;
        for (const SettingSpec& spec : DeclaredSettings(*text, &errors)) {
            auto stored = options.settings.find(spec.key);
            std::string value = SettingValue(
                spec, stored == options.settings.end() ? std::nullopt : std::optional<std::string>(stored->second));
            theme.settings[spec.key] = value;
            theme.variables["setting-" + spec.key] = value;
        }
        for (const std::string& error : errors) theme.problems.push_back("manifest.json: " + error + ", settings ignored");
        theirs = ParseManifest(*text, "", kKitApiMajor, nullptr).partial.playbar;
    }
    SetPlaybarPlace(options, theirs, theme);
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

// A list row draws its selection over its background, from brushes of its own: `background` on a selected row
// (`.list-item:selected`) goes to them, or Apple's grey would cover it. Nothing for any other selector.
std::vector<std::string_view> SelectedRowBrushes(const Selector& selector, std::string_view property) {
    if (property != "Background" || selector.parts.empty() || selector.trigger != Trigger::Always ||
        selector.pseudo != PseudoElement::None) {
        return {};
    }
    const Compound& last = selector.parts.back();
    if (!last.selected || (last.type != "ListViewItemPresenter" && last.type != "GridViewItemPresenter")) return {};
    return {"SelectedBackground", "SelectedPointerOverBackground", "SelectedPressedBackground"};
}

void AddAssignment(const Rule& rule, const PropertySpec& spec, const Value& value, const Declaration& declaration,
                   int order, const RuleSource& source, Theme& theme) {
    for (const Selector& selector : rule.selectors) {
        if (std::optional<std::string> problem = MisplacedProperty(selector, spec, declaration)) {
            theme.problems.push_back(AtLine(source.file, declaration.line) + ": " + *problem + ", ignored");
            continue;
        }
        std::vector<std::string_view> properties = SelectedRowBrushes(selector, spec.xaml);
        if (properties.empty()) properties.push_back(spec.xaml);
        for (std::string_view property : properties) {
            Assignment assignment{selector, SpecificityOf(selector), std::string(property), value,
                                  declaration.important, order, declaration.line};
            assignment.source = source.file;
            assignment.layer = source.layer;
            assignment.media = rule.media;
            theme.assignments.push_back(std::move(assignment));
        }
    }
}

// `font-role: title` sets the family, the weight and the size of the role at once.
bool AddFontRole(const Rule& rule, const Declaration& declaration, const std::string& value, int order,
                 const RuleSource& source, Theme& theme) {
    std::optional<FontStyle> style = ResolveFontRole(std::vector<const Theme*>{&theme, source.base}, value);
    if (!style) {
        theme.problems.push_back(AtLine(source.file, declaration.line) + ": font role \"" + value +
                                 "\" is unknown, ignored");
        return false;
    }
    if (!source.applied) return true;
    AddAssignment(rule, *FindProperty("font-family"), {ValueKind::Text, style->family}, declaration, order, source, theme);
    AddAssignment(rule, *FindProperty("font-weight"), {ValueKind::Number, style->weight}, declaration, order, source, theme);
    AddAssignment(rule, *FindProperty("font-size"), {ValueKind::Length, style->size}, declaration, order, source, theme);
    return true;
}

void LoadRules(FileSource& files, const ValueContext& context, Theme& theme, const RuleSource& source) {
    if (std::optional<std::string> css = files.Read("user.css")) LoadRuleText(*css, context, theme, source);
}

// Apple's progress line grows its played part once a second, in steps: with every theme it glides between them
// instead, as Spicetify's smooth progress bar (`transition: width`, src/win/width_glide.hpp). The thumb and a mascot
// riding the line follow, being laid out after it. A theme's own transition on the played part keeps its value.
constexpr char kPlayedPart[] = ".scrubber Rectangle#HorizontalDecreaseRect";

Theme ReadTheme(const std::string& id, const std::string& folder, FileSource& files, const LoadOptions& options) {
    Theme theme;
    theme.id = id;
    theme.name = id;

    Colours colours{theme, options.parts.colors};
    std::set<std::string> spicetify = LoadColors(files, options.scheme, colours);

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
    if (options.accent) ChooseAccent(colours, *options.accent);
    DefineDerivedColors(colours);
    LoadSettings(files, options, theme);
    for (const auto& [name, value] : options.artwork) theme.variables[name] = value;
    ValueContext context{theme.palette, files, folder, options.parts.assets};
    // Spicetify's colours paint Apple Music's own elements first; the theme's user.css comes on top.
    if (std::string translated = SpicetifyStylesheet(spicetify); !translated.empty()) {
        RuleSource source;
        source.file = "color.ini (Spicetify translation)";
        source.layer = -1;
        source.applied = options.parts.colors;
        LoadRuleText(translated, context, theme, source);
    }
    RuleSource rules;
    rules.applied = options.parts.resources;
    LoadRules(files, context, theme, rules);
    AddDerivedRules(context, theme, options.parts);
    return theme;
}

Theme ReadAccentOnly(const LoadOptions& options) {
    Theme theme;
    theme.id = kAccentOnlyId;
    theme.name = "accent colour";
    theme.usable = true;
    if (!options.accent) return theme;
    Colours colours{theme, options.parts.colors};
    ChooseAccent(colours, *options.accent);
    DefineDerivedColors(colours);
    NoFiles files;
    ValueContext context{theme.palette, files, "", false};
    AddDerivedRules(context, theme, options.parts);
    return theme;
}

Theme ReadSnippet(const std::string& id, const std::string& folder, FileSource& files, const Theme* base,
                  const LoadOptions& options) {
    Theme snippet;
    snippet.id = id;
    snippet.name = id;
    // What a snippet sees of the theme; its font roles are read through `base` (RuleSource::base).
    if (base) {
        snippet.palette = base->palette;
        snippet.variables = base->variables;
        snippet.settings = base->settings;
        snippet.keyframes = base->keyframes;
    }
    for (const char* name : kColorsFiles) {
        if (!files.Exists(name)) continue;
        snippet.problems.push_back(std::string(name) + " ignored: a snippet uses the colors of the active theme ($name)");
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
    LoadRules(files, context, snippet, source);
    if (std::string css = RoomInFixedSlots(snippet); !css.empty()) {
        RuleSource derived;
        derived.file = kFixedSlotSource;
        derived.layer = -1;
        LoadRuleText(css, context, snippet, derived);
    }
    return snippet;
}

Theme Lightened(Theme theme, GlassLevel level) {
    ApplyGlassLevel(theme, level);
    return theme;
}

}  // namespace

namespace detail {

void LoadRuleText(const std::string& css, const ValueContext& context, Theme& theme, const RuleSource& source) {
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
                if (AddFontRole(rule, declaration, *raw, order, from, theme)) order++;
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

            if (from.applied) AddAssignment(rule, *spec, *conversion.value, declaration, order, from, theme);
            order++;
        }
    }
}

std::string SmoothProgressLine(const Theme& theme) {
    if (Sets(theme, kPlayedPart, "Transition")) return {};
    return std::string(kPlayedPart) + " { transition: width 1s linear; }\n";
}

// Apple's suggestions open right under the search box, but their frame starts with its 1-pixel top edge and a padding
// of 2: a band of 3 between the field and the first suggestion, which read as a gap under every theme. The list meets
// the field; the frame keeps its sides and its bottom. A theme that sets either keeps its own.
constexpr char kSuggestionsFrame[] = ".search-suggestions Border#SuggestionsContainer";

std::string SuggestionsAgainstTheBox(const Theme& theme) {
    std::string css;
    if (!Sets(theme, kSuggestionsFrame, "Padding")) css += " padding: 0 0 2 0;";
    if (!Sets(theme, kSuggestionsFrame, "BorderThickness")) css += " border-width: 0 1 1 1;";
    return css.empty() ? css : std::string(kSuggestionsFrame) + " {" + css + " }\n";
}

}  // namespace detail

Theme LoadTheme(const std::string& id, const std::string& folder, FileSource& files, const LoadOptions& options) {
    return Lightened(ReadTheme(id, folder, files, options), options.glass);
}

Theme LoadAccentOnly(const LoadOptions& options) {
    return Lightened(ReadAccentOnly(options), options.glass);
}

std::string PlaybarPlace(std::string_view chosen, const std::optional<std::string>& theirs) {
    std::string place = ToLowerAscii(chosen);
    if (!NamesPlace(place)) place = theirs ? ToLowerAscii(*theirs) : "top";
    if (place == "bottom") return "Bottom";
    if (place == "centred") return "Centred";
    return "Top";
}

std::optional<Theme> LoadPlaybarLayout(const Theme* base, const LoadOptions& options) {
    if (base && base->placesPlaybar) return std::nullopt;
    auto known = base ? base->settings.find(kPlaybarSetting) : std::map<std::string, std::string>::const_iterator{};
    std::string place = base && known != base->settings.end() ? known->second : PlaybarPlace(options.playbar, std::nullopt);
    if (!NamesPlace(ToLowerAscii(place)) || ToLowerAscii(place) == "top") return std::nullopt;

    struct LayoutFiles final : FileSource {
        std::optional<std::string> Read(std::string_view path) override {
            return path == "user.css" ? std::optional<std::string>(kPlaybarLayout) : std::nullopt;
        }
        bool Exists(std::string_view path) override { return path == "user.css"; }
    } files;
    LoadOptions own = options;
    own.settings.clear();
    own.playbar = ToLowerAscii(place);
    return LoadSnippet(kPlaybarLayoutId, "", files, base, own);
}

Theme LoadSnippet(const std::string& id, const std::string& folder, FileSource& files, const Theme* base,
                  const LoadOptions& options) {
    return Lightened(ReadSnippet(id, folder, files, base, options), options.glass);
}

std::optional<WindowMaterial> WindowMaterialOf(std::string_view value) {
    std::string name = ToLowerAscii(TrimSpace(value));
    if (name == "mica") return WindowMaterial::Mica;
    if (name == "mica-alt") return WindowMaterial::MicaAlt;
    if (name == "acrylic") return WindowMaterial::Acrylic;
    return std::nullopt;
}

std::optional<WindowMaterial> Theme::Material() const {
    auto value = variables.find("window-material");
    if (value == variables.end()) return std::nullopt;
    std::optional<WindowMaterial> material = WindowMaterialOf(value->second);
    return material ? std::optional<WindowMaterial>(MaterialAt(glass, *material)) : std::nullopt;
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

bool ReadsArtworkAccent(FileSource& files) {
    std::optional<std::string> text = files.Read("manifest.json");
    return text && ParseManifest(*text, "", kKitApiMajor, nullptr).partial.accent.value_or("") == "artwork";
}

std::string ReadDefaultScheme(FileSource& files) {
    std::optional<std::string> text = files.Read("manifest.json");
    if (!text) return {};
    return ParseManifest(*text, "", kKitApiMajor, nullptr).partial.defaultScheme.value_or("");
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
