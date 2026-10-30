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
#include "../core/settings.hpp"
#include "../core/theme/color.hpp"
#include "../core/theme/css.hpp"
#include "../core/theme/commands.hpp"
#include "../core/theme/engine.hpp"
#include "../core/theme/files.hpp"
#include "../core/theme/ini.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/theme/library.hpp"
#include "../core/theme/properties.hpp"
#include "../core/theme/selector.hpp"
#include "../core/theme/session.hpp"
#include "../core/theme/theme.hpp"
#include "../core/theme/theme_settings.hpp"
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
