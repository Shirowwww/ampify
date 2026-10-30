#include <string>

#include "../core/ui_model.hpp"
#include "test.hpp"

namespace {

amk::Json Parse(const std::string& text) {
    std::string error;
    std::optional<amk::Json> value = amk::Json::Parse(text, &error);
    return value ? *value : amk::Json();
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
    amk::Json tree = Parse(R"({"type":"stack","children":[{"type":"text","text":"Paroles"},)"
                           R"({"type":"row","children":[{"type":"button","label":"OK"},{"type":"toggle","label":"x"}]}]})");
    CHECK(amk::ValidateTree(tree, &error));
    CHECK(error.empty());
}

TEST_CASE(UiTree_RefusesAnUnknownPrimitive) {
    std::string error;
    CHECK(!amk::ValidateTree(Parse(R"({"type":"script","text":"x"})"), &error));
    CHECK(error.find("script") != std::string::npos);
}

TEST_CASE(UiTree_RefusesAChildOfAPrimitiveThatHasNone) {
    std::string error;
    CHECK(!amk::ValidateTree(Parse(R"({"type":"text","children":[{"type":"text"}]})"), &error));
}

TEST_CASE(UiTree_RefusesAnObjectOrAListAsAnAttribute) {
    std::string error;
    CHECK(!amk::ValidateTree(Parse(R"({"type":"text","style":{"color":"red"}})"), &error));
    CHECK(!amk::ValidateTree(Parse(R"({"type":"text","items":[1,2]})"), &error));
}

TEST_CASE(UiTree_RefusesTooMuchDepth) {
    std::string error;
    CHECK(amk::ValidateTree(Parse(Nested(7)), &error));
    CHECK(!amk::ValidateTree(Parse(Nested(9)), &error));
    CHECK(error.find("profonde") != std::string::npos);
}

TEST_CASE(UiTree_RefusesTooManyElements) {
    std::string error;
    std::string children;
    for (int i = 0; i < 300; i++) children += (i ? "," : "") + std::string(R"({"type":"text"})");
    CHECK(amk::ValidateTree(Parse(R"({"type":"stack","children":[)" + children + "]}"), &error) == false);
    CHECK(error.find("trop grande") != std::string::npos);
}

TEST_CASE(UiTree_RefusesLongText) {
    std::string error;
    std::string text(501, 'a');
    CHECK(!amk::ValidateTree(Parse(R"({"type":"text","text":")" + text + R"("})"), &error));
}

TEST_CASE(UiModel_AddsAControlAndFindsIt) {
    amk::UiModel model;
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
    amk::UiModel model;
    CHECK(model.AddControl("demo", "sidebar", "x", "") == 0);
    CHECK(model.Controls().empty());
}

TEST_CASE(UiModel_PlacementsNeedTheMatchingPermission) {
    CHECK(amk::PlacementPermission("topbar") == "ui.topbar");
    CHECK(amk::PlacementPermission("playbar") == "ui.playbar");
    CHECK(amk::PlacementPermission("menu-context") == "ui.menu");
    CHECK(amk::PlacementPermission("menu-app") == "ui.menu");
}

TEST_CASE(UiModel_APanelIsReplacedWhenRegisteredAgain) {
    amk::UiModel model;
    model.SetPanel("demo", "bar", Parse(R"({"type":"text","text":"un"})"));
    model.SetPanel("demo", "bar", Parse(R"({"type":"text","text":"deux"})"));
    auto panel = model.Panel("demo", "bar");
    CHECK(panel.has_value());
    CHECK(panel->tree.Find("text")->AsString() != nullptr);
    CHECK(*panel->tree.Find("text")->AsString() == "deux");
}

TEST_CASE(UiModel_RemoveElementKeepsTheOthers) {
    amk::UiModel model;
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
    amk::UiModel model;
    model.Navigate("demo", "/a");
    model.Navigate("demo", "/b");
    auto route = model.Route();
    CHECK(route.has_value());
    CHECK(route->second == "/b");
}

TEST_CASE(UiTree_AcceptsCardsDropdownsAndCallbackIds) {
    std::string error;
    CHECK(amk::ValidateTree(Parse(R"({"type":"card","title":"Réglages","onClick":3,"children":[)"
                                  R"({"type":"textfield","value":"","onChange":4},)"
                                  R"({"type":"dropdown","value":"a","onChange":5,"children":[{"type":"option","value":"a","label":"A"}]},)"
                                  R"({"type":"button","label":"OK","tooltip":"Valider","tooltipPlacement":"top","tooltipDelay":0}]})"),
                            &error));
    CHECK(error.empty());
}

TEST_CASE(UiTree_RefusesMisplacedOptionsAndCallbacks) {
    std::string error;
    CHECK(!amk::ValidateTree(Parse(R"({"type":"option","value":"a"})"), &error));
    CHECK(error.find("option") != std::string::npos);
    CHECK(!amk::ValidateTree(Parse(R"({"type":"dropdown","children":[{"type":"text","text":"a"}]})"), &error));
    CHECK(!amk::ValidateTree(Parse(R"({"type":"text","text":"a","onClick":1})"), &error));
    CHECK(error.find("onClick") != std::string::npos);
    CHECK(!amk::ValidateTree(Parse(R"({"type":"button","onClick":1.5})"), &error));
    CHECK(!amk::ValidateTree(Parse(R"({"type":"button","onClick":"1"})"), &error));
    CHECK(!amk::ValidateTree(Parse(R"({"type":"slider","onClick":1})"), &error));
    CHECK(!amk::ValidateTree(Parse(R"({"type":"button","onChange":1})"), &error));
    CHECK(!amk::ValidateTree(Parse(R"({"type":"button","tooltipDelay":20000})"), &error));
    CHECK(!amk::ValidateTree(Parse(R"({"type":"button","tooltip":3})"), &error));
}

TEST_CASE(UiModel_NamedPopupsAreReplacedAndClosed) {
    amk::UiModel ui;
    ui.OpenPopup("a", Parse(R"({"type":"text","text":"une fois"})"));
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
