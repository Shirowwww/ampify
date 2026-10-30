#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../core/journal.hpp"
#include "../core/json.hpp"
#include "../core/manifest.hpp"
#include "../core/settings.hpp"
#include "../core/theme/color.hpp"
#include "../core/theme/css.hpp"
#include "../core/theme/commands.hpp"
#include "../core/theme/engine.hpp"
#include "../core/theme/files.hpp"
#include "../core/theme/fonts.hpp"
#include "../core/theme/ini.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/theme/layout.hpp"
#include "../core/theme/library.hpp"
#include "../core/theme/properties.hpp"
#include "../core/theme/scheme_choice.hpp"
#include "../core/theme/selector.hpp"
#include "../core/theme/session.hpp"
#include "../core/theme/snippets.hpp"
#include "../core/theme/theme.hpp"
#include "../core/theme/theme_settings.hpp"
#include "../core/theme/worker.hpp"
#include "test.hpp"

namespace th = amk::theme;

namespace {

// Theme files held in memory: the tests do not touch the disk.
class MemoryFiles final : public th::FileSource {
public:
    std::map<std::string, std::string> files;

    std::optional<std::string> Read(std::string_view path) override {
        if (!th::IsSafeRelativePath(path)) return std::nullopt;
        auto it = files.find(std::string(path));
        if (it == files.end()) return std::nullopt;
        return it->second;
    }

    bool Exists(std::string_view path) override {
        return th::IsSafeRelativePath(path) && files.count(std::string(path)) > 0;
    }
};

// An interface made of plain data. An element has only the properties it was created with, as a real element has
// only its own. Removing an element makes every later call on it fail, as a closed page does.
class FakeTree final : public th::TreeBackend {
public:
    struct Element {
        th::NodeInfo info;  // `parent` holds the index of the parent element until Snapshot translates it
        bool alive = true;
        std::map<std::string, th::Value> properties;
    };

    std::vector<Element> elements;
    std::map<std::string, th::Value> resources;
    std::string dictionary;
    std::set<std::string> refusedProperties;
    int writes = 0;

    // Returns the element id, which is its index plus one. `parent` is an element index, or -1 for a root.
    th::NodeId Add(int parent, std::string type, std::string name, std::map<std::string, th::Value> properties = {}) {
        Element element;
        element.info.id = elements.size() + 1;
        element.info.type = std::move(type);
        element.info.name = std::move(name);
        element.info.parent = parent;
        element.info.depth = parent < 0 ? 0 : elements[parent].info.depth + 1;
        element.properties = std::move(properties);
        elements.push_back(std::move(element));
        return elements.back().info.id;
    }

    void Remove(th::NodeId id) {
        if (Element* element = Find(id)) element->alive = false;
    }

    std::vector<th::NodeInfo> Snapshot() override {
        std::vector<int> position(elements.size(), -1);
        std::vector<th::NodeInfo> nodes;
        for (size_t i = 0; i < elements.size(); i++) {
            if (!elements[i].alive) continue;

            th::NodeInfo info = elements[i].info;
            int parent = elements[i].info.parent;
            info.parent = parent >= 0 ? position[parent] : -1;
            position[i] = static_cast<int>(nodes.size());
            nodes.push_back(info);
        }
        return nodes;
    }

    std::optional<th::Value> Read(th::NodeId id, std::string_view property) override {
        Element* element = Find(id);
        if (!element) return std::nullopt;
        auto it = element->properties.find(std::string(property));
        if (it == element->properties.end()) return std::nullopt;
        return it->second;
    }

    bool Write(th::NodeId id, std::string_view property, const th::Value& value) override {
        Element* element = Find(id);
        if (!element || refusedProperties.count(std::string(property))) return false;
        auto it = element->properties.find(std::string(property));
        if (it == element->properties.end()) return false;
        it->second = value;
        writes++;
        return true;
    }

    std::vector<std::string> ResourceKeys() override {
        std::vector<std::string> keys;
        for (const auto& [key, value] : resources) keys.push_back(key);
        return keys;
    }

    std::optional<th::Value> ReadResource(std::string_view key) override {
        auto it = resources.find(std::string(key));
        if (it == resources.end()) return std::nullopt;
        return it->second;
    }

    bool WriteResource(std::string_view key, const th::Value& value) override {
        auto it = resources.find(std::string(key));
        if (it == resources.end()) return false;
        it->second = value;
        writes++;
        return true;
    }

    bool SetDictionary(std::string_view xaml) override {
        if (xaml.find("REFUSE") != std::string_view::npos) return false;
        dictionary = std::string(xaml);
        return true;
    }

private:
    Element* Find(th::NodeId id) {
        if (id == 0 || id > elements.size() || !elements[id - 1].alive) return nullptr;
        return &elements[id - 1];
    }
};

th::Value Brush(std::string text) {
    return {th::ValueKind::Color, std::move(text)};
}

th::Selector Sel(const char* text) {
    std::string problem;
    std::optional<th::Selector> selector = th::ParseSelector(text, &problem);
    CHECK(selector.has_value());
    if (!selector) {
        amk::test::ReportFailure(__FILE__, __LINE__, "selector « " + std::string(text) + " » : " + problem);
        return {};
    }
    return *selector;
}

// The text of a converted value, or PROBLEM followed by the reason.
std::string Converted(const char* css, std::string_view raw, const th::Palette& palette, MemoryFiles& files) {
    const th::PropertySpec* spec = th::FindProperty(css);
    if (!spec) return "UNKNOWN PROPERTY";
    th::ValueContext context{palette, files, "C:/themes/test"};
    th::Conversion conversion = th::ConvertValue(*spec, raw, context);
    return conversion.value ? conversion.value->text : "PROBLEM: " + conversion.problem;
}

th::Theme Load(MemoryFiles& files, th::LoadOptions options = {}) {
    return th::LoadTheme("test", "C:/themes/test", files, options);
}

// The first problem that mentions `fragment`, or an empty string.
std::string ProblemContaining(const th::Theme& theme, std::string_view fragment) {
    for (const std::string& problem : theme.problems) {
        if (problem.find(fragment) != std::string::npos) return problem;
    }
    return {};
}

std::string TempFolder(const char* name) {
    std::filesystem::path folder = std::filesystem::temp_directory_path() / ("amk-theme-test-" + std::string(name));
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder.string();
}

}  // namespace

// --- INI -------------------------------------------------------------------------------------------------

TEST_CASE(Ini_ReadsSectionsKeysAndComments) {
    th::Ini ini;
    std::vector<std::string> problems =
        ini.Parse("; comment\n[Base]\nAccent = FA2D48\n# another comment\nBackground=#000\n\n[Dark]\nAccent=FFFFFF\n");

    CHECK(problems.empty());
    CHECK(ini.Sections().size() == 2);
    CHECK(ini.Get("base", "accent") == "FA2D48");  // sections and keys are case-insensitive
    CHECK(ini.Get("Dark", "ACCENT") == "FFFFFF");
    CHECK(ini.Get("Dark", "Background") == std::nullopt);
    CHECK(ini.Get("Light", "Accent") == std::nullopt);
}

TEST_CASE(Ini_ReportsBadLinesAndKeepsTheRest) {
    th::Ini ini;
    std::vector<std::string> problems = ini.Parse("[Base]\nok = 1\nno equals here\n = 3\n[Broken\nx = 2\n");

    CHECK(problems.size() == 3);
    CHECK(ini.Get("Base", "ok") == "1");
    CHECK(ini.Get("", "x") == "2");  // the key after a broken header is kept, in a section without a name
    CHECK(!problems.empty() && problems[0].find("ligne 3") != std::string::npos);
}

// --- Colours ---------------------------------------------------------------------------------------------

TEST_CASE(Color_ParsesTheThreeHexForms) {
    CHECK((th::ParseColor("#FFF") == th::Argb{0xFF, 0xFF, 0xFF, 0xFF}));
    CHECK((th::ParseColor("#F80") == th::Argb{0xFF, 0xFF, 0x88, 0x00}));
    CHECK((th::ParseColor("FA2D48") == th::Argb{0xFF, 0xFA, 0x2D, 0x48}));  // the # is optional, as in colors.ini
    CHECK((th::ParseColor("#80FA2D48") == th::Argb{0x80, 0xFA, 0x2D, 0x48}));

    CHECK(!th::ParseColor("#GG0000"));
    CHECK(!th::ParseColor("#12345"));
    CHECK(!th::ParseColor(""));
    CHECK(th::ToXamlColor(th::Argb{0x80, 0xFA, 0x2D, 0x48}) == "#80FA2D48");
}

TEST_CASE(Palette_NamesAreCaseInsensitiveAndUnique) {
    th::Palette palette;
    CHECK((palette.Define("Accent", th::Argb{0xFF, 1, 2, 3})));
    CHECK((!palette.Define("accent", th::Argb{0xFF, 4, 5, 6})));
    CHECK((palette.Find("ACCENT") == th::Argb{0xFF, 1, 2, 3}));
    CHECK(!palette.Find("missing"));
}

// --- CSS subset ------------------------------------------------------------------------------------------

TEST_CASE(Css_ParsesSelectorsDeclarationsAndLines) {
    th::Stylesheet sheet = th::ParseStylesheet(
        "/* top */\nGrid#Bar > TextBlock, Button {\n  background: #FFF;\n  visibility: collapsed !important;\n}\n");

    CHECK(sheet.problems.empty());
    CHECK(sheet.rules.size() == 1);
    if (sheet.rules.size() != 1) return;

    const th::Rule& rule = sheet.rules[0];
    CHECK(rule.selectors.size() == 2);
    CHECK(rule.line == 2);
    CHECK(rule.selectors[0].parts.size() == 2);
    CHECK(rule.selectors[0].parts[0].type == "Grid");
    CHECK(rule.selectors[0].parts[0].name == "Bar");
    CHECK(rule.selectors[0].parts[1].type == "TextBlock");
    CHECK(rule.selectors[0].parts[1].combinator == th::Combinator::Child);

    CHECK(rule.declarations.size() == 2);
    CHECK(rule.declarations[0].property == "background");
    CHECK(rule.declarations[0].value == "#FFF");
    CHECK(rule.declarations[0].line == 3);
    CHECK(!rule.declarations[0].important);
    CHECK(rule.declarations[1].value == "collapsed");
    CHECK(rule.declarations[1].important);
}

TEST_CASE(Css_SkipsBadRulesAndKeepsTheOthers) {
    th::Stylesheet sheet = th::ParseStylesheet(
        "Grid > { color: red; }\n"
        "@import url(x.css);\n"
        "Grid#A { margin: 4 }\n"
        "Deep > > Grid { width: 1 }\n"
        "TextBlock { font-size 12 }\n");

    CHECK(sheet.problems.size() == 4);
    CHECK(sheet.rules.size() == 2);  // Grid#A, and TextBlock whose one declaration is refused
    if (sheet.rules.size() == 2) {
        CHECK(sheet.rules[0].selectors[0].parts[0].name == "A");
        CHECK(sheet.rules[1].declarations.empty());
    }
}

// --- Selectors and specificity ---------------------------------------------------------------------------

TEST_CASE(Selector_MatchesTypesNamesAndCombinators) {
    // Page
    //   Grid#Bar
    //     TextBlock          (index 2)
    //   Grid                 (index 3)
    //     Border
    //       TextBlock        (index 5)
    std::vector<th::NodeInfo> nodes(6);
    nodes[0] = {1, "Page", "", "", -1, 0};
    nodes[1] = {2, "Grid", "", "Bar", 0, 1};
    nodes[2] = {3, "TextBlock", "", "", 1, 2};
    nodes[3] = {4, "Grid", "", "", 0, 1};
    nodes[4] = {5, "Border", "", "", 3, 2};
    nodes[5] = {6, "TextBlock", "", "", 4, 3};

    CHECK(th::Matches(Sel("TextBlock"), nodes, 2));
    CHECK(th::Matches(Sel("TextBlock"), nodes, 5));
    CHECK(!th::Matches(Sel("TextBlock"), nodes, 1));

    CHECK(th::Matches(Sel("Grid TextBlock"), nodes, 2));
    CHECK(th::Matches(Sel("Grid TextBlock"), nodes, 5));  // a descendant, through Border
    CHECK(!th::Matches(Sel("Grid > TextBlock"), nodes, 5));  // but not a direct child
    CHECK(th::Matches(Sel("Grid > TextBlock"), nodes, 2));

    CHECK(th::Matches(Sel("#Bar TextBlock"), nodes, 2));
    CHECK(!th::Matches(Sel("#Bar TextBlock"), nodes, 5));
    CHECK(th::Matches(Sel("Page > Grid > Border > TextBlock"), nodes, 5));
    CHECK(th::Matches(Sel("* > Border"), nodes, 4));
    CHECK(th::Matches(Sel("Grid Border > TextBlock"), nodes, 5));  // the descendant search must try every ancestor
}

TEST_CASE(Selector_SpecificityOrdersNamesBeforeTypes) {
    CHECK(th::SpecificityOf(Sel("#Bar TextBlock")) > th::SpecificityOf(Sel("Grid > TextBlock > Border")));
    CHECK(th::SpecificityOf(Sel("Grid > TextBlock")) < th::SpecificityOf(Sel("Grid#A TextBlock")));
    CHECK(th::SpecificityOf(Sel("*")) == th::SpecificityOf(Sel("*")));
}

TEST_CASE(Selector_RejectsWhatItDoesNotKnow) {
    std::string problem;
    CHECK(!th::ParseSelector("Grid[Name=x]", &problem));
    CHECK(problem.find("non pris en charge") != std::string::npos);
    CHECK(!th::ParseSelector("Grid:hover", &problem));
    CHECK(!th::ParseSelector("TextBlock >", &problem));
    CHECK(!th::ParseSelector("# ", &problem));
}

// --- Values ----------------------------------------------------------------------------------------------

TEST_CASE(Properties_ConvertBrushesAndImages) {
    th::Palette palette;
    palette.Define("Accent", th::Argb{0xFF, 0xFA, 0x2D, 0x48});
    MemoryFiles files;
    files.files["images/bg.png"] = "PNG";

    CHECK(Converted("background", "#fa2d48", palette, files) == "#FFFA2D48");
    CHECK(Converted("background", "$accent", palette, files) == "#FFFA2D48");
    CHECK(Converted("background", "$Missing", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(Converted("background", "transparent", palette, files) == "#00FFFFFF");
    CHECK(Converted("background", "url(images/bg.png)", palette, files) == "C:/themes/test/images/bg.png");
    CHECK(Converted("background", "url(\"images/bg.png\")", palette, files) == "C:/themes/test/images/bg.png");
    CHECK(Converted("background", "url(images/missing.png)", palette, files).find("introuvable") != std::string::npos);
    CHECK(Converted("background", "url(../secret.png)", palette, files).find("non autorisé") != std::string::npos);
    CHECK(Converted("background", "nonsense", palette, files).rfind("PROBLEM", 0) == 0);
}

TEST_CASE(Properties_ConvertLengthsAndThickness) {
    th::Palette palette;
    MemoryFiles files;

    CHECK(Converted("font-size", "18px", palette, files) == "18");
    CHECK(Converted("font-size", "1.5", palette, files) == "1.5");
    CHECK(Converted("width", "auto", palette, files) == "NaN");
    CHECK(Converted("width", "wide", palette, files).rfind("PROBLEM", 0) == 0);

    // CSS order is top right bottom left; XAML order is left,top,right,bottom.
    CHECK(Converted("margin", "8", palette, files) == "8,8,8,8");
    CHECK(Converted("margin", "4 8", palette, files) == "8,4,8,4");
    CHECK(Converted("margin", "1 2 3", palette, files) == "2,1,2,3");
    CHECK(Converted("margin", "1 2 3 4", palette, files) == "4,1,2,3");
    CHECK(Converted("margin", "1 2 3 4 5", palette, files).rfind("PROBLEM", 0) == 0);
}

TEST_CASE(Properties_ConvertVisibilityNumbersAndText) {
    th::Palette palette;
    MemoryFiles files;

    CHECK(Converted("visibility", "collapsed", palette, files) == "Collapsed");
    CHECK(Converted("visibility", "hidden", palette, files) == "Collapsed");
    CHECK(Converted("visibility", "Visible", palette, files) == "Visible");
    CHECK(Converted("visibility", "maybe", palette, files).rfind("PROBLEM", 0) == 0);

    CHECK(Converted("opacity", "0.5", palette, files) == "0.5");
    CHECK(Converted("opacity", "1.5", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(Converted("font-weight", "bold", palette, files) == "700");
    CHECK(Converted("font-weight", "normal", palette, files) == "400");
    CHECK(Converted("font-weight", "600", palette, files) == "600");

    CHECK(Converted("font-family", "\"Segoe UI Variable\"", palette, files) == "Segoe UI Variable");
    CHECK(Converted("font-family", "Arial", palette, files) == "Arial");
}

TEST_CASE(Properties_TemplatesMustBeXaml) {
    th::Palette palette;
    MemoryFiles files;
    files.files["templates/button.xaml"] = "<ControlTemplate />";
    files.files["templates/plain.txt"] = "not xaml";

    th::ValueContext context{palette, files, "C:/themes/test"};
    const th::PropertySpec* spec = th::FindProperty("template");
    CHECK(spec != nullptr);
    if (!spec) return;

    th::Conversion good = th::ConvertValue(*spec, "url(templates/button.xaml)", context);
    CHECK(good.value.has_value());
    if (good.value) CHECK(good.value->kind == th::ValueKind::Template);

    CHECK(!th::ConvertValue(*spec, "url(templates/plain.txt)", context).value);
    CHECK(!th::ConvertValue(*spec, "url(templates/absent.xaml)", context).value);
}

TEST_CASE(Properties_NumbersPrintWithoutTrailingZeros) {
    CHECK(th::FormatNumber(18.0) == "18");
    CHECK(th::FormatNumber(0.5) == "0.5");
    CHECK(th::FormatNumber(-4.0) == "-4");
}

TEST_CASE(Properties_FindsByCssNameWhateverTheCase) {
    CHECK(th::FindProperty("Background") != nullptr);
    CHECK(th::FindProperty("Background")->xaml == "Background");
    CHECK(th::FindProperty("color")->xaml == "Foreground");
    CHECK(th::FindProperty("unknown-property") == nullptr);
}

TEST_CASE(Xaml_ExtractsResourceKeys) {
    std::vector<std::string> keys = th::XamlKeys("<SolidColorBrush x:Key=\"A\"/><X x:Key='B'/><Y Key=\"no\"/>");
    CHECK(keys.size() == 2);
    if (keys.size() == 2) {
        CHECK(keys[0] == "A");
        CHECK(keys[1] == "B");
    }
}

// --- Theme loading ---------------------------------------------------------------------------------------

TEST_CASE(Theme_UsesTheRequestedSchemeAndFallsBack) {
    MemoryFiles files;
    files.files["colors.ini"] = "[Base]\nAccent = FA2D48\n[Dark]\nAccent = 0A84FF\n";
    files.files["user.css"] = "Page { background: $accent; }\n";

    th::Theme dark = Load(files, {"dark", nullptr, {}});
    CHECK(dark.usable);
    CHECK(dark.scheme == "Dark");
    CHECK(dark.schemes.size() == 2);
    CHECK(dark.problems.empty());
    CHECK(dark.assignments.size() == 1);
    if (dark.assignments.size() == 1) CHECK(dark.assignments[0].value.text == "#FF0A84FF");
    if (!dark.resources.empty()) CHECK(dark.resources[0].value.text == "#FF0A84FF");

    th::Theme missing = Load(files, {"Light", nullptr, {}});
    CHECK(missing.scheme == "Base");  // falls back to the first scheme
    CHECK(!ProblemContaining(missing, "schéma « Light » absent").empty());
    if (missing.assignments.size() == 1) CHECK(missing.assignments[0].value.text == "#FFFA2D48");
}

TEST_CASE(Theme_RefusesResourcesOutsideTheAllowedList) {
    MemoryFiles files;
    files.files["resources.xaml"] =
        "<ResourceDictionary xmlns=\"x\"><SolidColorBrush x:Key=\"AccentBrush\"/>"
        "<SolidColorBrush x:Key=\"Secret\"/></ResourceDictionary>";

    std::set<std::string> onlyAccent{"AccentBrush"};
    th::Theme refused = Load(files, {"", &onlyAccent, {}});
    CHECK(!refused.usable);
    CHECK(refused.resourceDictionary.empty());
    CHECK(!ProblemContaining(refused, "Secret").empty());

    std::set<std::string> both{"AccentBrush", "Secret"};
    th::Theme accepted = Load(files, {"", &both, {}});
    CHECK(accepted.usable);
    CHECK(!accepted.resourceDictionary.empty());

    th::Theme unchecked = Load(files, {"", nullptr, {}});
    CHECK(unchecked.usable);
    CHECK(!ProblemContaining(unchecked, "vérification non faite").empty());
}

TEST_CASE(Theme_NeedsAtLeastOneThemeFile) {
    MemoryFiles files;
    files.files["readme.txt"] = "not a theme";
    th::Theme theme = Load(files);
    CHECK(!theme.usable);
    CHECK(!ProblemContaining(theme, "pas un thème").empty());
}

TEST_CASE(Theme_ReportsBadRulesWithTheirLineAndKeepsTheGoodOnes) {
    MemoryFiles files;
    files.files["user.css"] = "TextBlock {\n  font-size: wide;\n  unknown-prop: 3;\n  background: #FFF;\n}\n";

    th::Theme theme = Load(files);
    CHECK(theme.usable);
    CHECK(theme.assignments.size() == 1);
    CHECK(!ProblemContaining(theme, "ligne 2").empty());
    CHECK(!ProblemContaining(theme, "ligne 3").empty());
    if (theme.assignments.size() == 1) CHECK(theme.assignments[0].line == 4);
}

// --- Apply, restore and live reload ----------------------------------------------------------------------

namespace {

// Builds the same small page for each engine test:
//   Page                       background #FF000000
//     Grid#Bar                 background #FF0000FF
//       TextBlock     (e2)     foreground, font size, visibility
//     TextBlock#Title (e3)     foreground, font size, visibility
//     Border#Hide     (e4)     visibility, background
FakeTree MakePage() {
    FakeTree tree;
    tree.Add(-1, "Page", "", {{"Background", Brush("#FF000000")}});
    tree.Add(0, "Grid", "Bar", {{"Background", Brush("#FF0000FF")}});
    tree.Add(1, "TextBlock", "", {{"Foreground", Brush("#FF111111")},
                                  {"FontSize", {th::ValueKind::Length, "12"}},
                                  {"Visibility", {th::ValueKind::Visibility, "Visible"}}});
    tree.Add(0, "TextBlock", "Title", {{"Foreground", Brush("#FF111111")},
                                       {"FontSize", {th::ValueKind::Length, "12"}},
                                       {"Visibility", {th::ValueKind::Visibility, "Visible"}}});
    tree.Add(0, "Border", "Hide", {{"Visibility", {th::ValueKind::Visibility, "Visible"}},
                                   {"Background", Brush("#FF000000")}});
    return tree;
}

constexpr const char* kPageRules =
    "Page { background: #FF101010; }\n"
    "Grid#Bar { background: #FF202020; }\n"
    "TextBlock { color: #FFEEEEEE; }\n"
    "Grid TextBlock { font-size: 20px; }\n"
    "TextBlock#Title { color: #FF00FF00; }\n"
    "TextBlock { visibility: collapsed !important; }\n"
    "#Hide { visibility: collapsed; }\n";

}  // namespace

TEST_CASE(Engine_AppliesTheCascadeAndRestoresTheOriginals) {
    MemoryFiles files;
    files.files["user.css"] = kPageRules;
    th::Theme theme = Load(files);

    FakeTree tree = MakePage();
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report first = engine.Apply(theme, tree);
    CHECK(first.applied == 8);
    CHECK(first.refused == 0);
    CHECK(engine.Active());
    CHECK(engine.OriginalCount() == 8);

    CHECK(tree.elements[0].properties["Background"].text == "#FF101010");  // the page rule
    CHECK(tree.elements[1].properties["Background"].text == "#FF202020");
    CHECK(tree.elements[2].properties["Foreground"].text == "#FFEEEEEE");  // TextBlock beats nothing here
    CHECK(tree.elements[2].properties["FontSize"].text == "20");           // Grid TextBlock, under Grid#Bar
    CHECK(tree.elements[3].properties["Foreground"].text == "#FF00FF00");  // #Title beats TextBlock on names
    CHECK(tree.elements[3].properties["FontSize"].text == "12");           // not under the grid
    CHECK(tree.elements[2].properties["Visibility"].text == "Collapsed");  // !important
    CHECK(tree.elements[4].properties["Visibility"].text == "Collapsed");  // #Hide
    CHECK(tree.elements[4].properties["Background"].text == "#FF000000");  // no rule for it

    // Applying again writes nothing: the values are already in place.
    int writesBefore = tree.writes;
    th::ThemeEngine::Report second = engine.Apply(theme, tree);
    CHECK(second.applied == 0);
    CHECK(second.unchanged == 8);
    CHECK(tree.writes == writesBefore);

    engine.Restore(tree);
    CHECK(!engine.Active());
    CHECK(engine.OriginalCount() == 0);
    CHECK(tree.elements[0].properties["Background"].text == "#FF000000");
    CHECK(tree.elements[1].properties["Background"].text == "#FF0000FF");
    CHECK(tree.elements[2].properties["Foreground"].text == "#FF111111");
    CHECK(tree.elements[2].properties["FontSize"].text == "12");
    CHECK(tree.elements[2].properties["Visibility"].text == "Visible");
    CHECK(tree.elements[3].properties["Foreground"].text == "#FF111111");
    CHECK(tree.elements[4].properties["Visibility"].text == "Visible");

    CHECK(log.str().find("Appliqué : Page Background = #FF101010") != std::string::npos);
    CHECK(log.str().find("Thème retiré : 8 valeur(s) restaurée(s)") != std::string::npos);
}

TEST_CASE(Engine_RefusesWhatAnElementCannotTake) {
    MemoryFiles files;
    files.files["user.css"] = "Border { font-family: Arial; }\n";
    th::Theme theme = Load(files);

    FakeTree tree = MakePage();
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report report = engine.Apply(theme, tree);
    CHECK(report.applied == 0);
    CHECK(report.refused == 1);  // the Border has no FontFamily; the refusal is written once
    CHECK(log.str().find("Refusé : « Border#Hide » n'a pas de propriété FontFamily") != std::string::npos);

    engine.Apply(theme, tree);
    CHECK(log.str().find("Refusé") == log.str().rfind("Refusé"));
}

TEST_CASE(Engine_ReappliesAThemeLikeLiveReloadDoes) {
    MemoryFiles files;
    files.files["user.css"] = kPageRules;
    th::Theme first = Load(files);

    MemoryFiles edited;
    edited.files["user.css"] = "Page { background: #FF303030; }\n";
    th::Theme second = Load(edited);

    FakeTree tree = MakePage();
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    engine.Apply(first, tree);
    engine.Restore(tree);
    engine.Apply(second, tree);

    CHECK(tree.elements[0].properties["Background"].text == "#FF303030");
    CHECK(tree.elements[1].properties["Background"].text == "#FF0000FF");  // the first theme's change is gone
    engine.Restore(tree);
    CHECK(tree.elements[0].properties["Background"].text == "#FF000000");
}

TEST_CASE(Engine_ForgetsOriginalsOfElementsThatWentAway) {
    MemoryFiles files;
    files.files["user.css"] = kPageRules;
    th::Theme theme = Load(files);

    FakeTree tree = MakePage();
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    engine.Apply(theme, tree);
    CHECK(engine.OriginalCount() == 8);

    tree.Remove(3);  // the TextBlock under Grid#Bar is closed: its three originals cannot be restored any more
    engine.Apply(theme, tree);
    CHECK(engine.OriginalCount() == 5);
}

TEST_CASE(Engine_AppliesColoursResourcesAndDictionaries) {
    MemoryFiles files;
    files.files["colors.ini"] = "[Base]\nAccentFillColorDefaultBrush = 0A84FF\nGhost = 111111\n";
    files.files["resources.xaml"] = "<ResourceDictionary><SolidColorBrush x:Key=\"AccentFillColorDefaultBrush\"/>"
                                    "</ResourceDictionary>";
    std::set<std::string> allowed{"AccentFillColorDefaultBrush"};
    th::Theme theme = Load(files, {"", &allowed, {}});
    CHECK(theme.usable);

    FakeTree tree;
    tree.resources["AccentFillColorDefaultBrush"] = Brush("#FF000000");
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report report = engine.Apply(theme, tree);
    CHECK(tree.resources["AccentFillColorDefaultBrush"].text == "#FF0A84FF");
    CHECK(report.refused == 1);  // Ghost is not an application resource
    CHECK(log.str().find("ressource absente de l'application : Ghost") != std::string::npos);
    CHECK(tree.dictionary == theme.resourceDictionary);

    engine.Restore(tree);
    CHECK(tree.resources["AccentFillColorDefaultBrush"].text == "#FF000000");
    CHECK(tree.dictionary.empty());
}

TEST_CASE(Engine_ReportsADictionaryTheInterfaceRefuses) {
    MemoryFiles files;
    files.files["resources.xaml"] = "<ResourceDictionary>REFUSE</ResourceDictionary>";
    th::Theme theme = Load(files);
    CHECK(theme.usable);

    FakeTree tree;
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report report = engine.Apply(theme, tree);
    CHECK(report.refused == 1);
    CHECK(tree.dictionary.empty());
}

// --- Library, watcher and settings -----------------------------------------------------------------------

TEST_CASE(Library_ListsOnlyFoldersWithThemeFiles) {
    std::string root = TempFolder("library");
    std::filesystem::path base(root);
    std::filesystem::create_directories(base / "minuit");
    std::filesystem::create_directories(base / "vide");
    std::filesystem::create_directories(base / "css");
    { std::ofstream(base / "minuit" / "colors.ini") << "[Base]\n"; }
    { std::ofstream(base / "css" / "user.css") << "Page {}\n"; }

    std::vector<std::string> folders = th::ListThemeFolders(base);
    CHECK(folders.size() == 2);
    if (folders.size() == 2) {
        CHECK(folders[0] == "css");
        CHECK(folders[1] == "minuit");
    }
    std::filesystem::remove_all(base);
}

TEST_CASE(Library_SignatureChangesWhenAFileChanges) {
    std::string root = TempFolder("signature");
    std::filesystem::path folder(root);
    { std::ofstream(folder / "colors.ini") << "[Base]\nA = 111111\n"; }
    std::string before = th::FolderSignature(folder);

    { std::ofstream(folder / "colors.ini") << "[Base]\nA = 1111111111\n"; }
    CHECK(th::FolderSignature(folder) != before);

    std::string current = th::FolderSignature(folder);
    th::ChangeWatcher watcher([&] { return th::FolderSignature(folder); });
    CHECK(!watcher.Poll());  // the first poll only records the baseline
    CHECK(!watcher.Poll());
    { std::ofstream(folder / "user.css") << "Page { background: #FFF; }\n"; }
    CHECK(watcher.Poll());
    CHECK(!watcher.Poll());
    CHECK(current != th::FolderSignature(folder));
    std::filesystem::remove_all(folder);
}

TEST_CASE(Settings_ThemeKeysRoundTrip) {
    amk::Settings settings;
    th::ThemeSettings value;
    value.active = "minuit";
    value.scheme = "Dark";
    value.liveReload = false;
    value.inspect = true;
    th::WriteThemeSettings(settings, value);

    th::ThemeSettings back = th::ReadThemeSettings(settings);
    CHECK(back.active == "minuit");
    CHECK(back.scheme == "Dark");
    CHECK(!back.liveReload);
    CHECK(back.inspect);

    th::WriteThemeSettings(settings, th::ThemeSettings{});
    CHECK(th::ReadThemeSettings(settings).active.empty());
    CHECK(th::ReadThemeSettings(settings).liveReload);  // default on
}

// --- Inspector -------------------------------------------------------------------------------------------

TEST_CASE(Inspector_SuggestsASelectorThatMatchesItsElement) {
    FakeTree tree = MakePage();
    std::vector<th::NodeInfo> nodes = tree.Snapshot();

    CHECK(th::SuggestSelector(nodes, 0) == "Page");
    CHECK(th::SuggestSelector(nodes, 1) == "Grid#Bar");
    CHECK(th::SuggestSelector(nodes, 2) == "Grid#Bar > TextBlock");
    for (int index = 0; index < static_cast<int>(nodes.size()); index++) {
        CHECK(th::Matches(Sel(th::SuggestSelector(nodes, index).c_str()), nodes, index));
    }
}

TEST_CASE(Inspector_DumpsResourcesAndElements) {
    FakeTree tree = MakePage();
    tree.resources["AccentFillColorDefaultBrush"] = Brush("#FF0A84FF");

    std::string dump = th::InspectTree(tree);
    CHECK(dump.find("Ressources de l'application (1) :") != std::string::npos);
    CHECK(dump.find("AccentFillColorDefaultBrush = #FF0A84FF") != std::string::npos);
    CHECK(dump.find("(5 éléments)") != std::string::npos);
    CHECK(dump.find("  Grid#Bar  sélecteur : Grid#Bar  Background=#FF0000FF") != std::string::npos);
    CHECK(dump.find("    TextBlock  sélecteur : Grid#Bar > TextBlock") != std::string::npos);

    th::InspectOptions small;
    small.maxLines = 2;
    CHECK(th::InspectTree(tree, small).find("arrêté après 2 lignes") != std::string::npos);
}

// --- Example themes shipped in the repository ------------------------------------------------------------

TEST_CASE(Examples_LoadWithoutProblems) {
    for (const char* name : {"minuit", "texte-agrandi", "bouton-arrondi", "masquer-chrome"}) {
        std::filesystem::path folder = std::filesystem::path(AMK_SOURCE_DIR) / "themes" / "examples" / name;
        th::FolderFiles files(folder);
        th::Theme theme = th::LoadTheme(name, folder.generic_string(), files);

        CHECK(theme.usable);
        for (const std::string& problem : theme.problems) {
            amk::test::ReportFailure(__FILE__, __LINE__, std::string(name) + " : " + problem);
        }
        CHECK(!theme.assignments.empty() || !theme.resources.empty());
    }
}

// --- Session: the decisions taken on each tick -----------------------------------------------------------

TEST_CASE(Session_RestoresOnANewGenerationAndStylesNewElements) {
    MemoryFiles files;
    files.files["user.css"] = kPageRules;
    auto first = std::make_shared<const th::Theme>(Load(files));

    MemoryFiles other;
    other.files["user.css"] = "Page { background: #FF303030; }\n";
    auto second = std::make_shared<const th::Theme>(Load(other));

    FakeTree tree = MakePage();
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);

    th::SessionRequest request;
    request.themeId = "first";
    request.theme = first;
    request.generation = 1;
    session.Tick(tree, request);
    CHECK(session.Active());
    CHECK(tree.elements[0].properties["Background"].text == "#FF101010");

    // A page opened after the first tick gets the rules on the next tick, without a new generation.
    tree.Add(0, "TextBlock", "", {{"Foreground", Brush("#FF111111")}});
    session.Tick(tree, request);
    CHECK(tree.elements[5].properties["Foreground"].text == "#FFEEEEEE");

    request.themeId = "second";
    request.theme = second;
    request.generation = 2;
    session.Tick(tree, request);
    CHECK(tree.elements[0].properties["Background"].text == "#FF303030");
    CHECK(tree.elements[1].properties["Background"].text == "#FF0000FF");  // the first theme's change is undone

    th::SessionRequest none;
    none.generation = 3;
    session.Tick(tree, none);
    CHECK(!session.Active());
    CHECK(tree.elements[0].properties["Background"].text == "#FF000000");
}

TEST_CASE(Session_ReturnsAnInspectorDumpOnlyWhenAsked) {
    FakeTree tree = MakePage();
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);

    th::SessionRequest request;
    CHECK(!session.Tick(tree, request).has_value());

    request.inspect = true;
    std::optional<std::string> dump = session.Tick(tree, request);
    CHECK(dump.has_value());
    if (dump) CHECK(dump->find("Arbre (5 éléments)") != std::string::npos);

    FakeTree empty;
    CHECK(!session.Tick(empty, request).has_value());  // no content yet: asked again on the next tick
}

TEST_CASE(Session_RefusesAThemeThatIsNotUsable) {
    MemoryFiles files;
    files.files["resources.xaml"] = "<ResourceDictionary><X x:Key=\"Secret\"/></ResourceDictionary>";
    std::set<std::string> none;
    auto refused = std::make_shared<const th::Theme>(Load(files, {"", &none, {}}));
    CHECK(!refused->usable);

    FakeTree tree = MakePage();
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);

    th::SessionRequest request;
    request.themeId = "broken";
    request.theme = refused;
    request.generation = 1;
    session.Tick(tree, request);
    CHECK(!session.Active());
    CHECK(log.str().find("« broken » refusé") != std::string::npos);
}

// --- theme.parts ------------------------------------------------------------------------------------------

TEST_CASE(Parts_ColoursResourcesAndAssetsCanBeSwitchedOff) {
    MemoryFiles files;
    files.files["colors.ini"] = "[Base]\nAccentFillColorDefaultBrush = 0A84FF\n";
    files.files["resources.xaml"] = "<ResourceDictionary><X x:Key=\"AccentFillColorDefaultBrush\"/></ResourceDictionary>";
    files.files["images/bg.png"] = "PNG";
    files.files["user.css"] = "Page { background: url(images/bg.png); }\nTextBlock { font-size: 15px; }\n";
    std::set<std::string> allowed{"AccentFillColorDefaultBrush"};

    th::LoadOptions all{"", &allowed, {}};
    th::Theme full = Load(files, all);
    CHECK(full.usable);
    CHECK(full.resources.size() == 1);
    CHECK(!full.resourceDictionary.empty());
    CHECK(full.assignments.size() == 2);

    th::LoadOptions none{"", &allowed, {false, false, false}};
    th::Theme off = Load(files, none);
    CHECK(off.usable);
    CHECK(off.resources.empty());                 // colours off: no brush is replaced
    CHECK(off.resourceDictionary.empty());        // resources off: no dictionary
    CHECK(off.assignments.empty());               // resources off: no rule, which includes the images
    CHECK(off.palette.Find("AccentFillColorDefaultBrush").has_value());  // the palette stays for $names

    th::LoadOptions noAssets{"", &allowed, {true, true, false}};
    th::Theme withoutImages = Load(files, noAssets);
    CHECK(withoutImages.assignments.size() == 1);  // the font rule stays, the image rule is refused
    CHECK(!ProblemContaining(withoutImages, "assets").empty());
}

TEST_CASE(Parts_ReadBackFromSettings) {
    amk::Settings settings;
    th::ThemeSettings value;
    value.parts = {false, true, false};
    th::WriteThemeSettings(settings, value);
    th::ThemeSettings back = th::ReadThemeSettings(settings);
    CHECK(!back.parts.colors);
    CHECK(back.parts.resources);
    CHECK(!back.parts.assets);
    CHECK(th::ReadThemeSettings(amk::Settings{}).parts.colors);  // defaults are on
}

// --- Commands: amk theme list, amk theme set, amk watch --------------------------------------------------

namespace {

// A kit in a temporary folder, with a usable theme `minuit` (two schemes) and a refused theme `casse`.
th::KitLayout MakeKit(const char* name) {
    th::KitLayout kit{std::filesystem::path(TempFolder(name))};
    std::filesystem::create_directories(kit.Themes() / "minuit");
    std::filesystem::create_directories(kit.Themes() / "casse");
    { std::ofstream(kit.Themes() / "minuit" / "colors.ini") << "[Base]\nAccent = 0A84FF\n[Dark]\nAccent = 1C1C1E\n"; }
    { std::ofstream(kit.Themes() / "minuit" / "user.css") << "Page { background: $accent; }\n"; }
    { std::ofstream(kit.Themes() / "casse" / "resources.xaml") << "<Oops />\n"; }
    return kit;
}

std::vector<std::string> Args(std::initializer_list<const char*> values) {
    return std::vector<std::string>(values.begin(), values.end());
}

}  // namespace

TEST_CASE(Commands_ListShowsUsableAndRefusedThemes) {
    th::KitLayout kit = MakeKit("list");
    std::ostringstream out;
    std::ostringstream err;
    CHECK(th::RunThemeCommand(Args({"theme", "list"}), kit, out, err) == 0);
    CHECK(out.str().find("minuit") != std::string::npos);
    CHECK(out.str().find("minuit : utilisable, schémas : Base Dark") != std::string::npos);
    CHECK(out.str().find("casse : refusé") != std::string::npos);
    std::filesystem::remove_all(kit.root);
}

TEST_CASE(Commands_SetWritesTheThemeAndTheScheme) {
    th::KitLayout kit = MakeKit("set");
    std::ostringstream out;
    std::ostringstream err;
    CHECK(th::RunThemeCommand(Args({"theme", "set", "minuit", "--scheme", "Dark"}), kit, out, err) == 0);

    amk::Settings settings;
    settings.Load(kit.Settings().string());
    CHECK(settings.GetString("theme.active", "") == "minuit");
    CHECK(settings.GetString("theme.scheme", "") == "Dark");
    CHECK(out.str().find("Thème « minuit » choisi, schéma Dark") != std::string::npos);
    std::filesystem::remove_all(kit.root);
}

TEST_CASE(Commands_SetKeepsTheOtherSettings) {
    th::KitLayout kit = MakeKit("keep");
    amk::Settings before;
    before.Set("extensions.atmos-leveler.enabled", "1");
    before.SetBool("theme.live_reload", false);
    before.SetBool("theme.parts.assets", false);
    before.Save(kit.Settings().string());

    std::ostringstream out;
    std::ostringstream err;
    CHECK(th::RunThemeCommand(Args({"theme", "set", "minuit"}), kit, out, err) == 0);

    amk::Settings after;
    after.Load(kit.Settings().string());
    CHECK(after.GetString("extensions.atmos-leveler.enabled", "") == "1");
    CHECK(!after.GetBool("theme.live_reload", true));
    CHECK(!after.GetBool("theme.parts.assets", true));
    CHECK(after.GetString("theme.scheme", "x").empty());  // no --scheme: the first scheme is used
    std::filesystem::remove_all(kit.root);
}

TEST_CASE(Commands_SetNoneAndTheErrors) {
    th::KitLayout kit = MakeKit("none");
    std::ostringstream out;
    std::ostringstream err;

    CHECK(th::RunThemeCommand(Args({"theme", "set", "minuit"}), kit, out, err) == 0);
    CHECK(th::RunThemeCommand(Args({"theme", "set", "none"}), kit, out, err) == 0);
    amk::Settings settings;
    settings.Load(kit.Settings().string());
    CHECK(th::ReadThemeSettings(settings).active.empty());

    // A missing theme, a refused theme, and a wrong command change nothing and report why.
    CHECK(th::RunThemeCommand(Args({"theme", "set", "absent"}), kit, out, err) == 2);
    CHECK(th::RunThemeCommand(Args({"theme", "set", "casse"}), kit, out, err) == 1);
    CHECK(err.str().find("refusé : réglages inchangés") != std::string::npos);
    CHECK(th::RunThemeCommand(Args({"theme", "frobnicate"}), kit, out, err) == 2);
    CHECK(th::RunThemeCommand(Args({"theme", "set", "minuit", "--bogus"}), kit, out, err) == 2);
    CHECK(th::RunThemeCommand(Args({"theme", "set", "minuit", "--scheme"}), kit, out, err) == 2);

    settings.Load(kit.Settings().string());
    CHECK(th::ReadThemeSettings(settings).active.empty());  // still none
    std::filesystem::remove_all(kit.root);
}

TEST_CASE(Commands_WatchRechecksWhenAFileChanges) {
    std::string root = TempFolder("watch");
    std::filesystem::path folder(root);
    { std::ofstream(folder / "colors.ini") << "[Base]\nAccent = 0A84FF\n"; }

    std::ostringstream out;
    std::ostringstream err;
    int pauses = 0;
    auto pause = [&] {
        pauses++;
        if (pauses == 1) { std::ofstream(folder / "user.css") << "Page { background: #FFF; }\n"; }
    };

    int status = th::RunWatch(Args({"watch", root.c_str(), "--polls", "3"}), out, err, pause);
    CHECK(status == 0);
    CHECK(pauses == 3);
    CHECK(out.str().find("Changement détecté dans " + folder.filename().string()) != std::string::npos);

    // One check at the start, one after the change: two reports, the second one with the new rule.
    std::string text = out.str();
    size_t first = text.find("--- vérifié");
    CHECK(first != std::string::npos);
    CHECK(first != std::string::npos && text.find("--- vérifié", first + 1) != std::string::npos);
    CHECK(text.find("Page → Background = #FFFFFFFF") != std::string::npos);
    std::filesystem::remove_all(folder);
}

TEST_CASE(Commands_WatchRejectsBadUse) {
    std::ostringstream out;
    std::ostringstream err;
    auto nothing = [] {};
    CHECK(th::RunWatch(Args({"watch"}), out, err, nothing) == 2);
    CHECK(th::RunWatch(Args({"watch", "/no/such/folder/for/amk"}), out, err, nothing) == 2);
    CHECK(th::RunWatch(Args({"watch", "x", "--polls", "many"}), out, err, nothing) == 2);
}

// --- Spicetify parity: flow-direction, order, variables, font roles, snippets, schemes, layout, inspector JSON ----

namespace {

// A NavigationView with items that can be reordered, as the real sidebar can (NavigationView.MenuItems.Move).
// Snapshot lists the items in their current order; Order reads and moves the position of an item.
class ListTree final : public th::TreeBackend {
public:
    struct Item {
        th::NodeId id = 0;
        std::string name;
        th::Value visibility{th::ValueKind::Visibility, "Visible"};
    };
    std::vector<Item> items;  // current order

    explicit ListTree(std::vector<std::string> names) {
        for (const std::string& name : names) items.push_back({items.size() + 2, name, {}});
        for (Item& item : items) item.visibility = {th::ValueKind::Visibility, "Visible"};
    }

    std::vector<std::string> Names() const {
        std::vector<std::string> names;
        for (const Item& item : items) names.push_back(item.name);
        return names;
    }

    std::vector<th::NodeInfo> Snapshot() override {
        std::vector<th::NodeInfo> nodes;
        nodes.push_back({1, "NavigationView", "", "Sidebar", -1, 0});
        for (const Item& item : items) nodes.push_back({item.id, "NavigationViewItem", "", item.name, 0, 1});
        return nodes;
    }

    std::optional<th::Value> Read(th::NodeId id, std::string_view property) override {
        int index = IndexOf(id);
        if (index < 0) return std::nullopt;
        if (property == th::kOrderProperty) return th::Value{th::ValueKind::Number, std::to_string(index)};
        if (property == "Visibility") return items[index].visibility;
        return std::nullopt;
    }

    bool Write(th::NodeId id, std::string_view property, const th::Value& value) override {
        int index = IndexOf(id);
        if (index < 0) return false;
        if (property == "Visibility") {
            items[index].visibility = value;
            return true;
        }
        if (property != th::kOrderProperty) return false;
        int target = std::stoi(value.text);
        if (target < 0 || target >= static_cast<int>(items.size())) return false;
        Item moved = items[index];
        items.erase(items.begin() + index);
        items.insert(items.begin() + target, moved);
        return true;
    }

    std::vector<std::string> ResourceKeys() override { return {}; }
    std::optional<th::Value> ReadResource(std::string_view) override { return std::nullopt; }
    bool WriteResource(std::string_view, const th::Value&) override { return false; }
    bool SetDictionary(std::string_view) override { return true; }

private:
    int IndexOf(th::NodeId id) const {
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }
};

std::shared_ptr<const th::Theme> Shared(th::Theme theme) {
    return std::make_shared<const th::Theme>(std::move(theme));
}

th::Theme LoadSnippetFrom(MemoryFiles& files, const char* id, const th::Theme* base, th::LoadOptions options = {}) {
    return th::LoadSnippet(id, std::string("C:/snippets/") + id, files, base, options);
}

void WriteFile(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

}  // namespace

TEST_CASE(Properties_FlowDirectionAndOrder) {
    th::Palette palette;
    MemoryFiles files;
    CHECK(Converted("flow-direction", "rtl", palette, files) == "RightToLeft");
    CHECK(Converted("flow-direction", "Right-To-Left", palette, files) == "RightToLeft");
    CHECK(Converted("direction", "ltr", palette, files) == "LeftToRight");
    CHECK(Converted("flow-direction", "up", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(th::FindProperty("flow-direction")->xaml == "FlowDirection");

    CHECK(Converted("order", "2", palette, files) == "2");
    CHECK(Converted("order", "007", palette, files) == "7");
    CHECK(Converted("order", "-1", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(Converted("order", "1.5", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(th::FindProperty("order")->xaml == th::kOrderProperty);
}

TEST_CASE(Engine_AppliesFlowDirectionAndRestoresIt) {
    MemoryFiles files;
    files.files["user.css"] = "Page { flow-direction: rtl; }\n";
    th::Theme theme = Load(files);
    CHECK(theme.problems.empty());

    FakeTree tree;
    tree.Add(-1, "Page", "", {{"FlowDirection", {th::ValueKind::Keyword, "LeftToRight"}}});
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);
    engine.Apply(theme, tree);
    CHECK(tree.elements[0].properties["FlowDirection"].text == "RightToLeft");
    engine.Restore(tree);
    CHECK(tree.elements[0].properties["FlowDirection"].text == "LeftToRight");
}

TEST_CASE(Engine_MovesItemsInOrderAndUndoesTheMoves) {
    ListTree tree({"A", "B", "C", "D"});
    MemoryFiles files;
    // D first, then B: listed items come first in the listed order, the others keep theirs after them.
    files.files["user.css"] = "#D { order: 0; }\n#B { order: 1; }\n";
    th::Theme theme = Load(files);

    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);
    th::ThemeEngine::Report first = engine.Apply(theme, tree);
    CHECK((tree.Names() == std::vector<std::string>{"D", "B", "A", "C"}));
    CHECK(first.applied == 2);

    th::ThemeEngine::Report second = engine.Apply(theme, tree);  // the next tick writes nothing
    CHECK(second.applied == 0);
    CHECK((tree.Names() == std::vector<std::string>{"D", "B", "A", "C"}));

    engine.Restore(tree);
    CHECK((tree.Names() == std::vector<std::string>{"A", "B", "C", "D"}));
}

TEST_CASE(Variables_RootBlockAndVar) {
    MemoryFiles files;
    files.files["user.css"] =
        ":root {\n"
        "    --accent: #FF112233;\n"
        "    --pad: 4px;\n"
        "    --both: var(--pad) 8px;\n"
        "    color: red;\n"
        "}\n"
        "Page { background: var(--accent); padding: var(--both); }\n"
        "Grid { background: var(--absent, #FF445566); }\n"
        "TextBlock { color: var(--absent); --local: 1; }\n"
        "Border { margin: VAR(--later); }\n"
        ":root { --later: 2; }\n";
    th::Theme theme = Load(files);

    std::map<std::string, std::string> values;
    for (const th::Assignment& assignment : theme.assignments) values[assignment.selector.text + " " + assignment.property] = assignment.value.text;
    CHECK(values["Page Background"] == "#FF112233");
    CHECK(values["Page Padding"] == "8,4,8,4");
    CHECK(values["Grid Background"] == "#FF445566");
    CHECK(values["Border Margin"] == "2,2,2,2");  // a :root further down still counts, as in CSS
    CHECK(!values.count("TextBlock Foreground"));
    CHECK(theme.variables["pad"] == "4px");

    CHECK(!ProblemContaining(theme, "user.css ligne 5 : « color » dans :root").empty());
    CHECK(!ProblemContaining(theme, "variable « --absent » non définie").empty());
    CHECK(!ProblemContaining(theme, "variable « --local » hors de :root").empty());

    std::string problem;
    CHECK(!th::SubstituteVariables("var(--x", {}, &problem).has_value());
    CHECK(problem.find("parenthèse") != std::string::npos);
    CHECK(!th::SubstituteVariables("var(x)", {}, &problem).has_value());
    CHECK(th::SubstituteVariables("var(--a, var(--b, 3))", {}).value_or("") == "3");
}

TEST_CASE(Fonts_RolesFromRootBlockAndDefaults) {
    MemoryFiles files;
    files.files["user.css"] =
        ":root {\n"
        "    --font-title: \"Segoe UI Variable Display\";\n"
        "    --font-title-weight: bold;\n"
        "    --font-titre-size: 30px;\n"
        "    --font-texte: Inter;\n"
        "    --font-body-weight: heavy;\n"
        "}\n"
        "TextBlock#Title { font-role: title; }\n"
        "TextBlock { font-family: var(--font-texte); }\n"
        "Grid { font-role: nothing; }\n";
    th::Theme theme = Load(files);

    std::optional<th::FontStyle> title = th::ResolveFontRole(&theme, "titre");
    CHECK(title.has_value());
    if (title) {
        CHECK(title->role == "title");
        CHECK(title->family == "Segoe UI Variable Display");
        CHECK(title->weight == "700");
        CHECK(title->size == "30");
        CHECK(title->fromTheme);
    }
    std::optional<th::FontStyle> body = th::ResolveFontRole(&theme, "body");
    CHECK(body && body->family == "Inter" && body->weight == "400" && body->size == "14");
    std::optional<th::FontStyle> caption = th::ResolveFontRole(nullptr, "caption");
    CHECK(caption && !caption->fromTheme && caption->size == "12");
    CHECK(!th::ResolveFontRole(&theme, "unknown").has_value());

    int titleParts = 0;
    for (const th::Assignment& assignment : theme.assignments) {
        if (assignment.selector.text == "TextBlock#Title") titleParts++;
        if (assignment.selector.text == "TextBlock") CHECK(assignment.value.text == "Inter");
    }
    CHECK(titleParts == 3);
    CHECK(!ProblemContaining(theme, "--font-body-weight").empty());
    CHECK(!ProblemContaining(theme, "rôle de police « nothing » inconnu").empty());

    std::optional<amk::Json> json = amk::Json::Parse(th::FontRolesJson(&theme));
    CHECK(json.has_value());
    if (json) {
        const amk::Json* role = json->Find("title");
        CHECK(role && role->Find("family") && *role->Find("family")->AsString() == "Segoe UI Variable Display");
        CHECK(role && role->Find("weight") && *role->Find("weight")->AsNumber() == 700);
        CHECK(role && role->Find("fromTheme") && *role->Find("fromTheme")->AsBool());
        CHECK(json->Find("display") != nullptr);  // a default role is listed too
    }
}

TEST_CASE(Snippets_LoadWithTheColoursOfTheTheme) {
    MemoryFiles themeFiles;
    themeFiles.files["colors.ini"] = "[Base]\nAccent = FF0000\n";
    themeFiles.files["user.css"] = ":root { --font-title: Bahnschrift; }\n";
    th::Theme theme = Load(themeFiles);

    MemoryFiles files;
    files.files["colors.ini"] = "[Base]\nAccent = 00FF00\n";
    files.files["resources.xaml"] = "<ResourceDictionary xmlns=\"x\">\n  <!-- rien -->\n</ResourceDictionary>";
    files.files["user.css"] = "Page { background: $Accent; }\nTextBlock { font-role: title; }\n";
    th::Theme snippet = LoadSnippetFrom(files, "s1", &theme);
    CHECK(snippet.usable);
    CHECK(snippet.resourceDictionary.empty());  // an empty dictionary is not merged
    CHECK(!ProblemContaining(snippet, "colors.ini ignoré").empty());
    CHECK(!snippet.assignments.empty());
    if (!snippet.assignments.empty()) {
        CHECK(snippet.assignments[0].value.text == "#FFFF0000");  // the theme's colour
        CHECK(snippet.assignments[0].source == "snippets/s1/user.css");
        CHECK(snippet.assignments[1].value.text == "Bahnschrift");  // the theme's font role
    }

    // Without a theme, $names have nothing to read.
    th::Theme alone = LoadSnippetFrom(files, "s1", nullptr);
    CHECK(!ProblemContaining(alone, "absente de [colors]").empty());

    MemoryFiles refused;
    refused.files["resources.xaml"] = "<ResourceDictionary><X x:Key=\"Secret\"/></ResourceDictionary>";
    std::set<std::string> allowed{"Accent"};
    th::LoadOptions options;
    options.allowedKeys = &allowed;
    CHECK(!LoadSnippetFrom(refused, "s2", &theme, options).usable);

    MemoryFiles empty;
    empty.files["manifest.json"] = "{}";
    CHECK(!LoadSnippetFrom(empty, "s3", &theme).usable);
}

TEST_CASE(Snippets_EnabledInIdOrderAndDictionariesCombined) {
    amk::Settings settings;
    settings.Set("snippets.zeta.enabled", "1");
    settings.Set("snippets.alpha.enabled", "true");
    settings.Set("snippets.beta.enabled", "0");
    settings.Set("snippets.ghost.enabled", "1");  // not installed
    CHECK((th::EnabledSnippets(settings, {"zeta", "beta", "alpha"}) == std::vector<std::string>{"alpha", "zeta"}));

    std::string theme = "<?xml version=\"1.0\"?><ResourceDictionary><A x:Key=\"K\"/></ResourceDictionary>";
    std::string snippet = "<ResourceDictionary><B x:Key=\"K\"/></ResourceDictionary>";
    CHECK(th::CombineDictionaries({theme}) == theme);
    CHECK(th::CombineDictionaries({"", "<ResourceDictionary/>"}).empty());
    std::string combined = th::CombineDictionaries({theme, "<ResourceDictionary/>", snippet});
    CHECK(combined.find("<ResourceDictionary.MergedDictionaries>") != std::string::npos);
    CHECK(combined.find("<?xml") == std::string::npos);
    CHECK(combined.find("<A x:Key") < combined.find("<B x:Key"));  // the snippet comes last, so it wins

    std::string folder = TempFolder("snippet-list");
    WriteFile(std::filesystem::path(folder) / "b" / "user.css", "Page { opacity: 1; }");
    WriteFile(std::filesystem::path(folder) / "a" / "resources.xaml", "<ResourceDictionary/>");
    WriteFile(std::filesystem::path(folder) / "c" / "notes.txt", "pas un snippet");
    CHECK((th::ListSnippetFolders(folder) == std::vector<std::string>{"a", "b"}));
}

TEST_CASE(Snippets_BeatTheThemeInIdOrder) {
    MemoryFiles themeFiles;
    themeFiles.files["user.css"] = "Grid#Bar { background: #FF111111; }\nPage { background: #FF101010; }\n";
    th::Theme theme = Load(themeFiles);

    MemoryFiles first;
    first.files["user.css"] = "Grid { background: #FF222222; }\nPage { background: #FF202020; }\n";
    MemoryFiles second;
    second.files["user.css"] = "Page { background: #FF303030; }\n";
    std::vector<std::shared_ptr<const th::Theme>> snippets{Shared(LoadSnippetFrom(first, "a", &theme)),
                                                           Shared(LoadSnippetFrom(second, "b", &theme))};

    th::Theme composed = th::ComposeTheme(&theme, snippets, nullptr);
    CHECK(composed.name == "test + a, b");

    FakeTree tree = MakePage();
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);
    engine.Apply(composed, tree);
    CHECK(tree.elements[1].properties["Background"].text == "#FF222222");  // a snippet type beats a theme #name
    CHECK(tree.elements[0].properties["Background"].text == "#FF303030");  // b comes after a
    CHECK(log.str().find("(snippets/b/user.css ligne 1)") != std::string::npos);
}

TEST_CASE(Session_SnippetsComeAndGoWithoutTouchingTheTheme) {
    MemoryFiles themeFiles;
    themeFiles.files["user.css"] = "Page { background: #FF101010; }\n";
    auto theme = Shared(Load(themeFiles));

    MemoryFiles snippetFiles;
    snippetFiles.files["user.css"] = "Page { background: #FF999999; }\n#Hide { visibility: collapsed; }\nGrid { bad: 1; }\n";
    auto snippet = Shared(LoadSnippetFrom(snippetFiles, "calme", theme.get()));

    FakeTree tree = MakePage();
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);

    th::SessionRequest request;
    request.themeId = "test";
    request.theme = theme;
    request.snippets = {snippet};
    request.generation = 1;
    session.Tick(tree, request);
    CHECK(tree.elements[0].properties["Background"].text == "#FF999999");
    CHECK(tree.elements[4].properties["Visibility"].text == "Collapsed");
    CHECK(log.str().find("Snippet « calme » chargé") != std::string::npos);
    CHECK(log.str().find("Snippet « calme » : snippets/calme/user.css ligne 3 : propriété « bad » inconnue") !=
          std::string::npos);
    CHECK(session.Applied() && session.Applied()->name == "test + calme");

    // The snippet is switched off: the worker sends a new generation without it.
    request.snippets.clear();
    request.generation = 2;
    session.Tick(tree, request);
    CHECK(tree.elements[0].properties["Background"].text == "#FF101010");  // the theme again
    CHECK(tree.elements[4].properties["Visibility"].text == "Visible");     // the original again

    // A snippet without a theme still applies.
    th::SessionRequest alone;
    alone.snippets = {snippet};
    alone.generation = 3;
    session.Tick(tree, alone);
    CHECK(session.Active());
    CHECK(tree.elements[0].properties["Background"].text == "#FF999999");

    th::SessionRequest none;
    none.generation = 4;
    session.Tick(tree, none);
    CHECK(!session.Active());
    CHECK(tree.elements[0].properties["Background"].text == "#FF000000");
}

TEST_CASE(Scheme_ScheduleParsesAndPicksByTime) {
    th::Schedule schedule = th::ParseSchedule("19:00=Dark, 7:00=Light; bad, 25:00=X, 12:30=, 07:00=Clair");
    CHECK(schedule.entries.size() == 2);
    CHECK(schedule.problems.size() == 4);  // bad, 25:00, 12:30= and 07:00 given twice
    if (schedule.entries.size() == 2) {
        CHECK(schedule.entries[0].minute == 7 * 60);
        CHECK(schedule.entries[0].scheme == "Clair");  // the last 07:00 counts
    }
    CHECK(th::ScheduledScheme(schedule, 6 * 60 + 59) == "Dark");  // the evening entry still holds before 07:00
    CHECK(th::ScheduledScheme(schedule, 7 * 60) == "Clair");
    CHECK(th::ScheduledScheme(schedule, 18 * 60 + 59) == "Clair");
    CHECK(th::ScheduledScheme(schedule, 23 * 60 + 59) == "Dark");
    CHECK(th::ScheduledScheme(th::ParseSchedule(""), 600).empty());
}

TEST_CASE(Scheme_SystemModePicksByName) {
    CHECK(th::SystemScheme({"Base", "Dark"}, true) == "Dark");
    CHECK(th::IsDarkScheme("Dark") && th::IsDarkScheme("Nuit") && th::IsDarkScheme("Minuit sombre"));
    CHECK(!th::IsDarkScheme("Base") && !th::IsDarkScheme("Light") && !th::IsDarkScheme(""));
    CHECK(th::SystemScheme({"Base", "Dark"}, false) == "Base");
    CHECK(th::SystemScheme({"Sombre", "Clair"}, false) == "Clair");
    CHECK(th::SystemScheme({"Sombre", "Clair"}, true) == "Sombre");
    CHECK(th::SystemScheme({"MidnightDark", "DayLight"}, false) == "DayLight");
    CHECK(th::SystemScheme({"Nuit", "Jour"}, false) == "Jour");
    CHECK(th::SystemScheme({"Only"}, true).empty());
    CHECK(th::SystemScheme({}, false).empty());
}

TEST_CASE(Scheme_ChoosePrefersTheScheduleThenAutoThenTheName) {
    std::vector<std::string> schemes{"Light", "Dark"};
    th::SchemeChoice choice = th::ChooseScheme("auto", "", schemes, {true, 600});
    CHECK(choice.scheme == "Dark");
    CHECK(choice.reason == "mode sombre de Windows");

    choice = th::ChooseScheme("Dark", "07:00=Light,19:00=Dark", schemes, {true, 12 * 60});
    CHECK(choice.scheme == "Light");
    CHECK(choice.reason == "horaire 07:00");

    choice = th::ChooseScheme("", "07:00=auto,22:00=Dark", schemes, {false, 9 * 60});
    CHECK(choice.scheme == "Light");
    CHECK(choice.reason == "horaire 07:00, mode clair de Windows");

    choice = th::ChooseScheme("dark", "", schemes, {false, 0});
    CHECK(choice.scheme == "Dark");  // the name as colors.ini writes it

    choice = th::ChooseScheme("auto", "", {"Only"}, {true, 0});
    CHECK(choice.scheme.empty());
    CHECK(!choice.problems.empty());

    choice = th::ChooseScheme("", "08:00=Sepia", schemes, {false, 9 * 60});
    CHECK(choice.scheme == "Sepia");
    CHECK(!choice.problems.empty());
}

TEST_CASE(Settings_ScheduleIsKeptAndOnlyWrittenWhenSet) {
    amk::Settings settings;
    th::ThemeSettings value;
    value.schedule = "07:00=Light,19:00=Dark";
    th::WriteThemeSettings(settings, value);
    CHECK(th::ReadThemeSettings(settings).schedule == "07:00=Light,19:00=Dark");
    th::WriteThemeSettings(settings, th::ThemeSettings{});
    CHECK(!settings.Get("theme.schedule").has_value());

    std::string root = TempFolder("schedule-kept");
    std::filesystem::path kit(root);
    WriteFile(kit / "themes" / "minuit" / "colors.ini", "[Light]\nA = FFFFFF\n[Dark]\nA = 000000\n");
    WriteFile(kit / "settings.txt", "theme.schedule = 07:00=Light,19:00=Dark\n");
    std::ostringstream out, err;
    CHECK(th::RunThemeCommand({"theme", "set", "minuit"}, th::KitLayout{kit}, out, err) == 0);
    amk::Settings after;
    after.Load((kit / "settings.txt").string());
    CHECK(after.GetString("theme.schedule", "") == "07:00=Light,19:00=Dark");
}

TEST_CASE(Worker_ReloadsOnlyWhenSomethingChanges) {
    std::filesystem::path kit(TempFolder("worker"));
    WriteFile(kit / "themes" / "minuit" / "colors.ini", "[Light]\nA = FFFFFF\n\n[Dark]\nA = 000000\n");
    WriteFile(kit / "themes" / "minuit" / "user.css", "Page { background: $A; }\n");
    WriteFile(kit / "snippets" / "calme" / "user.css", "Page { opacity: 0.5; }\n");

    amk::Settings settings;
    settings.Set("theme.active", "minuit");
    settings.Set("theme.scheme", "auto");

    th::ThemeWorker worker(kit);
    worker.SetSystemDark(true);
    th::SessionRequest first = worker.Tick(settings, 600);
    CHECK(first.theme && first.theme->scheme == "Dark");
    CHECK(first.schemeReason == "mode sombre de Windows");
    CHECK(first.snippets.empty());
    CHECK(!first.layout);

    th::SessionRequest same = worker.Tick(settings, 601);
    CHECK(same.generation == first.generation);

    worker.SetSystemDark(false);
    th::SessionRequest light = worker.Tick(settings, 602);
    CHECK(light.generation == first.generation + 1);
    CHECK(light.theme && light.theme->scheme == "Light");

    settings.Set("theme.schedule", "07:00=Light,19:00=Dark");
    CHECK(worker.Tick(settings, 600).generation == light.generation);  // still Light: no reload
    th::SessionRequest evening = worker.Tick(settings, 20 * 60);
    CHECK(evening.generation == light.generation + 1);
    CHECK(evening.theme && evening.theme->scheme == "Dark");
    CHECK(evening.schemeReason == "horaire 19:00");

    settings.Set("snippets.calme.enabled", "1");
    th::SessionRequest withSnippet = worker.Tick(settings, 20 * 60);
    CHECK(withSnippet.generation == evening.generation + 1);
    CHECK(withSnippet.snippets.size() == 1);
    if (withSnippet.snippets.size() == 1) CHECK(withSnippet.snippets[0]->usable);

    WriteFile(kit / "sidebar.json", "{\"items\":[{\"name\":\"Radio\",\"hidden\":true}]}");
    th::SessionRequest withLayout = worker.Tick(settings, 20 * 60);
    CHECK(withLayout.generation == withSnippet.generation + 1);
    CHECK(withLayout.layout && withLayout.layout->assignments.size() == 1);

    settings.Set("theme.inspect", "1");
    th::SessionRequest inspect = worker.Tick(settings, 20 * 60);
    CHECK(inspect.inspect);
    CHECK(inspect.generation == withLayout.generation);  // asking for a dump reloads nothing
}

TEST_CASE(Layout_ParsesWritesAndGenerates) {
    std::vector<std::string> problems;
    std::optional<th::LayoutConfig> config = th::ParseLayout(
        "{\"version\":1,\"reorder\":true,\"items\":[{\"name\":\"Home\"},{\"name\":\"Radio\",\"hidden\":true},"
        "{\"name\":\"bad name\"},{\"name\":\"Home\"},{\"name\":\"Store\",\"hidden\":\"yes\"}]}",
        problems);
    CHECK(config.has_value());
    if (config) {
        CHECK(config->reorder);
        CHECK(config->items.size() == 3);
        CHECK(config->items[1].hidden);
        std::vector<std::string> again;
        std::optional<th::LayoutConfig> back = th::ParseLayout(th::WriteLayout(*config), again);
        CHECK(back && back->items.size() == 3 && back->items[1].name == "Radio" && back->items[1].hidden && again.empty());
    }
    CHECK(problems.size() == 3);
    CHECK(!th::ParseLayout("[1]", problems).has_value());
    CHECK(!th::ParseLayout("{\"version\":1}", problems).has_value());

    ListTree tree({"Home", "Browse", "Radio"});
    std::vector<std::string> names = th::NamesMatching(tree.Snapshot(), Sel("NavigationViewItem"));
    CHECK((names == std::vector<std::string>{"Home", "Browse", "Radio"}));

    th::LayoutConfig fresh = th::GenerateLayout(std::nullopt, names);
    CHECK(fresh.reorder && fresh.items.size() == 3 && !fresh.items[0].hidden);

    th::LayoutConfig existing;
    existing.items = {{"Radio", true}, {"Closed", false}};
    th::LayoutConfig merged = th::GenerateLayout(existing, names);
    CHECK(!merged.reorder);  // an existing file keeps its choice
    CHECK(merged.items.size() == 4);
    if (merged.items.size() == 4) {
        CHECK(merged.items[0].name == "Radio" && merged.items[0].hidden);
        CHECK(merged.items[1].name == "Closed");  // kept: its page may simply be closed
        CHECK(merged.items[2].name == "Home");
    }
}

TEST_CASE(Session_SidebarJsonHidesAndReordersThenRestores) {
    std::vector<std::string> problems;
    std::optional<th::LayoutConfig> config = th::ParseLayout(
        "{\"reorder\":true,\"items\":[{\"name\":\"Radio\"},{\"name\":\"Home\"},{\"name\":\"Browse\",\"hidden\":true}]}",
        problems);
    CHECK(config.has_value());
    auto layout = std::make_shared<th::Theme>();
    layout->usable = true;
    if (config) th::AddLayoutRules(*config, th::LayoutKind::Sidebar, *layout);
    CHECK(layout->assignments.size() == 4);
    if (!layout->assignments.empty()) {
        CHECK(layout->assignments[0].selector.text == "NavigationViewItem#Radio");
        CHECK(layout->assignments[0].source == "sidebar.json");
    }

    ListTree tree({"Home", "Browse", "Radio"});
    std::ostringstream log;
    amk::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);
    th::SessionRequest request;
    request.layout = layout;
    request.generation = 1;
    session.Tick(tree, request);
    CHECK((tree.Names() == std::vector<std::string>{"Radio", "Home", "Browse"}));
    CHECK(tree.items[2].visibility.text == "Collapsed");
    CHECK(log.str().find("(sidebar.json ligne 3)") != std::string::npos);

    th::SessionRequest none;
    none.generation = 2;
    session.Tick(tree, none);
    CHECK((tree.Names() == std::vector<std::string>{"Home", "Browse", "Radio"}));
    CHECK(tree.items[1].visibility.text == "Visible");

    th::Theme home;
    th::LayoutConfig sections;
    sections.items = {{"NewReleases", true}};
    th::AddLayoutRules(sections, th::LayoutKind::Home, home);
    CHECK(home.assignments.size() == 1);  // no reorder: only the hiding
    if (!home.assignments.empty()) CHECK(home.assignments[0].selector.text == "#NewReleases");
}

TEST_CASE(Inspector_ExportsTheTreeAsJson) {
    FakeTree tree = MakePage();
    tree.resources["AccentFillColorDefaultBrush"] = Brush("#FF0A84FF");
    tree.elements[0].properties["FlowDirection"] = {th::ValueKind::Keyword, "LeftToRight"};

    std::string text = th::InspectTreeJson(tree);
    std::string error;
    std::optional<amk::Json> json = amk::Json::Parse(text, &error);
    CHECK(json.has_value());
    if (!json) {
        amk::test::ReportFailure(__FILE__, __LINE__, "inspector.json : " + error);
        return;
    }
    CHECK(!*json->Find("truncated")->AsBool());
    const amk::Json::Array& resources = *json->Find("resources")->AsArray();
    CHECK(resources.size() == 1 && *resources[0].Find("key")->AsString() == "AccentFillColorDefaultBrush");
    const amk::Json::Array& nodes = *json->Find("nodes")->AsArray();
    CHECK(nodes.size() == 5);
    if (nodes.size() == 5) {
        CHECK(*nodes[0].Find("parent")->AsNumber() == -1);
        CHECK(*nodes[0].Find("properties")->Find("FlowDirection")->AsString() == "LeftToRight");
        CHECK(*nodes[2].Find("selector")->AsString() == "Grid#Bar > TextBlock");
        CHECK(*nodes[2].Find("parent")->AsNumber() == 1);
        CHECK(*nodes[1].Find("name")->AsString() == "Bar");
        CHECK(*nodes[1].Find("properties")->Find("Background")->AsString() == "#FF0000FF");
    }

    th::InspectOptions small;
    small.maxLines = 2;
    std::optional<amk::Json> cut = amk::Json::Parse(th::InspectTreeJson(tree, small));
    CHECK(cut && *cut->Find("truncated")->AsBool() && cut->Find("nodes")->AsArray()->size() == 2);
}

TEST_CASE(Examples_AllThemesAndSnippetsLoadWithoutProblems) {
    const std::filesystem::path source(AMK_SOURCE_DIR);
    std::vector<std::string> themes = th::ListThemeFolders(source / "themes" / "examples");
    CHECK(themes.size() >= 4);
    std::shared_ptr<const th::Theme> base;
    for (const std::string& name : themes) {
        std::filesystem::path folder = source / "themes" / "examples" / name;
        th::FolderFiles files(folder);
        th::Theme theme = th::LoadTheme(name, folder.generic_string(), files);
        CHECK(theme.usable);
        for (const std::string& problem : theme.problems) {
            amk::test::ReportFailure(__FILE__, __LINE__, name + " : " + problem);
        }
        if (name == "minuit") base = Shared(std::move(theme));
    }

    std::vector<std::string> snippets = th::ListSnippetFolders(source / "themes" / "snippets");
    CHECK(!snippets.empty());
    for (const std::string& name : snippets) {
        std::filesystem::path folder = source / "themes" / "snippets" / name;
        th::FolderFiles files(folder);
        // A snippet must work over a theme and without one.
        for (const th::Theme* over : {base.get(), static_cast<const th::Theme*>(nullptr)}) {
            th::Theme snippet = th::LoadSnippet(name, folder.generic_string(), files, over);
            CHECK(snippet.usable);
            CHECK(!snippet.assignments.empty() || !snippet.resourceDictionary.empty());
            for (const std::string& problem : snippet.problems) {
                amk::test::ReportFailure(__FILE__, __LINE__, "snippet " + name + " : " + problem);
            }
        }

        std::optional<std::string> manifest = files.Read("manifest.json");
        CHECK(manifest.has_value());
        amk::ManifestResult result =
            amk::ParseManifest(manifest.value_or(""), name, 1, [&](const std::string& file) { return files.Exists(file); });
        CHECK(result.manifest.has_value() && result.manifest->kind == amk::ElementKind::Snippet);
        for (const std::string& error : result.errors) {
            amk::test::ReportFailure(__FILE__, __LINE__, "snippet " + name + " manifest : " + error);
        }
    }
}
