#include <string>

#include "../src/core/ui_model.hpp"
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

TEST_CASE(UiTree_AcceptsTheLyricsOfTheSongPlayingWithATextStyle) {
    std::string error;
    CHECK(ampify::ValidateTree(Parse(R"({"type":"lyrics","size":26,"color":"#FFFFFF","placeholder":"None","grow":true})"),
                               &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"lyrics","size":400})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"lyrics","children":[{"type":"text","text":"x"}]})"), &error));
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

TEST_CASE(UiTree_AcceptsGridsTabsAndMenus) {
    std::string error;
    CHECK(ampify::ValidateTree(Parse(R"({"type":"grid","columns":3,"gap":8,"children":[{"type":"text","text":"a"}]})"),
                               &error));
    CHECK(ampify::ValidateTree(Parse(R"({"type":"tabs","children":[)"
                                  R"({"type":"tab","label":"Library","selected":true,"onClick":2},)"
                                  R"({"type":"tab","label":"Search","disabled":true}]})"),
                            &error));
    CHECK(ampify::ValidateTree(Parse(R"({"type":"menu","label":"More","disabled":false,"children":[)"
                                  R"({"type":"item","label":"Copy","onClick":3},{"type":"item","label":"Off","disabled":true}]})"),
                            &error));
    CHECK(error.empty());
}

TEST_CASE(UiTree_RefusesTabsAndItemsOutsideTheirContainer) {
    std::string error;
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"tab","label":"a"})"), &error));
    CHECK(error.find("tab") != std::string::npos);
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"item","label":"a"})"), &error));
    CHECK(error.find("item") != std::string::npos);
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"tabs","children":[{"type":"text","text":"a"}]})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"menu","children":[{"type":"tab","label":"a"}]})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"menu","children":[{"type":"button","label":"a"}]})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"tabs","children":[{"type":"tab","label":"a","onChange":1}]})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"text","text":"a","children":[{"type":"item","label":"a"}]})"), &error));
}

TEST_CASE(UiTree_RefusesAGridWithoutAWholeColumnCount) {
    std::string error;
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"grid","columns":0})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"grid","columns":13})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"grid","columns":2.5})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"grid","columns":"3"})"), &error));
    CHECK(error.find("columns") != std::string::npos);
    CHECK(ampify::ValidateTree(Parse(R"({"type":"grid","columns":12})"), &error));
}

TEST_CASE(UiTree_RefusesAGridWithAGapOutOfRange) {
    std::string error;
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"grid","gap":-1})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"grid","gap":1001})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"grid","gap":"8"})"), &error));
    CHECK(error.find("gap") != std::string::npos);
    CHECK(ampify::ValidateTree(Parse(R"({"type":"grid","gap":0})"), &error));
    CHECK(ampify::ValidateTree(Parse(R"({"type":"grid","gap":1000})"), &error));
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

TEST_CASE(UiTree_AcceptsALayerWithStyleAttributes) {
    std::string error;
    ampify::Json tree = Parse(
        R"({"type":"layer","background":"#FF101010","children":[)"
        R"({"type":"image","src":"https://example.com/a.jpg","fit":"cover","opacity":0.8,"cornerRadius":12},)"
        R"j({"type":"stack","background":"acrylic(#CC000000, 0.6)","padding":"48,32,48,32","cornerRadius":0,)j"
        R"("align":"center","valign":"stretch","maxWidth":900,"minWidth":200,"width":800,"height":600,"children":[)"
        R"({"type":"text","text":"Song","size":64,"weight":700,"color":"#FFFFFF","align":"center","wrap":false,)"
        R"("maxLines":2,"grow":true},)"
        R"({"type":"text","text":"line","opacity":0.4,"scale":1.1,"transition":"opacity 400ms, scale 0.3s"}]},)"
        R"({"type":"row","background":"acrylic","padding":8,"children":[{"type":"button","label":"x","align":"end"}]}]})");
    CHECK(ampify::ValidateTree(tree, &error));
    CHECK(error.empty());
}

TEST_CASE(UiTree_RefusesBadStyleValuesWithTheAttributeName) {
    const char* bad[][2] = {
        {R"({"type":"stack","background":"red"})", "background"},
        {R"j({"type":"stack","background":"acrylic(#CC000000, 2)"})j", "background"},
        {R"({"type":"stack","padding":"1,2,3"})", "padding"},
        {R"({"type":"card","padding":-4})", "padding"},
        {R"({"type":"layer","cornerRadius":"8"})", "cornerRadius"},
        {R"({"type":"row","maxWidth":20000})", "maxWidth"},
        {R"({"type":"stack","align":"middle"})", "align"},
        {R"({"type":"text","align":"start"})", "align"},
        {R"({"type":"button","valign":"top"})", "valign"},
        {R"({"type":"text","grow":"yes"})", "grow"},
        {R"({"type":"text","opacity":1.5})", "opacity"},
        {R"({"type":"icon","scale":-1})", "scale"},
        {R"({"type":"text","transition":"fade 300ms"})", "transition"},
        {R"({"type":"text","transition":"opacity 20s"})", "transition"},
        {R"({"type":"text","transition":"opacity"})", "transition"},
        {R"({"type":"text","size":200})", "size"},
        {R"({"type":"text","weight":50})", "weight"},
        {R"({"type":"text","weight":450.5})", "weight"},
        {R"({"type":"text","color":"#12345"})", "color"},
        {R"({"type":"text","wrap":1})", "wrap"},
        {R"({"type":"text","maxLines":1.5})", "maxLines"},
        {R"({"type":"image","fit":"zoom"})", "fit"},
        {R"({"type":"image","width":"100%"})", "width"},
    };
    for (const auto& [tree, attribute] : bad) {
        std::string error;
        if (ampify::ValidateTree(Parse(tree), &error) || error.find(attribute) == std::string::npos) {
            ampify::test::ReportFailure(__FILE__, __LINE__, std::string("accepted or unclear: ") + tree + " -> " + error);
        }
    }
}

TEST_CASE(UiTree_StyleAttributesAreOnlyCheckedWhereTheyAreRead) {
    // Trees written before the style attributes keep working: a size on a button or a color on a slider was never
    // read, so it is still not refused.
    std::string error;
    CHECK(ampify::ValidateTree(Parse(R"({"type":"button","label":"x","width":"100%"})"), &error));
    CHECK(ampify::ValidateTree(Parse(R"({"type":"slider","color":"red","size":"big"})"), &error));
    CHECK(ampify::ValidateTree(Parse(R"({"type":"icon","size":24})"), &error));
}

TEST_CASE(UiTree_InertIsABooleanOnAnyNode) {
    std::string error;
    CHECK(ampify::ValidateTree(Parse(R"({"type":"stack","inert":true,"children":[{"type":"button","label":"x","inert":false}]})"), &error));
    CHECK(!ampify::ValidateTree(Parse(R"({"type":"text","text":"x","inert":"yes"})"), &error));
    CHECK(error.find("inert") != std::string::npos);
}

TEST_CASE(UiModel_FullscreenSessionsFollowTheLastElementThatOpens) {
    ampify::UiModel ui;
    std::uint64_t first = ui.SetFullscreen("a", Parse(R"({"type":"text","text":"1"})"), 0);
    CHECK(first > 0);
    CHECK(ui.SetFullscreen("a", Parse(R"({"type":"text","text":"2"})"), first) == first);
    auto slot = ui.Fullscreen();
    CHECK(slot && slot->elementId == "a" && slot->id == std::to_string(first));
    CHECK(slot && slot->tree.Dump() == R"({"type":"text","text":"2"})");
    CHECK(ui.TakeDisplacedFullscreens().empty());

    // Another element takes the screen: a is listed once, and its late updates and closes change nothing.
    std::uint64_t second = ui.SetFullscreen("b", Parse(R"({"type":"text","text":"b"})"), 0);
    CHECK(second > first);
    auto displaced = ui.TakeDisplacedFullscreens();
    CHECK(displaced.size() == 1 && displaced[0].first == "a" && displaced[0].second == first);
    CHECK(ui.TakeDisplacedFullscreens().empty());
    CHECK(ui.SetFullscreen("a", Parse(R"({"type":"text","text":"3"})"), first) == 0);
    ui.CloseFullscreen("a", first);
    slot = ui.Fullscreen();
    CHECK(slot && slot->elementId == "b");

    // An old session of b closes nothing; its current one does.
    ui.CloseFullscreen("b", first);
    CHECK(ui.Fullscreen().has_value());
    ui.CloseFullscreen("b", second);
    CHECK(!ui.Fullscreen().has_value());
    CHECK(ui.SetFullscreen("b", Parse(R"({"type":"text"})"), second) == 0);

    ui.SetFullscreen("c", Parse(R"({"type":"text"})"), 0);
    ui.RemoveElement("c");
    CHECK(!ui.Fullscreen().has_value());
}
