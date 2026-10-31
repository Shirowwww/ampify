#include <string>

#include "../core/ui_model.hpp"
#include "test.hpp"

namespace {

ampify::Json Parse(const std::string& text) {
    std::string error;
    std::optional<ampify::Json> value = ampify::Json::Parse(text, &error);
    return value ? *value : ampify::Json();
}

// A stack nested `depth` levels deep, with one text at the bottom.
std::string Nested(int depth) {
    std::string text = R"({"type":"text","text":"fond"})";
    for (int i = 0; i < depth; i++) text = R"({"type":"stack","children":[)" + text + "]}";
    return text;
}

}  // namespace

TEST_CASE(UiTree_AcceptsStacksRowsAndControls) {
    std::string error;
    ampify::Json tree = Parse(R"({"type":"stack","children":[{"type":"text","text":"Paroles"},)"
                           R"({"type":"row","children":[{"type":"button","label":"OK"},{"type":"toggle","label":"x"}]}]})");
    CHECK(ampify::ValidateTree(tree, &error));
    CHECK(error.empty());
}

TEST_CASE(UiTree_RefusesAnUnknownPrimitive) {
    std::string error;
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"script","text":"x"})"), &error));
    CHECK(error.find("script") != std::string::npos);
}

TEST_CASE(UiTree_RefusesAChildOfAPrimitiveThatHasNone) {
    std::string error;
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"text","children":[{"type":"text"}]})"), &error));
}

TEST_CASE(UiTree_RefusesAnObjectOrAListAsAnAttribute) {
    std::string error;
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"text","style":{"color":"red"}})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"text","items":[1,2]})"), &error));
}

TEST_CASE(UiTree_RefusesTooMuchDepth) {
    std::string error;
    CHECK(ampify::ValidateTree(Parse(Nested(7)), &error));
    CHECK(!ampify::ValidateTree(Parse(Nested(9)), &error));
    CHECK(error.find("too deep") != std::string::npos);
}

TEST_CASE(UiTree_RefusesTooManyElements) {
    std::string error;
    std::string children;
    for (int i = 0; i < 300; i++) children += (i ? "," : "") + std::string(R"({"type":"text"})");
    CHECK(ampify::ValidateTree(Parse(R"({"type":"stack","children":[)" + children + "]}"), &error) == false);
    CHECK(error.find("too large") != std::string::npos);
}

TEST_CASE(UiTree_RefusesLongText) {
    std::string error;
    std::string text(501, 'a');
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"text","text":")" + text + R"("})"), &error));
}

TEST_CASE(UiModel_AddsAControlAndFindsIt) {
    ampify::UiModel model;
    int id = model.AddControl("demo", "topbar", "Paroles", "<svg/>");
    CHECK(id > 0);
    auto control = model.Find(id);
    CHECK(control.has_value());
    CHECK(control->elementId == "demo");
    CHECK(control->placement == "topbar");
    CHECK(control->label == "Paroles");
    CHECK(!model.Find(id + 100).has_value());
}

TEST_CASE(UiModel_RefusesAnUnknownPlacement) {
    ampify::UiModel model;
    CHECK(model.AddControl("demo", "footer", "x", "") == 0);
    CHECK(model.Controls().empty());
}

TEST_CASE(UiModel_PlacementsNeedTheMatchingPermission) {
    CHECK(ampify::PlacementPermission("topbar") == "ui.topbar");
    CHECK(ampify::PlacementPermission("playbar") == "ui.playbar");
    CHECK(ampify::PlacementPermission("sidebar") == "ui.sidebar");
    CHECK(ampify::PlacementPermission("menu-context") == "ui.menu");
    CHECK(ampify::PlacementPermission("menu-app") == "ui.menu");
}

TEST_CASE(UiModel_APanelIsReplacedWhenRegisteredAgain) {
    ampify::UiModel model;
    model.SetPanel("demo", "bar", Parse(R"({"type":"text","text":"un"})"));
    model.SetPanel("demo", "bar", Parse(R"({"type":"text","text":"deux"})"));
    auto panel = model.Panel("demo", "bar");
    CHECK(panel.has_value());
    CHECK(panel->tree.Find("text")->AsString() != nullptr);
    CHECK(*panel->tree.Find("text")->AsString() == "deux");
}

TEST_CASE(UiModel_RemoveElementKeepsTheOthers) {
    ampify::UiModel model;
    int mine = model.AddControl("demo", "topbar", "a", "");
    int theirs = model.AddControl("other", "topbar", "b", "");
    model.SetPanel("demo", "bar", Parse(R"({"type":"text"})"));
    model.SetTitle("demo", "Titre");
    model.OpenPopup("demo", Parse(R"({"type":"text"})"));
    model.Navigate("demo", "/library");

    model.RemoveElement("demo");
    CHECK(!model.Find(mine).has_value());
    CHECK(model.Find(theirs).has_value());
    CHECK(!model.Panel("demo", "bar").has_value());
    CHECK(!model.Title("demo").has_value());
    CHECK(model.Popups().empty());
    CHECK(!model.Route().has_value());
}

TEST_CASE(UiModel_TheLastNavigationWins) {
    ampify::UiModel model;
    model.Navigate("demo", "/a");
    model.Navigate("demo", "/b");
    auto route = model.Route();
    CHECK(route.has_value());
    CHECK(route->second == "/b");
}

TEST_CASE(UiTree_AcceptsCardsDropdownsAndCallbackIds) {
    std::string error;
    CHECK(ampify::ValidateTree(Parse(R"({"type":"card","title":"Settings","onClick":3,"children":[)"
                                  R"({"type":"textfield","value":"","onChange":4},)"
                                  R"({"type":"dropdown","value":"a","onChange":5,"children":[{"type":"option","value":"a","label":"A"}]},)"
                                  R"({"type":"button","label":"OK","tooltip":"Valider","tooltipPlacement":"top","tooltipDelay":0}]})"),
                            &error));
    CHECK(error.empty());
}

TEST_CASE(UiTree_RefusesMisplacedOptionsAndCallbacks) {
    std::string error;
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"option","value":"a"})"), &error));
    CHECK(error.find("option") != std::string::npos);
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"dropdown","children":[{"type":"text","text":"a"}]})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"text","text":"a","onClick":1})"), &error));
    CHECK(error.find("onClick") != std::string::npos);
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"button","onClick":1.5})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"button","onClick":"1"})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"slider","onClick":1})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"button","onChange":1})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"button","tooltipDelay":20000})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"button","tooltip":3})"), &error));
}

TEST_CASE(UiModel_NamedPopupsAreReplacedAndClosed) {
    ampify::UiModel ui;
    ui.OpenPopup("a", Parse(R"({"type":"text","text":"once"})"));
    ui.SetPopup("a", "infos", Parse(R"({"type":"text","text":"1"})"));
    ui.SetPopup("a", "infos", Parse(R"({"type":"text","text":"2"})"));
    auto popups = ui.Popups();
    CHECK(popups.size() == 2);
    CHECK(popups.size() == 2 && popups[1].tree.Dump() == R"({"type":"text","text":"2"})");
    ui.ClosePopup("a", "infos");
    CHECK(ui.Popups().size() == 1);
    int control = ui.AddControl("a", "topbar", "x", "");
    CHECK(ui.ReserveId() == control + 1);
}
