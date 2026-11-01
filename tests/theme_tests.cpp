#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../src/core/journal.hpp"
#include "../src/core/json.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/settings.hpp"
#include "../src/core/theme/animation.hpp"
#include "../src/core/theme/builtins.hpp"
#include "../src/core/theme/color.hpp"
#include "../src/core/theme/colour_keys.hpp"
#include "../src/core/theme/css.hpp"
#include "../src/core/theme/commands.hpp"
#include "../src/core/theme/engine.hpp"
#include "../src/core/theme/files.hpp"
#include "../src/core/theme/fonts.hpp"
#include "../src/core/theme/hover.hpp"
#include "../src/core/theme/ini.hpp"
#include "../src/core/theme/inspector.hpp"
#include "../src/core/theme/layout.hpp"
#include "../src/core/theme/library.hpp"
#include "../src/core/theme/properties.hpp"
#include "../src/core/theme/scheme_choice.hpp"
#include "../src/core/theme/selector.hpp"
#include "../src/core/theme/session.hpp"
#include "../src/core/theme/snippets.hpp"
#include "../src/core/theme/theme.hpp"
#include "../src/core/theme/theme_settings.hpp"
#include "../src/core/theme/worker.hpp"
#include "test.hpp"

namespace th = ampify::theme;

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
// only its own. Removing an element makes every later call on it fail, as a closed page does; detaching it only takes
// it out of the snapshot, as a closing menu does with its entries. :hover goes through the same HoverTracker as the
// Windows backend; PointerEnter and PointerExit stand for the pointer events.
class FakeTree final : public th::TreeBackend, private th::HoverTracker::Access {
public:
    struct Element {
        th::NodeInfo info;  // `parent` holds the index of the parent element until Snapshot translates it
        bool alive = true;
        bool detached = false;
        std::map<std::string, th::Value> properties;
    };

    std::vector<Element> elements;
    std::map<std::string, th::Value> resources;
    std::string dictionary;
    std::set<std::string> refusedProperties;
    int writes = 0;
    std::vector<std::string> written;  // the properties written, in order
    bool singlePrecision = false;      // keeps lengths as WinUI does: 16.8 reads back as 16.799999237060547
    std::set<th::NodeId> followed;  // elements with pointer handlers attached
    int handlersAttached = 0;
    int handlersRemoved = 0;

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

    void Detach(th::NodeId id, bool detached = true) {
        if (Element* element = Find(id)) element->detached = detached;
    }

    bool Alive(th::NodeId id) override { return Find(id) != nullptr; }

    std::vector<th::NodeInfo> Snapshot() override {
        std::vector<int> position(elements.size(), -1);
        std::vector<th::NodeInfo> nodes;
        std::map<int, int> children;  // live children, by parent element index
        std::vector<int> parentOf;    // the parent element index of each node
        for (size_t i = 0; i < elements.size(); i++) {
            if (!elements[i].alive || elements[i].detached) continue;

            th::NodeInfo info = elements[i].info;
            int parent = elements[i].info.parent;
            info.parent = parent >= 0 ? position[parent] : -1;
            info.position = children[parent]++;
            position[i] = static_cast<int>(nodes.size());
            nodes.push_back(info);
            parentOf.push_back(parent);
        }
        for (size_t k = 0; k < nodes.size(); k++) nodes[k].siblings = children[parentOf[k]];
        return nodes;
    }

    std::optional<th::Value> Read(th::NodeId id, std::string_view property) override {
        return hover.Read(id, property, *this);
    }

    bool Write(th::NodeId id, std::string_view property, const th::Value& value) override {
        return hover.Write(id, property, value, *this);
    }

    // Properties an element shows without holding a value of its own (inherited, from its template).
    std::set<std::pair<th::NodeId, std::string>> notOwn;

    std::optional<th::Value> ReadOriginal(th::NodeId id, std::string_view property) override {
        if (notOwn.count({id, std::string(property)})) return th::kNoOwnValue;
        return Read(id, property);
    }

    std::optional<th::ViewSize> size;  // the window, for @media; unknown by default
    std::vector<th::MediaQuery> watched;

    std::optional<th::ViewSize> WindowSize() override { return size; }
    void WatchSize(const std::vector<th::MediaQuery>& queries) override { watched = queries; }

    bool SetHover(th::NodeId id, const std::vector<th::HoverValue>& values) override {
        bool alive = Find(id) != nullptr;
        if (values.empty()) {
            hover.Set(id, values, *this);
            if (followed.erase(id)) handlersRemoved++;
            return alive;
        }
        if (!alive) return false;
        if (hover.Set(id, values, *this) == th::HoverTracker::Change::Added && followed.insert(id).second) {
            handlersAttached++;
        }
        return true;
    }

    void PointerEnter(th::NodeId id) {
        if (followed.count(id)) hover.Enter(id, *this);
    }

    void PointerExit(th::NodeId id) {
        if (followed.count(id)) hover.Exit(id, *this);
    }

    void Press(th::NodeId id, bool pressed) {
        if (followed.count(id)) hover.Toggle(id, th::Trigger::Active, pressed, *this);
    }

    void Focus(th::NodeId id, bool focused) {
        if (followed.count(id)) hover.Toggle(id, th::Trigger::Focus, focused, *this);
    }

    // The value the element shows, whatever the hover.
    th::Value Shown(th::NodeId id, const std::string& property) {
        return ReadRaw(id, property).value_or(th::Value{th::ValueKind::Text, "ABSENT"});
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
    std::optional<th::Value> ReadRaw(th::NodeId id, std::string_view property) override {
        Element* element = Find(id);
        if (!element) return std::nullopt;
        auto it = element->properties.find(std::string(property));
        if (it == element->properties.end()) return std::nullopt;
        return it->second;
    }

    bool WriteRaw(th::NodeId id, std::string_view property, const th::Value& value) override {
        Element* element = Find(id);
        if (!element || refusedProperties.count(std::string(property))) return false;
        auto it = element->properties.find(std::string(property));
        if (it == element->properties.end()) return false;
        it->second = value;
        if (singlePrecision && value.kind == th::ValueKind::Length) {
            it->second.text = th::FormatNumber(static_cast<float>(std::stod(value.text)));
        }
        writes++;
        written.emplace_back(property);
        return true;
    }

    Element* Find(th::NodeId id) {
        if (id == 0 || id > elements.size() || !elements[id - 1].alive || unreachable.count(id)) return nullptr;
        return &elements[id - 1];
    }

public:
    std::set<th::NodeId> unreachable;  // listed by the snapshot, gone when reached (a template part never realized)

private:

    th::HoverTracker hover;
};

th::Value Brush(std::string text) {
    return {th::ValueKind::Color, std::move(text)};
}

th::Selector Sel(const char* text) {
    std::string problem;
    std::optional<th::Selector> selector = th::ParseSelector(text, &problem);
    CHECK(selector.has_value());
    if (!selector) {
        ampify::test::ReportFailure(__FILE__, __LINE__, "selector \"" + std::string(text) + "\": " + problem);
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
    std::filesystem::path folder = std::filesystem::temp_directory_path() / ("ampify-theme-test-" + std::string(name));
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

TEST_CASE(Ini_AndCss_ReadAFileSavedWithAByteOrderMark) {
    th::Ini ini;
    CHECK(ini.Parse("\xEF\xBB\xBF[Night]\nmain = 16131F\n").empty());
    CHECK(ini.Get("Night", "main") == "16131F");

    th::Stylesheet sheet = th::ParseStylesheet("\xEF\xBB\xBF.sidebar { corner-radius: 4 }\n");
    CHECK(sheet.problems.empty());
    CHECK(sheet.rules.size() == 1);
}

TEST_CASE(Ini_ReportsBadLinesAndKeepsTheRest) {
    th::Ini ini;
    std::vector<std::string> problems = ini.Parse("[Base]\nok = 1\nno equals here\n = 3\n[Broken\nx = 2\n");

    CHECK(problems.size() == 3);
    CHECK(ini.Get("Base", "ok") == "1");
    CHECK(ini.Get("", "x") == "2");  // the key after a broken header is kept, in a section without a name
    CHECK(!problems.empty() && problems[0].find("line 3") != std::string::npos);
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

TEST_CASE(Css_MediaBlocksGiveTheirRulesACondition) {
    th::Stylesheet sheet = th::ParseStylesheet(
        "@media (max-width: 1000px) {\n"
        "    .sidebar { margin: 1 }\n"
        "    Border { width: 2 }\n"
        "}\n"
        "Grid { margin: 2 }\n"
        "@media screen and (min-width: 600) and (max-height: 900px) { A { width: 1 } }\n"
        "@media (orientation: portrait) { B { width: 1 } }\n"
        "@supports (display: grid) { C { width: 1 } }\n"
        "@media (max-width: 500) { :root { --x: 1; } }\n"
        "D { width: 3 }\n");

    CHECK(sheet.rules.size() == 5);
    if (sheet.rules.size() != 5) return;
    CHECK(sheet.rules[0].media && sheet.rules[0].media->maxWidth == 1000);
    CHECK(sheet.rules[0].line == 2);
    CHECK(sheet.rules[1].media && sheet.rules[1].selectors[0].parts[0].type == "Border");
    CHECK(!sheet.rules[2].media);
    CHECK(sheet.rules[2].line == 5);
    const std::optional<th::MediaQuery>& both = sheet.rules[3].media;
    CHECK(both && both->minWidth == 600 && both->maxHeight == 900);
    CHECK(both && both->Matches(800, 700) && !both->Matches(500, 700) && !both->Matches(800, 1000));
    CHECK(sheet.rules[4].selectors[0].parts[0].type == "D");
    CHECK(sheet.problems.size() == 3);  // orientation, @supports, a :root inside @media
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

TEST_CASE(Css_ClassesStandForThePathsToApplesZones) {
    th::Stylesheet sheet = th::ParseStylesheet(".sidebar { margin: 8 }\n.card:hover, .playbar Button { opacity: 0.5 }\n");

    CHECK(sheet.problems.empty());
    CHECK(sheet.rules.size() == 2);
    if (sheet.rules.size() != 2) return;

    const th::Selector& sidebar = sheet.rules[0].selectors.at(0);
    CHECK(sidebar.text == ".sidebar");
    CHECK(sidebar.parts.size() == 3);
    CHECK(sidebar.parts.front().name == "RootSplitView");
    CHECK(sidebar.parts.back().name == "PaneRoot");

    CHECK(sheet.rules[1].selectors.size() == 2);
    const th::Selector& card = sheet.rules[1].selectors.at(0);
    CHECK(card.trigger == th::Trigger::Hover);
    CHECK(card.parts.back().name == "SquareLockupContent");
    const th::Selector& button = sheet.rules[1].selectors.at(1);
    CHECK(button.parts.front().name == "TransportBar");
    CHECK(button.parts.back().type == "Button");
    CHECK(button.parts.back().combinator == th::Combinator::Descendant);
}

TEST_CASE(Css_AClassWithSeveralFormsGivesOneSelectorEach) {
    th::Stylesheet sheet = th::ParseStylesheet(".page-top-spacer { height: 8 }\n");
    CHECK(sheet.problems.empty());
    CHECK(sheet.rules.size() == 1 && sheet.rules[0].selectors.size() == 5);
}

TEST_CASE(Css_ScrollbarStandsForBothScrollBarsOfEveryList) {
    th::Stylesheet sheet = th::ParseStylesheet(".scrollbar:hover { color: #888888 }\n");
    CHECK(sheet.problems.empty());
    CHECK(sheet.rules.size() == 1 && sheet.rules[0].selectors.size() == 2);
    CHECK(sheet.rules[0].selectors.at(0).parts.back().type == "ScrollBar");
    CHECK(sheet.rules[0].selectors.at(0).parts.back().name == "VerticalScrollBar");
    CHECK(sheet.rules[0].selectors.at(1).parts.back().name == "HorizontalScrollBar");
    CHECK(sheet.rules[0].selectors.at(1).trigger == th::Trigger::Hover);
}

TEST_CASE(Css_RefusesUnknownAndGluedClassesButKeepsFullTypeNames) {
    std::string problem;
    CHECK(!th::ExpandClasses(".nope", &problem));
    CHECK(problem.find(".nope") != std::string::npos);
    CHECK(!th::ExpandClasses("Grid.card", &problem));
    CHECK(problem.find("glued") != std::string::npos);
    CHECK(!th::ExpandClasses(".Sidebar", &problem));

    std::optional<std::vector<std::string>> full = th::ExpandClasses("Microsoft.UI.Xaml.Controls.Grid > .lcd");
    CHECK(full && full->size() == 1 && full->at(0).starts_with("Microsoft.UI.Xaml.Controls.Grid > NowPlayingControl"));

    th::Stylesheet sheet = th::ParseStylesheet(".nope, Grid { margin: 1 }\nGrid#A { margin: 2 }\n");
    CHECK(sheet.problems.size() == 1);
    CHECK(sheet.rules.size() == 1);
}

TEST_CASE(Css_ImportBringsAReadyMadeLayoutAtItsPlace) {
    th::Stylesheet sheet = th::ParseStylesheet(
        "@import \"ampify:floating-panels\";\n@import url(ampify:bottom-bar);\n.sidebar { margin: 1 }\n");

    CHECK(sheet.problems.empty());
    CHECK(sheet.rules.size() > 3);
    if (sheet.rules.size() < 3) return;
    CHECK(sheet.rules.front().origin == "ampify:floating-panels");
    CHECK(sheet.rules[sheet.rules.size() - 2].origin == "ampify:bottom-bar");
    CHECK(sheet.rules.back().origin.empty());  // the stylesheet's own rule comes last, so it wins at equal weight

    th::Stylesheet unknown = th::ParseStylesheet("@import \"ampify:nope\";\n@import url(x.css);\nGrid { margin: 1 }\n");
    CHECK(unknown.problems.size() == 2);
    CHECK(!unknown.problems.empty() && unknown.problems[0].find("bottom-bar") != std::string::npos);
    CHECK(unknown.rules.size() == 1);
}

TEST_CASE(Builtins_EveryClassAndLayoutIsValid) {
    for (const std::string& name : th::ClassNames()) {
        std::optional<std::vector<std::string>> selectors = th::ClassSelectors(name);
        CHECK(selectors && !selectors->empty());
        if (!selectors) continue;
        for (const std::string& selector : *selectors) {
            std::string problem;
            CHECK(th::ParseSelector(selector, &problem));
            CHECK(problem.empty());
        }
    }
    for (const std::string& name : th::BuiltinStylesheetNames()) {
        th::Stylesheet sheet = th::ParseStylesheet(*th::BuiltinStylesheet(name));
        CHECK(sheet.problems.empty());
        for (const th::Rule& rule : sheet.rules) {
            for (const th::Declaration& declaration : rule.declarations) CHECK(th::FindProperty(declaration.property));
        }
    }
}

TEST_CASE(Builtins_EveryClassAndLayoutIsDocumented) {
    auto read = [](const char* relative) {
        std::ifstream in(std::filesystem::path(AMPIFY_SOURCE_DIR) / relative, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    };
    std::string names = read("docs/themes/names.md");
    std::string format = read("docs/themes/format.md");
    CHECK(!names.empty() && !format.empty());
    std::string undocumented;  // the classes missing from names.md, kept to read in a debugger
    for (const std::string& name : th::ClassNames()) {
        if (names.find("`." + name + "`") == std::string::npos) undocumented += " ." + name;
    }
    CHECK(undocumented.empty());
    for (const std::string& name : th::BuiltinStylesheetNames()) CHECK(format.find("`ampify:" + name + "`") != std::string::npos);
}

TEST_CASE(Theme_ALayoutTakesTheThemesVariablesAndReportsUnderItsName) {
    MemoryFiles files;
    files.files["user.css"] = ":root { --gap: 12; --panel-background: #FF112233; }\n@import \"ampify:floating-panels\";\n";

    th::Theme theme = Load(files);
    CHECK(theme.problems.empty());
    std::map<std::string, std::string> margins;
    for (const th::Assignment& assignment : theme.assignments) {
        if (assignment.selector.text != ".sidebar" && assignment.selector.text != ".content") continue;
        CHECK(assignment.source == "ampify:floating-panels");
        if (assignment.property == "Margin") margins[assignment.selector.text] = assignment.value.text;
    }
    // XAML order: left, top, right, bottom. The sidebar reaches a gap into the page column, the pages start two gaps
    // into it: one gap between the panels, open or folded.
    CHECK(margins[".sidebar"] == "12,12,-12,12");
    CHECK(margins[".content"] == "24,64,12,12");
}

TEST_CASE(Inspector_ReadsASavedTreeWithEachPlace) {
    std::optional<std::vector<th::NodeInfo>> nodes = th::ReadInspectorTree(R"({"version":1,"nodes":[
        {"index":0,"type":"Grid","fullType":"","name":"TransportBar","parent":-1,"depth":0},
        {"index":1,"type":"Grid","name":"","parent":0,"depth":1},
        {"index":2,"type":"Grid","name":"","parent":0,"depth":1},
        {"index":3,"type":"TextBlock","name":"Title","parent":2,"depth":2}]})");

    CHECK(nodes && nodes->size() == 4);
    if (!nodes || nodes->size() != 4) return;
    CHECK((*nodes)[2].position == 1 && (*nodes)[2].siblings == 2);
    CHECK((*nodes)[3].position == 0 && (*nodes)[3].siblings == 1);
    std::optional<th::Selector> second = th::ParseSelector("Grid#TransportBar > Grid:nth-child(2)");
    CHECK(second && th::Matches(*second, *nodes, 2) && !th::Matches(*second, *nodes, 1));

    std::string problem;
    CHECK(!th::ReadInspectorTree("{\"nodes\":[{\"type\":\"Grid\",\"parent\":3,\"depth\":0}]}", &problem));
    CHECK(problem.find("before its parent") != std::string::npos);
    CHECK(!th::ReadInspectorTree("[]", &problem));
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

TEST_CASE(Selector_StructuralPseudoClassesCountFromOne) {
    // Grid#Bar with three Grid children, the second holding a TextBlock.
    std::vector<th::NodeInfo> nodes(5);
    nodes[0] = {1, "Grid", "", "Bar", -1, 0};
    nodes[1] = {2, "Grid", "", "", 0, 1};
    nodes[2] = {3, "Grid", "", "", 0, 1};
    nodes[3] = {4, "TextBlock", "", "", 2, 2};
    nodes[4] = {5, "Grid", "", "", 0, 1};
    for (int i : {1, 2, 4}) nodes[i].siblings = 3;
    nodes[1].position = 0;
    nodes[2].position = 1;
    nodes[4].position = 2;
    nodes[3].position = 0;
    nodes[3].siblings = 1;

    CHECK(th::Matches(Sel("Grid#Bar > Grid:nth-child(2)"), nodes, 2));
    CHECK(!th::Matches(Sel("Grid#Bar > Grid:nth-child(2)"), nodes, 1));
    CHECK(th::Matches(Sel("Grid#Bar > Grid:first-child"), nodes, 1));
    CHECK(th::Matches(Sel("Grid#Bar > Grid:last-child"), nodes, 4));
    CHECK(!th::Matches(Sel("Grid#Bar > Grid:last-child"), nodes, 2));
    CHECK(th::Matches(Sel("Grid:nth-child(2) TextBlock:first-child:last-child"), nodes, 3));
    CHECK(th::SpecificityOf(Sel("Grid:nth-child(2)")) > th::SpecificityOf(Sel("Grid Grid")));
    CHECK(th::SpecificityOf(Sel("#Bar")) > th::SpecificityOf(Sel("Grid:nth-child(2)")));

    std::string problem;
    CHECK(!th::ParseSelector("Grid:nth-child(0)", &problem));
    CHECK(problem.find("counts from 1") != std::string::npos);
    CHECK(!th::ParseSelector("Grid:nth-child(odd)", &problem));
    CHECK(!th::ParseSelector("Grid:nth-child", &problem));
    CHECK(th::ParseSelector("Grid:nth-child(3):hover", &problem));
}

TEST_CASE(Selector_SpecificityOrdersNamesBeforeTypes) {
    CHECK(th::SpecificityOf(Sel("#Bar TextBlock")) > th::SpecificityOf(Sel("Grid > TextBlock > Border")));
    CHECK(th::SpecificityOf(Sel("Grid > TextBlock")) < th::SpecificityOf(Sel("Grid#A TextBlock")));
    CHECK(th::SpecificityOf(Sel("*")) == th::SpecificityOf(Sel("*")));
}

TEST_CASE(Selector_RejectsWhatItDoesNotKnow) {
    std::string problem;
    CHECK(!th::ParseSelector("Grid[Name=x]", &problem));
    CHECK(problem.find("is not supported") != std::string::npos);
    CHECK(!th::ParseSelector("Grid:checked", &problem));
    CHECK(problem.find(":checked") != std::string::npos);
    CHECK(!th::ParseSelector("Grid:hover > TextBlock", &problem));
    CHECK(problem.find("last element") != std::string::npos);
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
    CHECK(Converted("background", "url(images/missing.png)", palette, files).find("not found") != std::string::npos);
    CHECK(Converted("background", "url(../secret.png)", palette, files).find("not allowed") != std::string::npos);
    CHECK(Converted("background", "nonsense", palette, files).rfind("PROBLEM", 0) == 0);
}

TEST_CASE(Properties_ConvertLengthsAndThickness) {
    th::Palette palette;
    MemoryFiles files;

    CHECK(Converted("font-size", "18px", palette, files) == "18");
    CHECK(Converted("font-size", "1.5", palette, files) == "1.5");
    // Relative to the element's own size; the engine works out the pixels.
    CHECK(Converted("font-size", "120%", palette, files) == "120%");
    CHECK(Converted("font-size", "1.25em", palette, files) == "125%");
    CHECK(Converted("font-size", "0%", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(Converted("font-size", "1.2rem", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(Converted("width", "50%", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(th::ScaleFontSize({th::ValueKind::Length, "120%"}, {th::ValueKind::Length, "14"})->text == "16.8");
    CHECK(!th::ScaleFontSize({th::ValueKind::Length, "120%"}, {th::ValueKind::Length, "NaN"}));

    // WinUI keeps numbers in single precision: what it reads back is still the value written.
    CHECK(th::SameValue({th::ValueKind::Length, "16.8"}, {th::ValueKind::Length, "16.799999237060547"}));
    CHECK(th::SameValue({th::ValueKind::Thickness, "8,4.1,8,4"}, {th::ValueKind::Thickness, "8,4.099999904632568,8,4"}));
    CHECK(!th::SameValue({th::ValueKind::Length, "16.8"}, {th::ValueKind::Length, "16.9"}));
    CHECK(!th::SameValue({th::ValueKind::Thickness, "8,4,8,4"}, {th::ValueKind::Thickness, "8,4,8"}));
    CHECK(!th::SameValue({th::ValueKind::Color, "#FF000000"}, {th::ValueKind::Color, "#FF000001"}));
    CHECK(Converted("width", "auto", palette, files) == "NaN");
    CHECK(Converted("width", "wide", palette, files).rfind("PROBLEM", 0) == 0);

    // CSS order is top right bottom left; XAML order is left,top,right,bottom.
    CHECK(Converted("margin", "8", palette, files) == "8,8,8,8");
    CHECK(Converted("margin", "4 8", palette, files) == "8,4,8,4");
    CHECK(Converted("margin", "1 2 3", palette, files) == "2,1,2,3");
    CHECK(Converted("margin", "1 2 3 4", palette, files) == "4,1,2,3");
    CHECK(Converted("margin", "1 2 3 4 5", palette, files).rfind("PROBLEM", 0) == 0);

    // As in CSS, only a margin goes below zero: a template's Border refuses the rest, and WinUI then stops the app.
    CHECK(Converted("margin", "0 -12", palette, files) == "-12,0,-12,0");
    CHECK(Converted("padding", "0 0 0 -2", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(Converted("border-width", "-1", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(Converted("corner-radius", "8 -4", palette, files).rfind("PROBLEM", 0) == 0);
}

TEST_CASE(Properties_CornersShowInXamlOrder) {
    // Kept in the order of a thickness, bottom-left first; shown the way XAML writes a CornerRadius, top-left first.
    th::Palette palette;
    MemoryFiles files;
    std::string kept = Converted("corner-radius", "0 12 12 0", palette, files);
    CHECK(kept == "0,0,12,12");
    CHECK(th::ShownText("CornerRadius", {th::ValueKind::Thickness, kept}) == "0,12,12,0");
    CHECK(th::ShownText("CornerRadius", {th::ValueKind::Thickness, "4,1,2,3"}) == "1,2,3,4");
    CHECK(th::ShownText("Margin", {th::ValueKind::Thickness, "4,1,2,3"}) == "4,1,2,3");
    CHECK(th::ShownText("CornerRadius", {th::ValueKind::Thickness, "8"}) == "8");
}

TEST_CASE(Properties_AcceptTheCssNamesThemesUseMost) {
    th::Palette palette;
    MemoryFiles files;

    // border-radius is what every Spicetify theme writes; it is corner-radius under another name.
    CHECK(th::FindProperty("border-radius") && th::FindProperty("border-radius")->xaml == "CornerRadius");
    CHECK(Converted("border-radius", "8px", palette, files) == Converted("corner-radius", "8px", palette, files));
    CHECK(Converted("min-width", "120px", palette, files) == "120");
    CHECK(Converted("max-height", "64", palette, files) == "64");
    CHECK(Converted("max-width", "none", palette, files) == "inf");
    CHECK(Converted("min-height", "none", palette, files).rfind("PROBLEM", 0) == 0);
    CHECK(Converted("min-width", "auto", palette, files) == "0");
    CHECK(th::FindProperty("min-height")->xaml == "MinHeight");
    CHECK(th::FindProperty("max-width")->xaml == "MaxWidth");
    // The width of the folded sidebar, for layouts that put margins around it.
    CHECK(th::FindProperty("compact-pane-length") && th::FindProperty("compact-pane-length")->xaml == "CompactPaneLength");
    CHECK(Converted("compact-pane-length", "60px", palette, files) == "60");
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
    CHECK(!ProblemContaining(missing, "scheme \"Light\" not found").empty());
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
    CHECK(!ProblemContaining(unchecked, "check not done").empty());
}

// --- Spicetify's color.ini, read as is ------------------------------------------------------------------------

const th::Assignment* AssignmentFor(const th::Theme& theme, std::string_view selector, std::string_view property) {
    for (const th::Assignment& assignment : theme.assignments) {
        if (assignment.selector.text == selector && assignment.property == property) return &assignment;
    }
    return nullptr;
}

std::string ResourceValue(const th::Theme& theme, std::string_view key) {
    for (const th::ResourceOverride& resource : theme.resources) {
        if (resource.key == key) return resource.value.text;
    }
    return "ABSENT";
}

TEST_CASE(Spicetify_ColorIniPaintsAppleMusicAsIs) {
    MemoryFiles files;
    files.files["color.ini"] =
        "[Nord]\nmain = 2E3440\nsidebar = 3B4252\nplayer = 434C5E\ntext = ECEFF4\nsubtext = D8DEE9\n"
        "button = 88C0D0\naccent = BF616A\n\n[Snow]\nmain = ECEFF4\ntext = 2E3440\n";
    files.files["user.css"] = "Grid#Bar { background: var(--spice-accent); color: $button; }\n";

    th::Theme theme = Load(files);
    CHECK(theme.usable);
    CHECK(theme.scheme == "Nord");
    CHECK(theme.problems.empty());
    CHECK(theme.dark.has_value() && *theme.dark);

    // The app brushes that play the part of each key, and no resource named after a Spicetify key.
    CHECK(ResourceValue(theme, "TextFillColorPrimaryBrush") == "#FFECEFF4");
    CHECK(ResourceValue(theme, "TextFillColorSecondaryBrush") == "#FFD8DEE9");
    CHECK(ResourceValue(theme, "SolidBackgroundFillColorBaseBrush") == "#FF2E3440");
    CHECK(ResourceValue(theme, "AccentFillColorDefaultBrush") == "#FF88C0D0");
    CHECK(ResourceValue(theme, "main") == "ABSENT");
    CHECK(ResourceValue(theme, "accent") == "ABSENT");

    // Apple Music's own elements, on the layer under user.css.
    const th::Assignment* sidebar = AssignmentFor(theme, "Grid#PaneRoot", "Background");
    CHECK(sidebar && sidebar->value.text == "#FF3B4252" && sidebar->layer == -1);
    const th::Assignment* player = AssignmentFor(theme, "BackDrop", "Background");
    CHECK(player && player->value.text == "#FF434C5E");
    const th::Assignment* own = AssignmentFor(theme, "Grid#Bar", "Background");
    CHECK(own && own->value.text == "#FFBF616A" && own->layer == 0);

    th::Theme snow = Load(files, {"Snow", nullptr, {}});
    CHECK(snow.dark.has_value() && !*snow.dark);
    CHECK(AssignmentFor(snow, "Grid#PaneRoot", "Background") == nullptr);  // no sidebar key: Apple Music's own
}

TEST_CASE(Theme_TextOnALightAccentTurnsDark) {
    MemoryFiles files;
    files.files["colors.ini"] =
        "[Cyan]\nAccentFillColorDefaultBrush = 4FD1FF\n\n"
        "[Blue]\nAccentFillColorDefaultBrush = 2F6FEB\n\n"
        "[Own]\nAccentFillColorDefaultBrush = 4FD1FF\nTextOnAccentFillColorPrimaryBrush = 112233\n";

    // White on a light cyan reads at 1.8:1: the text on the accent goes dark, as in Fluent's dark mode.
    th::Theme cyan = Load(files, {"Cyan", nullptr, {}});
    CHECK(ResourceValue(cyan, "TextOnAccentFillColorPrimaryBrush") == "#FF000000");
    CHECK(ResourceValue(cyan, "TextOnAccentFillColorSecondaryBrush") == "#B3000000");

    // On a blue white reads on, Apple Music's own white stays.
    th::Theme blue = Load(files, {"Blue", nullptr, {}});
    CHECK(ResourceValue(blue, "TextOnAccentFillColorPrimaryBrush") == "ABSENT");

    // A theme that names the colour keeps it.
    th::Theme own = Load(files, {"Own", nullptr, {}});
    CHECK(ResourceValue(own, "TextOnAccentFillColorPrimaryBrush") == "#FF112233");

    // A Spicetify button colour counts as the accent.
    files.files["colors.ini"] = "[Green]\nbutton = 1ED760\n";
    th::Theme green = Load(files);
    CHECK(ResourceValue(green, "TextOnAccentFillColorPrimaryBrush") == "#FF000000");

    // A colour the theme's resources.xaml gives stays its own.
    files.files["resources.xaml"] =
        "<ResourceDictionary><SolidColorBrush x:Key=\"TextOnAccentFillColorPrimaryBrush\"/></ResourceDictionary>";
    th::Theme dictionary = Load(files);
    CHECK(ResourceValue(dictionary, "TextOnAccentFillColorPrimaryBrush") == "ABSENT");
}

TEST_CASE(Theme_HoverAndPressedAccentsFollowTheAccent) {
    MemoryFiles files;
    files.files["colors.ini"] =
        "[Only]\nAccentFillColorDefaultBrush = 0A84FF\n\n"
        "[Hover]\nAccentFillColorDefaultBrush = 0A84FF\nAccentFillColorSecondaryBrush = 112233\n";

    // Fluent's accent under the pointer and while pressed: the accent at 90% and 80%.
    th::Theme only = Load(files, {"Only", nullptr, {}});
    CHECK(ResourceValue(only, "AccentFillColorSecondaryBrush") == "#E60A84FF");
    CHECK(ResourceValue(only, "AccentFillColorTertiaryBrush") == "#CC0A84FF");

    // A state the theme names stays its own; the other one still follows the accent.
    th::Theme hover = Load(files, {"Hover", nullptr, {}});
    CHECK(ResourceValue(hover, "AccentFillColorSecondaryBrush") == "#FF112233");
    CHECK(ResourceValue(hover, "AccentFillColorTertiaryBrush") == "#CC0A84FF");

    // A key the theme's resources.xaml defines is left to it.
    files.files["resources.xaml"] =
        "<ResourceDictionary><SolidColorBrush x:Key=\"AccentFillColorTertiaryBrush\"/></ResourceDictionary>";
    std::set<std::string> allowed{"AccentFillColorTertiaryBrush"};
    th::Theme dictionary = Load(files, {"Only", &allowed, {}});
    CHECK(ResourceValue(dictionary, "AccentFillColorSecondaryBrush") == "#E60A84FF");
    CHECK(ResourceValue(dictionary, "AccentFillColorTertiaryBrush") == "ABSENT");
}

TEST_CASE(Spicetify_ThemeRulesWinOverTheTranslatedOnes) {
    MemoryFiles files;
    files.files["color.ini"] = "[Base]\nsidebar = 112233\n";
    files.files["user.css"] = "Grid { background: #FF445566; }\n";  // less precise than Grid#PaneRoot, but the theme's
    th::Theme theme = Load(files);

    FakeTree tree;
    th::NodeId pane = tree.Add(-1, "Grid", "PaneRoot", {{"Background", Brush("#FF000000")}});
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);
    engine.Apply(theme, tree);
    CHECK(tree.Shown(pane, "Background").text == "#FF445566");
}

TEST_CASE(Theme_ArtworkIsTheSongPlayingAsABackground) {
    MemoryFiles files;
    files.files["user.css"] = "Grid#PageRoot { background: artwork; }\nGrid#A { background: artwork contain; }\n"
                              "Grid#B { background: artwork sideways; }\n";
    th::Theme theme = Load(files);
    const th::Assignment* cover = AssignmentFor(theme, "Grid#PageRoot", "Background");
    CHECK(cover && cover->value.kind == th::ValueKind::Image && cover->value.text == "artwork:|UniformToFill");
    const th::Assignment* contain = AssignmentFor(theme, "Grid#A", "Background");
    CHECK(contain && contain->value.text == "artwork:|Uniform");
    CHECK(AssignmentFor(theme, "Grid#B", "Background") == nullptr);
    CHECK(!ProblemContaining(theme, "unknown image fit").empty());
}

TEST_CASE(Spicetify_ReadsTheKeysOfSpicetifyOne) {
    MemoryFiles files;
    files.files["color.ini"] = "[Base]\nmain_fg = FFFFFF\nmain_bg = 000000\nsidebar_and_player_bg = 101010\n";
    th::Theme theme = Load(files);
    CHECK(ResourceValue(theme, "TextFillColorPrimaryBrush") == "#FFFFFFFF");
    CHECK(AssignmentFor(theme, "BackDrop", "Background") != nullptr);  // player falls back to the sidebar colour
    CHECK(theme.variables.count("spice-text") == 1);
    CHECK(theme.dark.has_value() && *theme.dark);
}

TEST_CASE(Theme_NeedsAtLeastOneThemeFile) {
    MemoryFiles files;
    files.files["readme.txt"] = "not a theme";
    th::Theme theme = Load(files);
    CHECK(!theme.usable);
    CHECK(!ProblemContaining(theme, "not a theme").empty());
}

TEST_CASE(Theme_ReportsBadRulesWithTheirLineAndKeepsTheGoodOnes) {
    MemoryFiles files;
    files.files["user.css"] = "TextBlock {\n  font-size: wide;\n  unknown-prop: 3;\n  background: #FFF;\n}\n";

    th::Theme theme = Load(files);
    CHECK(theme.usable);
    CHECK(theme.assignments.size() == 1);
    CHECK(!ProblemContaining(theme, "line 2").empty());
    CHECK(!ProblemContaining(theme, "line 3").empty());
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

TEST_CASE(Engine_RestoreClearsWhatTheElementOnlyInherited) {
    MemoryFiles files;
    files.files["user.css"] = "FontIcon { color: #FF000000; }\nTextBlock { color: #FF00FF00; }\n";
    th::Theme theme = Load(files);

    FakeTree tree;
    th::NodeId icon = tree.Add(-1, "FontIcon", "", {{"Foreground", Brush("#FFE4E4E4")}});
    th::NodeId text = tree.Add(-1, "TextBlock", "", {{"Foreground", Brush("#FF111111")}});
    tree.notOwn.insert({icon, "Foreground"});  // the icon shows its button's colour
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    engine.Apply(theme, tree);
    CHECK(tree.Shown(icon, "Foreground").text == "#FF000000");
    engine.Restore(tree);

    // The icon gets no fixed colour back, so it follows its button again; the text had its own and gets it back.
    CHECK(tree.Shown(icon, "Foreground") == th::kNoOwnValue);
    CHECK(tree.Shown(text, "Foreground").text == "#FF111111");
}

TEST_CASE(Engine_AppliesTheCascadeAndRestoresTheOriginals) {
    MemoryFiles files;
    files.files["user.css"] = kPageRules;
    th::Theme theme = Load(files);

    FakeTree tree = MakePage();
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
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

    CHECK(log.str().find("Applied: Page Background = #FF101010") != std::string::npos);
    CHECK(log.str().find("Theme removed: 8 value(s) restored") != std::string::npos);
}

TEST_CASE(Engine_RefusesWhatAnElementCannotTake) {
    MemoryFiles files;
    files.files["user.css"] = "Border { font-family: Arial; }\n";
    th::Theme theme = Load(files);

    FakeTree tree = MakePage();
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report report = engine.Apply(theme, tree);
    CHECK(report.applied == 0);
    CHECK(report.refused == 1);  // the Border has no FontFamily; the refusal is written once
    CHECK(log.str().find("Refused: \"Border#Hide\" has no property FontFamily") != std::string::npos);

    engine.Apply(theme, tree);
    CHECK(log.str().find("Refused") == log.str().rfind("Refused"));
}

TEST_CASE(Engine_LeavesTheFontOfAnIconAlone) {
    MemoryFiles files;
    files.files["user.css"] = "TextBlock { font-family: Arial; font-size: 20px; }\n";
    th::Theme theme = Load(files);

    FakeTree tree;
    std::map<std::string, th::Value> font = {{"FontFamily", {th::ValueKind::Text, "Segoe UI"}},
                                             {"FontSize", {th::ValueKind::Length, "12"}}};
    tree.Add(-1, "Grid", "Bar");
    th::NodeId text = tree.Add(0, "TextBlock", "", font);
    th::NodeId icon = tree.Add(0, "TextBlock", "Star", font);
    tree.elements[2].info.icon = true;  // TextBlock#Star draws an icon glyph
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    engine.Apply(theme, tree);
    CHECK(tree.Shown(text, "FontFamily").text == "Arial");
    CHECK(tree.Shown(icon, "FontFamily").text == "Segoe UI");
    CHECK(tree.Shown(icon, "FontSize").text == "20");  // the rest of the rule still applies to it
    CHECK(log.str().find("Refused") == std::string::npos);
}

TEST_CASE(Engine_ScalesEachTextFromItsOwnSize) {
    MemoryFiles files;
    files.files["user.css"] = "TextBlock { font-size: 120%; }\nTextBlock:hover { font-size: 1.5em; }\n";
    th::Theme theme = Load(files);
    CHECK(theme.problems.empty());

    FakeTree tree;
    tree.singlePrecision = true;
    th::NodeId body = tree.Add(-1, "TextBlock", "Body", {{"FontSize", {th::ValueKind::Length, "14"}}});
    th::NodeId title = tree.Add(-1, "TextBlock", "Title", {{"FontSize", {th::ValueKind::Length, "28"}}});
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);
    auto size = [&](th::NodeId id, const char* pixels) {
        return th::SameValue(tree.Shown(id, "FontSize"), {th::ValueKind::Length, pixels});
    };

    engine.Apply(theme, tree);
    CHECK(size(body, "16.8"));
    CHECK(size(title, "33.6"));  // still twice the body text

    // The next pass reads back 16.799999237060547, the same size, and still starts from the size before the theme.
    CHECK(engine.Apply(theme, tree).applied == 0);
    CHECK(size(body, "16.8"));

    tree.PointerEnter(title);
    CHECK(size(title, "50.4"));  // 150% of the theme's size underneath
    tree.PointerExit(title);

    engine.Restore(tree);
    CHECK(tree.Shown(body, "FontSize").text == "14");
    CHECK(tree.Shown(title, "FontSize").text == "28");
}

TEST_CASE(Theme_AFontRoleTakesPixels) {
    MemoryFiles files;
    files.files["user.css"] = ":root { --font-body-size: 120%; }\n";
    th::Theme theme = Load(files);
    CHECK(theme.problems.size() == 1);
    if (!theme.problems.empty()) CHECK(theme.problems[0].find("a font role takes pixels") != std::string::npos);
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
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
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
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
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
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report report = engine.Apply(theme, tree);
    CHECK(tree.resources["AccentFillColorDefaultBrush"].text == "#FF0A84FF");
    CHECK(report.refused == 1);  // Ghost is not an application resource
    CHECK(log.str().find("resource missing from the app: Ghost") != std::string::npos);
    CHECK(tree.dictionary == theme.resourceDictionary);

    engine.Restore(tree);
    CHECK(tree.resources["AccentFillColorDefaultBrush"].text == "#FF000000");
    CHECK(tree.dictionary.empty());
}

TEST_CASE(Engine_DoesNotRefuseAValueForAnElementGoneSinceTheSnapshot) {
    // The hidden chevron of an AppBarButton is listed, then cannot be reached: nothing to write, and no fault of the
    // theme either.
    MemoryFiles files;
    files.files["user.css"] = "Button FontIcon { foreground: #FF000000; }\n";
    th::Theme theme = Load(files);
    FakeTree tree;
    tree.Add(-1, "Button", "Play");
    th::NodeId icon = tree.Add(0, "FontIcon", "", {{"Foreground", Brush("#FFFFFFFF")}});
    th::NodeId chevron = tree.Add(0, "FontIcon", "SubItemChevron");
    tree.unreachable.insert(chevron);
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report report = engine.Apply(theme, tree);
    CHECK(report.refused == 0);
    CHECK(report.applied == 1);
    CHECK(tree.elements[icon - 1].properties["Foreground"].text == "#FF000000");
}

TEST_CASE(Engine_ReportsADictionaryTheInterfaceRefuses) {
    MemoryFiles files;
    files.files["resources.xaml"] = "<ResourceDictionary>REFUSE</ResourceDictionary>";
    th::Theme theme = Load(files);
    CHECK(theme.usable);

    FakeTree tree;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report report = engine.Apply(theme, tree);
    CHECK(report.refused == 1);
    CHECK(tree.dictionary.empty());
}

// --- Library, watcher and settings -----------------------------------------------------------------------

TEST_CASE(Library_ListsOnlyFoldersWithThemeFiles) {
    std::string root = TempFolder("library");
    std::filesystem::path base(root);
    std::filesystem::create_directories(base / "midnight");
    std::filesystem::create_directories(base / "empty");
    std::filesystem::create_directories(base / "css");
    { std::ofstream(base / "midnight" / "colors.ini") << "[Base]\n"; }
    { std::ofstream(base / "css" / "user.css") << "Page {}\n"; }

    std::vector<std::string> folders = th::ListThemeFolders(base);
    CHECK(folders.size() == 2);
    if (folders.size() == 2) {
        CHECK(folders[0] == "css");
        CHECK(folders[1] == "midnight");
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
    ampify::Settings settings;
    th::ThemeSettings value;
    value.active = "midnight";
    value.scheme = "Dark";
    value.liveReload = false;
    value.inspect = true;
    th::WriteThemeSettings(settings, value);

    th::ThemeSettings back = th::ReadThemeSettings(settings);
    CHECK(back.active == "midnight");
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

TEST_CASE(ColourKeys_AreSortedUniqueAndNameTheFluentBrushes) {
    std::span<const std::string_view> keys = th::ColourResourceKeys();
    CHECK(keys.size() > 500);
    CHECK(std::adjacent_find(keys.begin(), keys.end(), [](auto a, auto b) { return a >= b; }) == keys.end());
    CHECK(std::binary_search(keys.begin(), keys.end(), std::string_view("TextFillColorPrimaryBrush")));
    CHECK(std::binary_search(keys.begin(), keys.end(), std::string_view("PaneBackgroundBrush")));
    CHECK(std::binary_search(keys.begin(), keys.end(), std::string_view("ListViewItemBackgroundSelected")));
}

TEST_CASE(Inspector_DumpsResourcesAndElements) {
    FakeTree tree = MakePage();
    tree.resources["AccentFillColorDefaultBrush"] = Brush("#FF0A84FF");

    std::string dump = th::InspectTree(tree);
    CHECK(dump.find("Application resources (1):") != std::string::npos);
    CHECK(dump.find("AccentFillColorDefaultBrush = #FF0A84FF") != std::string::npos);
    CHECK(dump.find("(5 elements)") != std::string::npos);
    CHECK(dump.find("  Grid#Bar  selector: Grid#Bar  Background=#FF0000FF") != std::string::npos);
    CHECK(dump.find("    TextBlock  selector: Grid#Bar > TextBlock") != std::string::npos);

    th::InspectOptions small;
    small.maxLines = 2;
    CHECK(th::InspectTree(tree, small).find("stopped after 2 lines") != std::string::npos);
}

TEST_CASE(Inspector_ShowsTheColourOfAnAppBrushKeptAsAnObject) {
    FakeTree tree;
    tree.Add(-1, "Grid", "LCD", {{"Background", {th::ValueKind::Opaque, "handle:3;#14FFFFFF"}},
                                 {"BorderBrush", {th::ValueKind::Opaque, "handle:4"}}});

    std::string dump = th::InspectTree(tree);
    CHECK(dump.find("Background=#14FFFFFF") != std::string::npos);
    CHECK(dump.find("BorderBrush=present") != std::string::npos);
}

// --- Example themes shipped in the repository ------------------------------------------------------------

TEST_CASE(Examples_LoadWithoutProblems) {
    for (const char* name : {"midnight", "greenroom", "hide-chrome", "glass", "artwork"}) {
        std::filesystem::path folder = std::filesystem::path(AMPIFY_SOURCE_DIR) / "themes" / name;
        th::FolderFiles files(folder);
        th::Theme theme = th::LoadTheme(name, folder.generic_string(), files);

        CHECK(theme.usable);
        for (const std::string& problem : theme.problems) {
            ampify::test::ReportFailure(__FILE__, __LINE__, std::string(name) + " : " + problem);
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
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
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

TEST_CASE(Session_AThemeEditedLiveGoesOverItself) {
    MemoryFiles files;
    files.files["colors.ini"] = "[Base]\nAccentFillColorDefaultBrush = 0A84FF\n";
    files.files["user.css"] = "Page { background: #FF101010; }\nGrid#Bar { background: #FF202020; }\n";
    auto first = std::make_shared<const th::Theme>(Load(files));
    MemoryFiles edited;
    edited.files["user.css"] = "Page { background: #FF303030; }\n";  // the Bar rule and the accent are gone
    auto second = std::make_shared<const th::Theme>(Load(edited));

    FakeTree tree = MakePage();
    tree.resources["AccentFillColorDefaultBrush"] = Brush("#FF000000");
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);

    th::SessionRequest request;
    request.themeId = "live";
    request.theme = first;
    request.generation = 1;
    session.Tick(tree, request);
    CHECK(tree.elements[1].properties["Background"].text == "#FF202020");
    CHECK(tree.resources["AccentFillColorDefaultBrush"].text == "#FF0A84FF");

    // Saved again: the new version goes over the old one, with no restore of everything in between.
    request.theme = second;
    request.generation = 2;
    session.Tick(tree, request);
    CHECK(log.str().find("Theme removed") == std::string::npos);
    CHECK(tree.elements[0].properties["Background"].text == "#FF303030");
    CHECK(tree.elements[1].properties["Background"].text == "#FF0000FF");  // given back
    CHECK(tree.resources["AccentFillColorDefaultBrush"].text == "#FF000000");
    // The pass after the save is reported: the new background, the Bar and the accent given back.
    CHECK(log.str().find(": 3 value(s) applied") != std::string::npos);

    // Another theme still starts from Apple Music's own values.
    request.themeId = "other";
    request.theme = first;
    request.generation = 3;
    session.Tick(tree, request);
    CHECK(log.str().find("Theme removed") != std::string::npos);
    CHECK(tree.elements[0].properties["Background"].text == "#FF101010");
}

TEST_CASE(Session_ReturnsAnInspectorDumpOnlyWhenAsked) {
    FakeTree tree = MakePage();
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);

    th::SessionRequest request;
    CHECK(!session.Tick(tree, request).has_value());

    request.inspect = true;
    std::optional<std::string> dump = session.Tick(tree, request);
    CHECK(dump.has_value());
    if (dump) CHECK(dump->find("Tree (5 elements)") != std::string::npos);

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
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);

    th::SessionRequest request;
    request.themeId = "broken";
    request.theme = refused;
    request.generation = 1;
    session.Tick(tree, request);
    CHECK(!session.Active());
    CHECK(log.str().find("\"broken\" refused") != std::string::npos);
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
    ampify::Settings settings;
    th::ThemeSettings value;
    value.parts = {false, true, false};
    th::WriteThemeSettings(settings, value);
    th::ThemeSettings back = th::ReadThemeSettings(settings);
    CHECK(!back.parts.colors);
    CHECK(back.parts.resources);
    CHECK(!back.parts.assets);
    CHECK(th::ReadThemeSettings(ampify::Settings{}).parts.colors);  // defaults are on
}

// --- Commands: ampify theme list, ampify theme set, ampify watch --------------------------------------------------

namespace {

// A kit in a temporary folder, with a usable theme `midnight` (two schemes) and a refused theme `broken`.
th::KitLayout MakeKit(const char* name) {
    th::KitLayout kit{std::filesystem::path(TempFolder(name))};
    std::filesystem::create_directories(kit.Themes() / "midnight");
    std::filesystem::create_directories(kit.Themes() / "broken");
    { std::ofstream(kit.Themes() / "midnight" / "colors.ini") << "[Base]\nAccent = 0A84FF\n[Dark]\nAccent = 1C1C1E\n"; }
    { std::ofstream(kit.Themes() / "midnight" / "user.css") << "Page { background: $accent; }\n"; }
    { std::ofstream(kit.Themes() / "broken" / "resources.xaml") << "<Oops />\n"; }
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
    CHECK(out.str().find("midnight") != std::string::npos);
    CHECK(out.str().find("midnight: usable, schemes: Base Dark") != std::string::npos);
    CHECK(out.str().find("broken: refused") != std::string::npos);
    std::filesystem::remove_all(kit.root);
}

TEST_CASE(Commands_SetWritesTheThemeAndTheScheme) {
    th::KitLayout kit = MakeKit("set");
    std::ostringstream out;
    std::ostringstream err;
    CHECK(th::RunThemeCommand(Args({"theme", "set", "midnight", "--scheme", "Dark"}), kit, out, err) == 0);

    ampify::Settings settings;
    settings.Load(kit.Settings().string());
    CHECK(settings.GetString("theme.active", "") == "midnight");
    CHECK(settings.GetString("theme.scheme", "") == "Dark");
    CHECK(out.str().find("Theme \"midnight\" chosen, scheme Dark") != std::string::npos);
    std::filesystem::remove_all(kit.root);
}

TEST_CASE(Commands_SetKeepsTheOtherSettings) {
    th::KitLayout kit = MakeKit("keep");
    ampify::Settings before;
    before.Set("extensions.atmos-leveler.enabled", "1");
    before.SetBool("theme.live_reload", false);
    before.SetBool("theme.parts.assets", false);
    before.Save(kit.Settings().string());
    { std::ofstream(kit.Themes() / "midnight" / "manifest.json") << R"({"defaultScheme": "Dark"})"; }

    std::ostringstream out;
    std::ostringstream err;
    CHECK(th::RunThemeCommand(Args({"theme", "set", "midnight"}), kit, out, err) == 0);

    ampify::Settings after;
    after.Load(kit.Settings().string());
    CHECK(after.GetString("extensions.atmos-leveler.enabled", "") == "1");
    CHECK(!after.GetBool("theme.live_reload", true));
    CHECK(!after.GetBool("theme.parts.assets", true));
    CHECK(after.GetString("theme.scheme", "x").empty());  // no --scheme: the theme's default, read on each pass
    CHECK(out.str().find("chosen, scheme Dark") != std::string::npos);
    std::filesystem::remove_all(kit.root);
}

TEST_CASE(Commands_SetNoneAndTheErrors) {
    th::KitLayout kit = MakeKit("none");
    std::ostringstream out;
    std::ostringstream err;

    CHECK(th::RunThemeCommand(Args({"theme", "set", "midnight"}), kit, out, err) == 0);
    CHECK(th::RunThemeCommand(Args({"theme", "set", "none"}), kit, out, err) == 0);
    ampify::Settings settings;
    settings.Load(kit.Settings().string());
    CHECK(th::ReadThemeSettings(settings).active.empty());

    // A missing theme, a refused theme, and a wrong command change nothing and report why.
    CHECK(th::RunThemeCommand(Args({"theme", "set", "absent"}), kit, out, err) == 2);
    CHECK(th::RunThemeCommand(Args({"theme", "set", "broken"}), kit, out, err) == 1);
    CHECK(err.str().find("refused: settings unchanged") != std::string::npos);
    CHECK(th::RunThemeCommand(Args({"theme", "frobnicate"}), kit, out, err) == 2);
    CHECK(th::RunThemeCommand(Args({"theme", "set", "midnight", "--bogus"}), kit, out, err) == 2);
    CHECK(th::RunThemeCommand(Args({"theme", "set", "midnight", "--scheme"}), kit, out, err) == 2);

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
    CHECK(out.str().find("Change detected in " + folder.filename().string()) != std::string::npos);

    // One check at the start, one after the change: two reports, the second one with the new rule.
    std::string text = out.str();
    size_t first = text.find("--- checked");
    CHECK(first != std::string::npos);
    CHECK(first != std::string::npos && text.find("--- checked", first + 1) != std::string::npos);
    CHECK(text.find("Page → Background = #FFFFFFFF") != std::string::npos);
    std::filesystem::remove_all(folder);
}

TEST_CASE(Commands_WatchRejectsBadUse) {
    std::ostringstream out;
    std::ostringstream err;
    auto nothing = [] {};
    CHECK(th::RunWatch(Args({"watch"}), out, err, nothing) == 2);
    CHECK(th::RunWatch(Args({"watch", "/no/such/folder/for/ampify"}), out, err, nothing) == 2);
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
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
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
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
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

    CHECK(!ProblemContaining(theme, "user.css line 5: \"color\" inside :root").empty());
    CHECK(!ProblemContaining(theme, "variable \"--absent\" is not defined").empty());
    CHECK(!ProblemContaining(theme, "variable \"--local\" outside :root").empty());

    std::string problem;
    CHECK(!th::SubstituteVariables("var(--x", {}, &problem).has_value());
    CHECK(problem.find("parenthesis") != std::string::npos);
    CHECK(!th::SubstituteVariables("var(x)", {}, &problem).has_value());
    CHECK(th::SubstituteVariables("var(--a, var(--b, 3))", {}).value_or("") == "3");
}

TEST_CASE(Engine_MediaRulesFollowTheWindowSize) {
    MemoryFiles files;
    files.files["user.css"] =
        "Grid { width: 10 }\n"
        "@media (max-width: 800px) { Grid { width: 20 } TextBlock { color: #FF00FF00 } }\n";
    th::Theme theme = Load(files);

    FakeTree tree;
    th::NodeId grid = tree.Add(-1, "Grid", "", {{"Width", {th::ValueKind::Length, "5"}}});
    th::NodeId text = tree.Add(0, "TextBlock", "", {{"Foreground", Brush("#FF111111")}});
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    tree.size = th::ViewSize{1000, 700};
    engine.Apply(theme, tree);
    CHECK(tree.Shown(grid, "Width").text == "10");
    CHECK(tree.Shown(text, "Foreground").text == "#FF111111");
    CHECK(tree.watched.size() == 1 && tree.watched[0].maxWidth == 800);

    tree.size = th::ViewSize{700, 700};
    engine.Apply(theme, tree);
    CHECK(tree.Shown(grid, "Width").text == "20");
    CHECK(tree.Shown(text, "Foreground").text == "#FF00FF00");

    // Wider again: the rule outside @media wins back, and the colour no rule gives any more goes back.
    tree.size = th::ViewSize{1000, 700};
    engine.Apply(theme, tree);
    CHECK(tree.Shown(grid, "Width").text == "10");
    CHECK(tree.Shown(text, "Foreground").text == "#FF111111");

    engine.Restore(tree);
    CHECK(tree.Shown(grid, "Width").text == "5");
}

TEST_CASE(Calc_ComputesLengthsFromVariables) {
    MemoryFiles files;
    files.files["user.css"] =
        ":root { --gap: 12; --double: calc(var(--gap) * 2); }\n"
        "Grid { margin: var(--gap) calc(var(--gap) * -1) var(--gap) var(--double); }\n"
        "Border { padding: calc(1px * 2px); }\n";
    th::Theme theme = Load(files);

    std::map<std::string, std::string> values;
    for (const th::Assignment& assignment : theme.assignments) values[assignment.selector.text + " " + assignment.property] = assignment.value.text;
    CHECK(values["Grid Margin"] == "24,12,-12,12");
    CHECK(!values.count("Border Padding"));
    CHECK(!ProblemContaining(theme, "user.css line 3: calc() multiplies two lengths").empty());

    CHECK(th::EvaluateCalc("4 calc(10 - 2 * 3) calc((1 + 2) * 3px)").value_or("") == "4 4 9px");
    CHECK(th::EvaluateCalc("CALC(7 / 2)").value_or("") == "3.5");
    CHECK(th::EvaluateCalc("calc(calc(0.1 + 0.2) * 10ms)").value_or("") == "3ms");
    CHECK(th::EvaluateCalc("#FF112233").value_or("") == "#FF112233");
    CHECK(th::EvaluateCalc("calc(-(1 + 2) * 2px) calc(+4)").value_or("") == "-6px 4");
    CHECK(!th::EvaluateCalc("calc(inf * 2)").has_value());

    std::string problem;
    CHECK(!th::EvaluateCalc("calc(1 / 0)", &problem).has_value());
    CHECK(problem == "calc() divides by zero");
    CHECK(!th::EvaluateCalc("calc(4px / 2px)", &problem).has_value());
    CHECK(!th::EvaluateCalc("calc(1px + 2ms)", &problem).has_value());
    CHECK(problem == "calc() adds ms to px");
    CHECK(!th::EvaluateCalc("calc(1 +)", &problem).has_value());
    CHECK(!th::EvaluateCalc("calc(2", &problem).has_value());
    CHECK(problem.find("parenthesis") != std::string::npos);
}

TEST_CASE(Fonts_RolesFromRootBlockAndDefaults) {
    MemoryFiles files;
    files.files["user.css"] =
        ":root {\n"
        "    --font-title: \"Segoe UI Variable Display\";\n"
        "    --font-title-weight: bold;\n"
        "    --font-title-size: 30px;\n"
        "    --font-text: Inter;\n"
        "    --font-body-weight: heavy;\n"
        "}\n"
        "TextBlock#Title { font-role: title; }\n"
        "TextBlock { font-family: var(--font-text); }\n"
        "Grid { font-role: nothing; }\n";
    th::Theme theme = Load(files);

    std::optional<th::FontStyle> title = th::ResolveFontRole(&theme, "title");
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
    CHECK(!ProblemContaining(theme, "font role \"nothing\" is unknown").empty());

    std::optional<ampify::Json> json = ampify::Json::Parse(th::FontRolesJson(&theme));
    CHECK(json.has_value());
    if (json) {
        const ampify::Json* role = json->Find("title");
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
    files.files["resources.xaml"] = "<ResourceDictionary xmlns=\"x\">\n  <!-- nothing -->\n</ResourceDictionary>";
    files.files["user.css"] = "Page { background: $Accent; }\nTextBlock { font-role: title; }\n";
    th::Theme snippet = LoadSnippetFrom(files, "s1", &theme);
    CHECK(snippet.usable);
    CHECK(snippet.resourceDictionary.empty());  // an empty dictionary is not merged
    CHECK(!ProblemContaining(snippet, "colors.ini ignored").empty());
    CHECK(!snippet.assignments.empty());
    if (!snippet.assignments.empty()) {
        CHECK(snippet.assignments[0].value.text == "#FFFF0000");  // the theme's colour
        CHECK(snippet.assignments[0].source == "snippets/s1/user.css");
        CHECK(snippet.assignments[1].value.text == "Bahnschrift");  // the theme's font role
    }

    // Without a theme, $names have nothing to read.
    th::Theme alone = LoadSnippetFrom(files, "s1", nullptr);
    CHECK(!ProblemContaining(alone, "is not in [colors]").empty());

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
    ampify::Settings settings;
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
    WriteFile(std::filesystem::path(folder) / "c" / "notes.txt", "not a snippet");
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
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);
    engine.Apply(composed, tree);
    CHECK(tree.elements[1].properties["Background"].text == "#FF222222");  // a snippet type beats a theme #name
    CHECK(tree.elements[0].properties["Background"].text == "#FF303030");  // b comes after a
    CHECK(log.str().find("(snippets/b/user.css line 1)") != std::string::npos);
}

TEST_CASE(Session_SnippetsComeAndGoWithoutTouchingTheTheme) {
    MemoryFiles themeFiles;
    themeFiles.files["user.css"] = "Page { background: #FF101010; }\n";
    auto theme = Shared(Load(themeFiles));

    MemoryFiles snippetFiles;
    snippetFiles.files["user.css"] = "Page { background: #FF999999; }\n#Hide { visibility: collapsed; }\nGrid { bad: 1; }\n";
    auto snippet = Shared(LoadSnippetFrom(snippetFiles, "calm", theme.get()));

    FakeTree tree = MakePage();
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);

    th::SessionRequest request;
    request.themeId = "test";
    request.theme = theme;
    request.snippets = {snippet};
    request.generation = 1;
    session.Tick(tree, request);
    CHECK(tree.elements[0].properties["Background"].text == "#FF999999");
    CHECK(tree.elements[4].properties["Visibility"].text == "Collapsed");
    CHECK(log.str().find("Snippet \"calm\" loaded") != std::string::npos);
    CHECK(log.str().find("Snippet \"calm\": snippets/calm/user.css line 3: property \"bad\" is unknown") !=
          std::string::npos);
    CHECK(session.Applied() && session.Applied()->name == "test + calm");

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
    th::Schedule schedule = th::ParseSchedule("19:00=Dark, 7:00=Light; bad, 25:00=X, 12:30=, 07:00=Day");
    CHECK(schedule.entries.size() == 2);
    CHECK(schedule.problems.size() == 4);  // bad, 25:00, 12:30= and 07:00 given twice
    if (schedule.entries.size() == 2) {
        CHECK(schedule.entries[0].minute == 7 * 60);
        CHECK(schedule.entries[0].scheme == "Day");  // the last 07:00 counts
    }
    CHECK(th::ScheduledScheme(schedule, 6 * 60 + 59) == "Dark");  // the evening entry still holds before 07:00
    CHECK(th::ScheduledScheme(schedule, 7 * 60) == "Day");
    CHECK(th::ScheduledScheme(schedule, 18 * 60 + 59) == "Day");
    CHECK(th::ScheduledScheme(schedule, 23 * 60 + 59) == "Dark");
    CHECK(th::ScheduledScheme(th::ParseSchedule(""), 600).empty());
}

TEST_CASE(Scheme_SystemModePicksByName) {
    CHECK(th::SystemScheme({"Base", "Dark"}, true) == "Dark");
    CHECK(th::IsDarkScheme("Dark") && th::IsDarkScheme("Night") && th::IsDarkScheme("Midnight dark"));
    CHECK(!th::IsDarkScheme("Base") && !th::IsDarkScheme("Light") && !th::IsDarkScheme(""));
    CHECK(th::SystemScheme({"Base", "Dark"}, false) == "Base");
    CHECK(th::SystemScheme({"Dark", "Light"}, false) == "Light");
    CHECK(th::SystemScheme({"Dark", "Light"}, true) == "Dark");
    CHECK(th::SystemScheme({"MidnightDark", "DayLight"}, false) == "DayLight");
    CHECK(th::SystemScheme({"Night", "Day"}, false) == "Day");
    CHECK(th::SystemScheme({"Only"}, true).empty());
    CHECK(th::SystemScheme({}, false).empty());
}

TEST_CASE(Scheme_ChoosePrefersTheScheduleThenAutoThenTheName) {
    std::vector<std::string> schemes{"Light", "Dark"};
    th::SchemeChoice choice = th::ChooseScheme("auto", "", schemes, {true, 600});
    CHECK(choice.scheme == "Dark");
    CHECK(choice.reason == "Windows dark mode");

    choice = th::ChooseScheme("Dark", "07:00=Light,19:00=Dark", schemes, {true, 12 * 60});
    CHECK(choice.scheme == "Light");
    CHECK(choice.reason == "schedule 07:00");

    choice = th::ChooseScheme("", "07:00=auto,22:00=Dark", schemes, {false, 9 * 60});
    CHECK(choice.scheme == "Light");
    CHECK(choice.reason == "schedule 07:00, Windows light mode");

    choice = th::ChooseScheme("dark", "", schemes, {false, 0});
    CHECK(choice.scheme == "Dark");  // the name as colors.ini writes it

    choice = th::ChooseScheme("auto", "", {"Only"}, {true, 0});
    CHECK(choice.scheme.empty());
    CHECK(!choice.problems.empty());

    choice = th::ChooseScheme("", "08:00=Sepia", schemes, {false, 9 * 60});
    CHECK(choice.scheme == "Sepia");
    CHECK(!choice.problems.empty());
}

TEST_CASE(Scheme_TheThemesDefaultComesAfterTheSettings) {
    std::vector<std::string> schemes{"Base", "Dark"};
    th::SchemeChoice choice = th::ChooseScheme("", "", schemes, {false, 600}, "Dark");
    CHECK(choice.scheme == "Dark");
    CHECK(choice.reason == "the theme's default");

    CHECK(th::ChooseScheme("Base", "", schemes, {false, 600}, "Dark").scheme == "Base");
    CHECK(th::ChooseScheme("", "07:00=Base", schemes, {false, 600}, "Dark").scheme == "Base");

    choice = th::ChooseScheme("", "", schemes, {false, 600});
    CHECK(choice.scheme.empty());
    CHECK(choice.reason == "first scheme");
}

TEST_CASE(Worker_StartsOnTheSchemeTheManifestNames) {
    std::filesystem::path kit(TempFolder("worker-default"));
    WriteFile(kit / "themes" / "midnight" / "colors.ini", "[Base]\nA = FFFFFF\n\n[Dark]\nA = 000000\n");
    WriteFile(kit / "themes" / "midnight" / "manifest.json", R"({"id": "midnight", "defaultScheme": "Dark"})");

    ampify::Settings settings;
    settings.Set("theme.active", "midnight");
    th::ThemeWorker worker(kit);
    th::SessionRequest request = worker.Tick(settings, 600);
    CHECK(request.theme && request.theme->scheme == "Dark");
    CHECK(request.schemeReason == "the theme's default");
    std::filesystem::remove_all(kit);
}

TEST_CASE(Settings_ScheduleIsKeptAndOnlyWrittenWhenSet) {
    ampify::Settings settings;
    th::ThemeSettings value;
    value.schedule = "07:00=Light,19:00=Dark";
    th::WriteThemeSettings(settings, value);
    CHECK(th::ReadThemeSettings(settings).schedule == "07:00=Light,19:00=Dark");
    th::WriteThemeSettings(settings, th::ThemeSettings{});
    CHECK(!settings.Get("theme.schedule").has_value());

    std::string root = TempFolder("schedule-kept");
    std::filesystem::path kit(root);
    WriteFile(kit / "themes" / "midnight" / "colors.ini", "[Light]\nA = FFFFFF\n[Dark]\nA = 000000\n");
    WriteFile(kit / "settings.txt", "theme.schedule = 07:00=Light,19:00=Dark\n");
    std::ostringstream out, err;
    CHECK(th::RunThemeCommand({"theme", "set", "midnight"}, th::KitLayout{kit}, out, err) == 0);
    ampify::Settings after;
    after.Load((kit / "settings.txt").string());
    CHECK(after.GetString("theme.schedule", "") == "07:00=Light,19:00=Dark");
}

TEST_CASE(Worker_ReloadsOnlyWhenSomethingChanges) {
    std::filesystem::path kit(TempFolder("worker"));
    WriteFile(kit / "themes" / "midnight" / "colors.ini", "[Light]\nA = FFFFFF\n\n[Dark]\nA = 000000\n");
    WriteFile(kit / "themes" / "midnight" / "user.css", "Page { background: $A; }\n");
    WriteFile(kit / "snippets" / "calm" / "user.css", "Page { opacity: 0.5; }\n");

    ampify::Settings settings;
    settings.Set("theme.active", "midnight");
    settings.Set("theme.scheme", "auto");

    th::ThemeWorker worker(kit);
    worker.SetSystemDark(true);
    th::SessionRequest first = worker.Tick(settings, 600);
    CHECK(first.theme && first.theme->scheme == "Dark");
    CHECK(first.schemeReason == "Windows dark mode");
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
    CHECK(evening.schemeReason == "schedule 19:00");

    settings.Set("snippets.calm.enabled", "1");
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
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeSession session(journal);
    th::SessionRequest request;
    request.layout = layout;
    request.generation = 1;
    session.Tick(tree, request);
    CHECK((tree.Names() == std::vector<std::string>{"Radio", "Home", "Browse"}));
    CHECK(tree.items[2].visibility.text == "Collapsed");
    CHECK(log.str().find("(sidebar.json line 3)") != std::string::npos);

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
    std::optional<ampify::Json> json = ampify::Json::Parse(text, &error);
    CHECK(json.has_value());
    if (!json) {
        ampify::test::ReportFailure(__FILE__, __LINE__, "inspector.json: " + error);
        return;
    }
    CHECK(!*json->Find("truncated")->AsBool());
    const ampify::Json::Array& resources = *json->Find("resources")->AsArray();
    CHECK(resources.size() == 1 && *resources[0].Find("key")->AsString() == "AccentFillColorDefaultBrush");
    const ampify::Json::Array& nodes = *json->Find("nodes")->AsArray();
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
    std::optional<ampify::Json> cut = ampify::Json::Parse(th::InspectTreeJson(tree, small));
    CHECK(cut && *cut->Find("truncated")->AsBool() && cut->Find("nodes")->AsArray()->size() == 2);
}

TEST_CASE(Examples_AllThemesAndSnippetsLoadWithoutProblems) {
    const std::filesystem::path source(AMPIFY_SOURCE_DIR);
    std::vector<std::string> themes = th::ListThemeFolders(source / "themes");
    CHECK(themes.size() >= 9);
    std::shared_ptr<const th::Theme> base;
    for (const std::string& name : themes) {
        std::filesystem::path folder = source / "themes" / name;
        th::FolderFiles files(folder);
        th::Theme theme = th::LoadTheme(name, folder.generic_string(), files);
        CHECK(theme.usable);
        for (const std::string& problem : theme.problems) {
            ampify::test::ReportFailure(__FILE__, __LINE__, name + " : " + problem);
        }
        if (name == "midnight") base = Shared(std::move(theme));
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
                ampify::test::ReportFailure(__FILE__, __LINE__, "snippet " + name + " : " + problem);
            }
        }

        std::optional<std::string> manifest = files.Read("manifest.json");
        CHECK(manifest.has_value());
        ampify::ManifestResult result =
            ampify::ParseManifest(manifest.value_or(""), name, 1, [&](const std::string& file) { return files.Exists(file); });
        CHECK(result.manifest.has_value() && result.manifest->kind == ampify::ElementKind::Snippet);
        for (const std::string& error : result.errors) {
            ampify::test::ReportFailure(__FILE__, __LINE__, "snippet " + name + " manifest: " + error);
        }
    }
}

// --- Full redesign: gradients, acrylic, layout, transforms, shadows, fonts, :hover ------------------------

TEST_CASE(Properties_GradientsImagesAndAcrylic) {
    th::Palette palette;
    palette.Define("Accent", th::Argb{0xFF, 0xFA, 0x2D, 0x48});
    MemoryFiles files;
    files.files["images/bg.png"] = "PNG";

    // The points are relative to the element; the line reaches the corners, as CSS draws it.
    CHECK(Converted("background", "linear-gradient(90deg, #000000, #FFFFFF)", palette, files) ==
          "0,0.5,1,0.5;#FF000000@0;#FFFFFFFF@1");
    CHECK(Converted("background", "linear-gradient(to bottom, #000 0%, $accent 50%, #FFF)", palette, files) ==
          "0.5,0,0.5,1;#FF000000@0;#FFFA2D48@0.5;#FFFFFFFF@1");
    CHECK(Converted("background", "linear-gradient(135deg, #000, #111, #222, #333)", palette, files) ==
          "0,0,1,1;#FF000000@0;#FF111111@0.3333;#FF222222@0.6667;#FF333333@1");
    CHECK(Converted("background", "linear-gradient(#000, transparent)", palette, files) ==
          "0.5,0,0.5,1;#FF000000@0;#00FFFFFF@1");
    CHECK(Converted("color", "linear-gradient(to top right, #000, #FFF)", palette, files) ==
          "0,1,1,0;#FF000000@0;#FFFFFFFF@1");
    CHECK(Converted("background", "linear-gradient(45deg, #000 60%, #FFF 20%)", palette, files).ends_with("@0.6;#FFFFFFFF@0.6"));
    CHECK(Converted("background", "linear-gradient(90deg, #000)", palette, files).find("two colors") != std::string::npos);
    CHECK(Converted("background", "linear-gradient(1turn, #000, #FFF)", palette, files).find("angle") != std::string::npos);
    CHECK(Converted("background", "linear-gradient(#000 150%, #FFF)", palette, files).find("position") != std::string::npos);
    CHECK(Converted("background", "linear-gradient(#000, $Missing)", palette, files).find("is not in [colors]") != std::string::npos);
    CHECK(Converted("background", "linear-gradient(to middle, #000, #FFF)", palette, files).rfind("PROBLEM", 0) == 0);

    CHECK(Converted("background", "url(images/bg.png) cover", palette, files) == "C:/themes/test/images/bg.png|UniformToFill");
    CHECK(Converted("background", "url(images/bg.png) contain", palette, files) == "C:/themes/test/images/bg.png|Uniform");
    CHECK(Converted("background", "url('images/bg.png') FILL", palette, files) == "C:/themes/test/images/bg.png|Fill");
    CHECK(Converted("background", "url(images/bg.png) tile", palette, files).find("image fit") != std::string::npos);
    CHECK(Converted("background", "url(../bg.png) cover", palette, files).find("not allowed") != std::string::npos);

    CHECK(th::FindProperty("backdrop")->xaml == "Background");
    CHECK(Converted("backdrop", "acrylic(#1C1C1E, 0.6)", palette, files) == "#FF1C1C1E;0.6");
    CHECK(Converted("backdrop", "acrylic($accent, 1)", palette, files) == "#FFFA2D48;1");
    CHECK(Converted("backdrop", "acrylic(#1C1C1E, 1.5)", palette, files).find("outside 0 to 1") != std::string::npos);
    CHECK(Converted("backdrop", "acrylic(#1C1C1E)", palette, files).find("two values") != std::string::npos);
    CHECK(Converted("backdrop", "#1C1C1E", palette, files).find("acrylic(") != std::string::npos);
}

TEST_CASE(Properties_LayoutTransformsShadowsAndFonts) {
    th::Palette palette;
    MemoryFiles files;
    files.files["fonts/Inter.ttf"] = "TTF";
    files.files["fonts/Inter.woff"] = "WOFF";

    CHECK(Converted("horizontal-alignment", "center", palette, files) == "Center");
    CHECK(Converted("horizontal-alignment", "Right", palette, files) == "Right");
    CHECK(Converted("vertical-alignment", "bottom", palette, files) == "Bottom");
    CHECK(Converted("vertical-alignment", "stretch", palette, files) == "Stretch");
    CHECK(Converted("vertical-alignment", "left", palette, files).find("top, center, bottom") != std::string::npos);

    CHECK(th::FindProperty("grid-row")->xaml == "Grid.Row");
    CHECK(th::FindProperty("z-index")->xaml == "Canvas.ZIndex");
    CHECK(Converted("grid-row", "2", palette, files) == "2");
    CHECK(Converted("grid-column", "0", palette, files) == "0");
    CHECK(Converted("grid-row", "-1", palette, files).find("starts at 0") != std::string::npos);
    CHECK(Converted("grid-column-span", "0", palette, files).find("starts at 1") != std::string::npos);
    CHECK(Converted("grid-row-span", "3", palette, files) == "3");
    CHECK(Converted("z-index", "-5", palette, files) == "-5");
    CHECK(Converted("z-index", "1.5", palette, files).find("whole number") != std::string::npos);

    // CompositeTransform: scaleX, scaleY, rotation, translateX, translateY.
    CHECK(Converted("transform", "scale(1.05)", palette, files) == "1.05,1.05,0,0,0");
    CHECK(Converted("transform", "scale(2, 3) rotate(-5deg) translate(10px, 4)", palette, files) == "2,3,-5,10,4");
    CHECK(Converted("transform", "translateY(8px) scaleX(2)", palette, files) == "2,1,0,0,8");
    CHECK(Converted("transform", "rotate(10deg) rotate(5deg)", palette, files) == "1,1,15,0,0");
    CHECK(Converted("transform", "none", palette, files) == "1,1,0,0,0");
    CHECK(Converted("transform", "skew(4deg)", palette, files).find("unknown") != std::string::npos);
    CHECK(Converted("transform", "rotate(5)", palette, files).find("degrees") != std::string::npos);
    CHECK(Converted("transform", "scale(1.05", palette, files).rfind("PROBLEM", 0) == 0);

    CHECK(Converted("transform-origin", "50% 50%", palette, files) == "0.5,0.5");
    CHECK(Converted("transform-origin", "center", palette, files) == "0.5,0.5");
    CHECK(Converted("transform-origin", "top left", palette, files) == "0,0");
    CHECK(Converted("transform-origin", "right", palette, files) == "1,0.5");
    CHECK(Converted("transform-origin", "bottom", palette, files) == "0.5,1");
    CHECK(Converted("transform-origin", "25% 100%", palette, files) == "0.25,1");
    CHECK(Converted("transform-origin", "10px 5px", palette, files).find("origin") != std::string::npos);
    CHECK(Converted("transform-origin", "left right", palette, files).rfind("PROBLEM", 0) == 0);

    // transition: the animated properties and their duration in milliseconds, transform first, then opacity, then
    // background; the easing words are accepted and ignored.
    CHECK(Converted("transition", "transform 200ms", palette, files) == "transform:200");
    CHECK(Converted("transition", "opacity .15s ease, background 300ms", palette, files) == "opacity:150;background:300");
    CHECK(Converted("transition", "all 250ms ease-out", palette, files) == "transform:250;opacity:250;background:250");
    CHECK(Converted("transition", "background 0.2s, transform 120ms linear", palette, files) ==
          "transform:120;background:200");
    CHECK(Converted("transition", "none", palette, files).empty());
    CHECK(Converted("transition", "color 200ms", palette, files).find("transform, opacity, background") !=
          std::string::npos);
    CHECK(Converted("transition", "transform", palette, files).find("duration") != std::string::npos);
    CHECK(Converted("transition", "opacity 9s", palette, files).find("5 s") != std::string::npos);

    CHECK(Converted("box-shadow", "16px", palette, files) == "16");
    CHECK(Converted("box-shadow", "none", palette, files) == "0");
    CHECK(Converted("box-shadow", "200", palette, files).find("0 to 100") != std::string::npos);
    CHECK(Converted("box-shadow", "0 4px 8px #000", palette, files).find("depth") != std::string::npos);

    // Four corners, in CSS order (top-left, top-right, bottom-right, bottom-left), kept as bottom-left first.
    CHECK(Converted("corner-radius", "8 8 0 0", palette, files) == "0,8,8,0");

    CHECK(Converted("font-family", "url(fonts/Inter.ttf#Inter Display)", palette, files) ==
          "file:///C:/themes/test/fonts/Inter.ttf#Inter Display");
    CHECK(Converted("font-family", "url(\"fonts/Inter.ttf#Inter\")", palette, files) ==
          "file:///C:/themes/test/fonts/Inter.ttf#Inter");
    CHECK(Converted("font-family", "url(fonts/Inter.ttf)", palette, files).find("family name") != std::string::npos);
    CHECK(Converted("font-family", "url(fonts/Inter.woff#Inter)", palette, files).find("ttf, otf or ttc") != std::string::npos);
    CHECK(Converted("font-family", "url(../Inter.ttf#Inter)", palette, files).find("not allowed") != std::string::npos);
    CHECK(Converted("font-family", "url(fonts/Absent.otf#Absent)", palette, files).find("not found") != std::string::npos);

    th::ValueContext noAssets{palette, files, "C:/themes/test", false};
    th::Conversion refused = th::ConvertValue(*th::FindProperty("font-family"), "url(fonts/Inter.ttf#Inter)", noAssets);
    CHECK(!refused.value && refused.problem.find("fonts are turned off") != std::string::npos);

    // A font role can point at a font of the theme too.
    files.files["user.css"] = ":root { --font-title: url(fonts/Inter.ttf#Inter); }\nTextBlock { font-role: title; }\n";
    th::Theme theme = Load(files);
    CHECK(theme.problems.empty());
    CHECK(theme.fonts["title"].family == "file:///C:/themes/test/fonts/Inter.ttf#Inter");
}

TEST_CASE(Css_HoverOnTheLastPartOnly) {
    std::string problem;
    std::optional<th::Selector> hover = th::ParseSelector("Grid#TransportBar Button:hover", &problem);
    CHECK(hover.has_value());
    if (!hover) return;
    CHECK(hover->trigger == th::Trigger::Hover);
    CHECK(hover->parts.size() == 2);
    CHECK(hover->parts[1].type == "Button");
    CHECK(hover->text == "Grid#TransportBar Button:hover");
    std::optional<th::Selector> named = th::ParseSelector("#Play:HOVER", &problem);
    CHECK(named.has_value() && named->trigger == th::Trigger::Hover && named->parts[0].name == "Play");
    CHECK(th::ParseSelector("Button", &problem)->trigger == th::Trigger::Always);
    // Matching is structural: the state is the backend's business.
    CHECK(th::SpecificityOf(*hover) == th::SpecificityOf(Sel("Grid#TransportBar Button")));

    MemoryFiles files;
    files.files["user.css"] = "Button, Button:hover { order: 1; opacity: 0.5; }\n";
    th::Theme theme = Load(files);
    CHECK(!ProblemContaining(theme, "not possible under :hover").empty());
    CHECK(theme.assignments.size() == 3);  // order and opacity for Button, opacity only for Button:hover
}

TEST_CASE(Css_ActiveAndFocusOnTheLastPartOnly) {
    std::string problem;
    std::optional<th::Selector> active = th::ParseSelector("Grid#TransportBar Button:active", &problem);
    CHECK(active.has_value() && active->trigger == th::Trigger::Active && active->parts.size() == 2);
    std::optional<th::Selector> focus = th::ParseSelector("AutoSuggestBox:focus", &problem);
    CHECK(focus.has_value() && focus->trigger == th::Trigger::Focus);
    std::optional<th::Selector> within = th::ParseSelector("AutoSuggestBox:FOCUS-WITHIN", &problem);
    CHECK(within.has_value() && within->trigger == th::Trigger::Focus);
    std::optional<th::Selector> counted = th::ParseSelector("Button:active:nth-child(2)", &problem);
    CHECK(counted.has_value() && counted->trigger == th::Trigger::Active && counted->parts[0].nth == 2);
    CHECK(!th::ParseSelector("Button:active > Grid", &problem));
    CHECK(problem.find("last element") != std::string::npos);
    // A structural pseudo-class after the state does not make the part the last one.
    CHECK(!th::ParseSelector("Button:hover:first-child Grid", &problem));
    CHECK(problem.find("last element") != std::string::npos);
    CHECK(!th::ParseSelector("Button:focus:nth-child(2) > Grid", &problem));
    CHECK(problem.find("last element") != std::string::npos);
    CHECK(!th::ParseSelector("Button:hover:active", &problem));
    CHECK(problem.find("only one of") != std::string::npos);

    th::Stylesheet sheet = th::ParseStylesheet(".play-button:active { opacity: 0.5 }\n");
    CHECK(sheet.rules.size() == 1 && sheet.rules[0].selectors.size() == 1);
    if (sheet.rules.size() == 1 && !sheet.rules[0].selectors.empty()) {
        CHECK(sheet.rules[0].selectors[0].trigger == th::Trigger::Active);
        CHECK(sheet.rules[0].selectors[0].parts.back().type == "AppBarButton");
    }

    MemoryFiles files;
    files.files["user.css"] = "Button:active { order: 1; opacity: 0.5; }\n";
    th::Theme theme = Load(files);
    CHECK(!ProblemContaining(theme, "not possible under :active").empty());
}

namespace {

// Page > Button, with what a real button would have.
FakeTree MakeButtonPage() {
    FakeTree tree;
    tree.Add(-1, "Page", "", {{"Background", Brush("#FF000000")}});
    tree.Add(0, "Button", "Play",
             {{"Background", Brush("#FF000000")},
              {"Opacity", {th::ValueKind::Number, "1"}},
              {"RenderTransform", {th::ValueKind::Opaque, "handle:1"}},
              {"Shadow", {th::ValueKind::Opaque, "handle:2"}}});
    return tree;
}

}  // namespace

TEST_CASE(Engine_HoverShowsUnderThePointerAndRestores) {
    MemoryFiles files;
    files.files["user.css"] =
        "Button { background: #FF111111; }\n"
        "Button:hover { background: #FF222222; transform: scale(1.05); box-shadow: 16px; }\n"
        "#Play:hover { opacity: 0.5; }\n"
        "Button:hover { font-size: 20; }\n";  // a Button of this tree has no FontSize: refused
    th::Theme theme = Load(files);
    CHECK(theme.problems.empty());

    FakeTree tree = MakeButtonPage();
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report first = engine.Apply(theme, tree);
    CHECK(first.applied == 1 + 4);  // the background, then the four hover values
    CHECK(first.refused == 1);
    CHECK(engine.OriginalCount() == 2);  // the background, and one hover to take off
    CHECK(tree.handlersAttached == 1);
    CHECK(tree.Shown(2, "Background").text == "#FF111111");
    CHECK(tree.Shown(2, "RenderTransform").text == "handle:1");
    CHECK(log.str().find("Hover: Button#Play") != std::string::npos);

    tree.PointerEnter(2);
    CHECK(tree.Shown(2, "Background").text == "#FF222222");
    CHECK(tree.Shown(2, "RenderTransform").text == "1.05,1.05,0,0,0");
    CHECK(tree.Shown(2, "Shadow").text == "16");
    CHECK(tree.Shown(2, "Opacity").text == "0.5");

    // The next tick, under the pointer: the engine sees its own values and leaves the hover alone.
    th::ThemeEngine::Report second = engine.Apply(theme, tree);
    CHECK(second.applied == 0);
    CHECK(tree.handlersAttached == 1);
    CHECK(tree.Shown(2, "Background").text == "#FF222222");

    tree.PointerExit(2);
    CHECK(tree.Shown(2, "Background").text == "#FF111111");
    CHECK(tree.Shown(2, "RenderTransform").text == "handle:1");
    CHECK(tree.Shown(2, "Shadow").text == "handle:2");
    CHECK(tree.Shown(2, "Opacity").text == "1");

    // Taking the theme off under the pointer brings back the originals and removes the handlers.
    tree.PointerEnter(2);
    engine.Restore(tree);
    CHECK(engine.OriginalCount() == 0);
    CHECK(tree.handlersRemoved == 1);
    CHECK(tree.followed.empty());
    CHECK(tree.Shown(2, "Background").text == "#FF000000");
    CHECK(tree.Shown(2, "RenderTransform").text == "handle:1");
    CHECK(tree.Shown(2, "Opacity").text == "1");
    tree.PointerEnter(2);  // nothing follows the pointer any more
    CHECK(tree.Shown(2, "Opacity").text == "1");
}

TEST_CASE(Engine_ActiveAndFocusShowInTheirStates) {
    MemoryFiles files;
    files.files["user.css"] =
        "Button:hover { opacity: 0.8; }\n"
        "Button:active { transform: scale(0.95); opacity: 0.6; }\n"
        "#Play:focus { box-shadow: 16px; }\n";
    th::Theme theme = Load(files);
    CHECK(theme.problems.empty());

    FakeTree tree = MakeButtonPage();
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);
    th::ThemeEngine::Report first = engine.Apply(theme, tree);
    CHECK(first.applied == 4);
    CHECK(first.refused == 0);
    CHECK(tree.handlersAttached == 1);
    CHECK(log.str().find("Opacity = 0.6 (:active)") != std::string::npos);

    tree.PointerEnter(2);
    CHECK(tree.Shown(2, "Opacity").text == "0.8");
    tree.Press(2, true);
    CHECK(tree.Shown(2, "Opacity").text == "0.6");  // pressed beats the hover
    CHECK(tree.Shown(2, "RenderTransform").text == "0.95,0.95,0,0,0");
    tree.Press(2, false);
    CHECK(tree.Shown(2, "Opacity").text == "0.8");
    CHECK(tree.Shown(2, "RenderTransform").text == "handle:1");

    tree.Focus(2, true);
    CHECK(tree.Shown(2, "Shadow").text == "16");
    tree.PointerExit(2);
    CHECK(tree.Shown(2, "Opacity").text == "1");
    CHECK(tree.Shown(2, "Shadow").text == "16");  // the focus stays when the pointer leaves
    tree.Focus(2, false);
    CHECK(tree.Shown(2, "Shadow").text == "handle:2");
}

TEST_CASE(Hover_PressAndFocusShowOverTheHoverInOrder) {
    struct Element final : th::HoverTracker::Access {
        std::map<std::string, th::Value, std::less<>> values{{"Background", Brush("#FF111111")}};
        std::optional<th::Value> ReadRaw(th::NodeId, std::string_view property) override {
            auto it = values.find(property);
            if (it == values.end()) return std::nullopt;
            return it->second;
        }
        bool WriteRaw(th::NodeId, std::string_view property, const th::Value& value) override {
            values[std::string(property)] = value;
            writes++;
            return true;
        }
        int writes = 0;
    } element;
    th::HoverTracker states;
    states.Set(7,
               {{"Background", Brush("#FF222222")},
                {"Background", Brush("#FF333333"), th::Trigger::Focus},
                {"Background", Brush("#FF444444"), th::Trigger::Active}},
               element);
    CHECK(states.Uses(7, th::Trigger::Hover) && states.Uses(7, th::Trigger::Focus) && states.Uses(7, th::Trigger::Active));
    CHECK(!states.Uses(8, th::Trigger::Hover));

    states.Enter(7, element);
    CHECK(element.values["Background"] == Brush("#FF222222"));
    states.Toggle(7, th::Trigger::Focus, true, element);
    CHECK(element.values["Background"] == Brush("#FF333333"));  // the focus beats the hover
    states.Toggle(7, th::Trigger::Active, true, element);
    CHECK(element.values["Background"] == Brush("#FF444444"));  // pressed beats both
    int writes = element.writes;
    states.Toggle(7, th::Trigger::Active, true, element);
    CHECK(element.writes == writes);  // already pressed: nothing written

    // The engine writes underneath while the element is pressed, and reads back what it wrote.
    CHECK(states.Write(7, "Background", Brush("#FF555555"), element));
    CHECK(states.Read(7, "Background", element) == Brush("#FF555555"));
    CHECK(element.values["Background"] == Brush("#FF444444"));

    states.Toggle(7, th::Trigger::Active, false, element);
    CHECK(element.values["Background"] == Brush("#FF333333"));
    states.Exit(7, element);
    CHECK(element.values["Background"] == Brush("#FF333333"));  // still focused
    states.Toggle(7, th::Trigger::Focus, false, element);
    CHECK(element.values["Background"] == Brush("#FF555555"));
    CHECK(states.ValueOf(7, "Background", th::Trigger::Active) == Brush("#FF444444"));
    CHECK(states.ValueOf(7, "Background") == Brush("#FF222222"));
}

TEST_CASE(Engine_HoverFollowsThemeChangesWithoutLeaking) {
    MemoryFiles withHover;
    withHover.files["user.css"] = "Button { background: #FF111111; }\nButton:hover { opacity: 0.5; }\n";
    MemoryFiles otherHover;
    otherHover.files["user.css"] = "Button { background: #FF333333; }\nButton:hover { opacity: 0.8; background: #FF444444; }\n";
    MemoryFiles noHover;
    noHover.files["user.css"] = "Button { background: #FF111111; }\n";

    FakeTree tree = MakeButtonPage();
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    engine.Apply(Load(withHover), tree);
    tree.PointerEnter(2);
    CHECK(tree.Shown(2, "Opacity").text == "0.5");

    // Edited live under the pointer: the new hover values show at once, the new base waits underneath.
    engine.Apply(Load(otherHover), tree);
    CHECK(tree.Shown(2, "Opacity").text == "0.8");
    CHECK(tree.Shown(2, "Background").text == "#FF444444");
    CHECK(tree.handlersAttached == 1);
    tree.PointerExit(2);
    CHECK(tree.Shown(2, "Background").text == "#FF333333");
    CHECK(tree.Shown(2, "Opacity").text == "1");

    // The :hover rule is gone: the hover comes off and the handlers with it.
    engine.Apply(Load(noHover), tree);
    CHECK(tree.handlersRemoved == 1);
    CHECK(tree.followed.empty());
    CHECK(engine.OriginalCount() == 1);

    // An element that goes away takes its hover with it.
    engine.Apply(Load(withHover), tree);
    CHECK(tree.handlersAttached == 2);
    CHECK(engine.OriginalCount() == 2);
    tree.Remove(2);
    engine.Apply(Load(withHover), tree);
    CHECK(engine.OriginalCount() == 0);
    engine.Restore(tree);
}

TEST_CASE(Engine_GivesBackToAnElementTakenOutOfTheInterface) {
    MemoryFiles files;
    files.files["user.css"] = "Button { background: #FF111111; }\nButton:hover { opacity: 0.5; }\n";
    th::Theme theme = Load(files);
    FakeTree tree = MakeButtonPage();
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    engine.Apply(theme, tree);
    CHECK(engine.OriginalCount() == 2);  // the background, and the hover to take off

    // A menu closes: WinUI takes its entries out of the tree and keeps them for the next opening.
    tree.Detach(2);
    engine.Apply(theme, tree);
    CHECK(engine.OriginalCount() == 2);

    // Back in the tree, the theme's values are still in place.
    tree.Detach(2, false);
    th::ThemeEngine::Report again = engine.Apply(theme, tree);
    CHECK(again.applied == 0);

    // The theme comes off while the menu is closed: the entry gets its own values back all the same.
    tree.Detach(2);
    engine.Restore(tree);
    CHECK(tree.Shown(2, "Background").text == "#FF000000");
    CHECK(tree.handlersRemoved == 1);
    CHECK(engine.OriginalCount() == 0);
}

// The search box's buttons have a padding of 0,0,-2,0, which a theme template's Border refuses (WinUI then stops Apple
// Music): their own template comes back before that padding, when the theme comes off or stops giving them anything.
TEST_CASE(Engine_GivesATemplateBackBeforeTheOtherValues) {
    MemoryFiles files;
    files.files["templates/button.xaml"] = "<ControlTemplate />";
    files.files["user.css"] = "Button { padding: 8; template: url(templates/button.xaml); }\n";
    th::Theme theme = Load(files);
    MemoryFiles plainFiles;
    plainFiles.files["user.css"] = "Grid { opacity: 0.5; }\n";
    th::Theme plain = Load(plainFiles);
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });

    for (bool restore : {true, false}) {
        FakeTree tree;
        tree.Add(-1, "Button", "QueryButton",
                 {{"Padding", {th::ValueKind::Thickness, "0,0,-2,0"}}, {"Template", {th::ValueKind::Opaque, "handle:1"}}});
        th::ThemeEngine engine(journal);
        engine.Apply(theme, tree);
        CHECK(tree.Shown(1, "Template").kind == th::ValueKind::Template);

        tree.written.clear();
        if (restore) {
            engine.Restore(tree);
        } else {
            engine.Apply(plain, tree);
        }
        CHECK(tree.written == std::vector<std::string>({"Template", "Padding"}));
        CHECK(tree.Shown(1, "Padding").text == "0,0,-2,0");
    }
}

TEST_CASE(Hover_ValueOfGivesTheHoverValueWithOrWithoutThePointer) {
    struct Element final : th::HoverTracker::Access {
        std::map<std::string, th::Value, std::less<>> values{{"Background", Brush("#FF111111")}};
        std::optional<th::Value> ReadRaw(th::NodeId, std::string_view property) override {
            auto it = values.find(property);
            if (it == values.end()) return std::nullopt;
            return it->second;
        }
        bool WriteRaw(th::NodeId, std::string_view property, const th::Value& value) override {
            values[std::string(property)] = value;
            return true;
        }
    } element;
    th::HoverTracker hover;

    hover.Set(7, {{"Background", Brush("#FF222222")}}, element);
    CHECK(hover.ValueOf(7, "Background") == Brush("#FF222222"));  // before the pointer comes
    hover.Enter(7, element);
    CHECK(hover.ValueOf(7, "Background") == Brush("#FF222222"));
    CHECK(element.values["Background"] == Brush("#FF222222"));
    CHECK(!hover.ValueOf(7, "Foreground"));
    CHECK(!hover.ValueOf(8, "Background"));

    hover.Set(7, {}, element);
    CHECK(!hover.ValueOf(7, "Background"));
    CHECK(element.values["Background"] == Brush("#FF111111"));
}

TEST_CASE(Hover_ARefusedPropertyLeavesTheOthersAndItsStateClean) {
    struct Element final : th::HoverTracker::Access {
        std::map<std::string, th::Value, std::less<>> values{{"Background", Brush("#FF111111")},
                                                             {"Foreground", Brush("#FFAAAAAA")}};
        bool refuseBackground = false;
        std::optional<th::Value> ReadRaw(th::NodeId, std::string_view property) override {
            auto it = values.find(property);
            if (it == values.end()) return std::nullopt;
            return it->second;
        }
        bool WriteRaw(th::NodeId, std::string_view property, const th::Value& value) override {
            if (refuseBackground && property == "Background") throw std::runtime_error("refused");
            values[std::string(property)] = value;
            return true;
        }
    } element;
    th::HoverTracker hover;
    hover.Set(7, {{"Background", Brush("#FF222222")}, {"Foreground", Brush("#FFBBBBBB")}}, element);
    hover.Enter(7, element);
    CHECK(element.values["Foreground"] == Brush("#FFBBBBBB"));

    // The template changed under the pointer: Background is refused, Foreground still comes back.
    element.refuseBackground = true;
    hover.Exit(7, element);
    CHECK(element.values["Foreground"] == Brush("#FFAAAAAA"));

    // Nothing is left shown: the next visit shows both values again once the element takes them.
    element.refuseBackground = false;
    hover.Enter(7, element);
    CHECK(element.values["Background"] == Brush("#FF222222"));
    CHECK(element.values["Foreground"] == Brush("#FFBBBBBB"));
}

TEST_CASE(Engine_NewPropertiesRestoreExactly) {
    MemoryFiles files;
    files.files["images/glass.png"] = "PNG";
    files.files["user.css"] =
        "Grid#Card {\n"
        "    background: linear-gradient(135deg, #1C1C1E, #2C2C2E);\n"
        "    border-color: url(images/glass.png) cover;\n"
        "    corner-radius: 16 16 4 4;\n"
        "    horizontal-alignment: center;\n"
        "    vertical-alignment: top;\n"
        "    grid-row: 1;\n"
        "    grid-column-span: 2;\n"
        "    z-index: 3;\n"
        "    transform: rotate(2deg);\n"
        "    transform-origin: center;\n"
        "    box-shadow: 24px;\n"
        "}\n"
        "Border { backdrop: acrylic(#202020, 0.7); }\n";
    th::Theme theme = Load(files);
    CHECK(theme.problems.empty());

    FakeTree tree;
    tree.Add(-1, "Grid", "Card",
             {{"Background", {th::ValueKind::Opaque, "handle:7"}},
              {"BorderBrush", Brush("#FF000000")},
              {"CornerRadius", {th::ValueKind::Thickness, "0,0,0,0"}},
              {"HorizontalAlignment", {th::ValueKind::Keyword, "Stretch"}},
              {"VerticalAlignment", {th::ValueKind::Keyword, "Stretch"}},
              {"Grid.Row", {th::ValueKind::Number, "0"}},
              {"Grid.ColumnSpan", {th::ValueKind::Number, "1"}},
              {"Canvas.ZIndex", {th::ValueKind::Number, "0"}},
              {"RenderTransform", {th::ValueKind::Opaque, "handle:8"}},
              {"RenderTransformOrigin", {th::ValueKind::Point, "0,0"}},
              {"Shadow", {th::ValueKind::Opaque, "handle:9"}}});
    tree.Add(0, "Border", "", {{"Background", Brush("#FFFFFFFF")}});
    std::map<std::string, th::Value> before = tree.elements[0].properties;

    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);
    th::ThemeEngine::Report first = engine.Apply(theme, tree);
    CHECK(first.applied == 12);
    CHECK(first.refused == 0);
    std::map<std::string, th::Value>& card = tree.elements[0].properties;
    CHECK(card["Background"] == (th::Value{th::ValueKind::Gradient, "0,0,1,1;#FF1C1C1E@0;#FF2C2C2E@1"}));
    CHECK(card["BorderBrush"].text == "C:/themes/test/images/glass.png|UniformToFill");
    CHECK(card["CornerRadius"].text == "4,16,16,4");
    CHECK(card["HorizontalAlignment"].text == "Center");
    CHECK(card["Grid.ColumnSpan"].text == "2");
    CHECK(card["RenderTransform"] == (th::Value{th::ValueKind::Transform, "1,1,2,0,0"}));
    CHECK(card["RenderTransformOrigin"].text == "0.5,0.5");
    CHECK(card["Shadow"] == (th::Value{th::ValueKind::Shadow, "24"}));
    CHECK(tree.elements[1].properties["Background"] == (th::Value{th::ValueKind::Acrylic, "#FF202020;0.7"}));

    CHECK(engine.Apply(theme, tree).applied == 0);

    engine.Restore(tree);
    CHECK(tree.elements[0].properties == before);
    CHECK(tree.elements[1].properties["Background"].text == "#FFFFFFFF");
}

TEST_CASE(Theme_KeyframesGiveTracksToTheAnimationsThatNameThem) {
    MemoryFiles files;
    files.files["user.css"] =
        ":root { --turn: 360deg; }\n"
        "TextBlock { animation: pulse 2s infinite alternate; }\n"
        "@keyframes pulse {\n"
        "    from { opacity: 1; transform: scale(1) }\n"
        "    to { opacity: .6; transform: scale(1.05) rotate(var(--turn)) }\n"
        "}\n"
        "@keyframes glow { to { background: red } }\n"
        "Grid { animation: glow 1s }\n";
    th::Theme theme = Load(files);

    using Channel = th::AnimationTrack::Channel;
    auto pulse = theme.keyframes.find("pulse");
    CHECK(pulse != theme.keyframes.end());
    if (pulse != theme.keyframes.end()) {
        // No translation track: it stays at none in every stop.
        CHECK(pulse->second == (th::KeyframeTracks{{Channel::Opacity, {{0, 1, 0}, {1, 0.6, 0}}},
                                                   {Channel::Scale, {{0, 1, 1}, {1, 1.05, 1.05}}},
                                                   {Channel::Rotation, {{0, 0, 0}, {1, 360, 0}}}}));
    }
    CHECK(!ProblemContaining(theme, "\"background\" does not animate").empty());
    CHECK(!ProblemContaining(theme, "animates nothing").empty());  // glow
    CHECK(theme.assignments.size() == 1);
    if (theme.assignments.size() == 1) {
        CHECK(theme.assignments[0].property == "Animation");
        std::optional<std::vector<th::Animation>> animations = th::DecodeAnimations(theme.assignments[0].value.text);
        CHECK(animations && animations->size() == 1 && (*animations)[0].tracks == pulse->second);
    }

    // A snippet runs the keyframes of the theme it is applied on.
    MemoryFiles snippetFiles;
    snippetFiles.files["user.css"] = "Border { animation: pulse 1s }\n";
    th::Theme snippet = th::LoadSnippet("glow", "C:/snippets/glow", snippetFiles, &theme);
    CHECK(snippet.problems.empty());
    CHECK(snippet.assignments.size() == 1 && snippet.assignments[0].value.kind == th::ValueKind::Animation);
}

TEST_CASE(Engine_StartsAnAnimationOnceAndTakesItOffWithTheTheme) {
    MemoryFiles files;
    files.files["user.css"] =
        "#Cover { animation: spin 20s linear infinite; }\n"
        "@keyframes spin { to { transform: rotate(360deg) } }\n";
    th::Theme theme = Load(files);
    CHECK(theme.problems.empty());

    FakeTree tree;
    th::NodeId cover = tree.Add(-1, "Image", "Cover", {{"Animation", {th::ValueKind::Animation, ""}}});
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    th::ThemeEngine::Report report = engine.Apply(theme, tree);
    CHECK(report.applied == 1 && report.refused == 0);
    std::optional<std::vector<th::Animation>> running = th::DecodeAnimations(tree.Shown(cover, "Animation").text);
    CHECK(running && running->size() == 1 && (*running)[0].iterations == 0);
    CHECK(engine.Apply(theme, tree).applied == 0);  // the same animation is not started again
    engine.Restore(tree);
    CHECK(tree.Shown(cover, "Animation").text.empty());
}

TEST_CASE(Engine_SaysWhyAnElementRefusesAnAnimation) {
    MemoryFiles files;
    files.files["user.css"] = "#Play { animation: pop 1s }\n@keyframes pop { 50% { transform: scale(1.2) } }\n";
    th::Theme theme = Load(files);

    FakeTree tree;
    tree.Add(-1, "AppBarButton", "Play", {{"Animation", {th::ValueKind::Animation, ""}}});
    tree.refusedProperties.insert("Animation");
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("00:00:00.000"); });
    th::ThemeEngine engine(journal);

    CHECK(engine.Apply(theme, tree).refused == 1);
    CHECK(log.str().find("\"AppBarButton#Play\" refused the animation") != std::string::npos);
}
