#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../src/core/i18n.hpp"
#include "../src/core/journal.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

std::string Manifest(const std::string& id, const std::string& kind, const std::string& permissions) {
    return R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":")" + kind +
           R"(","author":"Shirowwww","description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" +
           permissions + "]}";
}

// An element with its own folder, a fake host, and a log in a string.
struct Fixture {
    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};

    std::unique_ptr<ampify::ScriptExtension> Make(const std::string& id, const std::string& kind,
                                               const std::string& permissions, const std::string& main) {
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("ui-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        std::ofstream(dir / "manifest.json") << Manifest(id, kind, permissions);
        std::ofstream(dir / "main.js") << main;
        ampify::ManifestResult result = ampify::ParseManifest(Manifest(id, kind, permissions), id, 1,
                                                        [](const std::string&) { return true; });
        return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, host, journal);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

}  // namespace

TEST_CASE(Interface_ATopbarButtonIsRecordedAndItsClickRuns) {
    Fixture f;
    auto element = f.Make("button", "extension", R"("ui.topbar")",
                          R"(Ampify.ui.topbar.addButton({ label: "Paroles", icon: "<svg/>", onClick: () => Ampify.log("clic") });)");
    CHECK(element->Start());
    auto controls = f.host.ui.Controls();
    CHECK(controls.size() == 1);
    CHECK(controls[0].elementId == "button");
    CHECK(controls[0].placement == "topbar");
    CHECK(controls[0].label == "Paroles");

    element->DeliverClick(controls[0].id);
    element->Pump();
    CHECK(f.Logged("[button] clic"));
    element->Stop();
}

TEST_CASE(Interface_APlaybarTextWithoutOnClickIsNotClickable) {
    Fixture f;
    auto element = f.Make("volume", "extension", R"("ui.playbar", "ui.sidebar")",
                          R"(Ampify.ui.playbar.addButton({ label: "50 %" });
                             try { Ampify.ui.sidebar.addItem({ label: "Stats" }); } catch (e) { Ampify.log("refused"); })");
    CHECK(element->Start());
    auto controls = f.host.ui.Controls();
    CHECK(controls.size() == 1);
    CHECK(controls.size() == 1 && controls[0].label == "50 %" && !controls[0].clickable);
    CHECK(f.Logged("refused"));  // a sidebar entry is there to be clicked
    element->Stop();
}

TEST_CASE(Interface_AButtonNeedsItsPermission) {
    Fixture f;
    auto element = f.Make("nobar", "extension", "", R"(Ampify.ui.playbar.addButton({ onClick: () => {} });)");
    CHECK(!element->Start());
    CHECK(f.host.ui.Controls().empty());
    CHECK(f.Logged("ui.playbar"));
}

TEST_CASE(Interface_AnUnknownMenuKindIsRefused) {
    Fixture f;
    auto element = f.Make("menus", "extension", R"("ui.menu")",
                          R"(Ampify.ui.menu.addItem("sidebar", { onClick: () => {} });)");
    CHECK(!element->Start());
    CHECK(f.host.ui.Controls().empty());
}

TEST_CASE(Interface_ASubmenuHoldsItsEntriesAndEachOneClicks) {
    Fixture f;
    auto element = f.Make("submenu", "extension", R"("ui.menu")", R"(
        Ampify.ui.menu.addItem("context", { label: "Tools", items: [
          { label: "Copy title", onClick: (item) => Ampify.log("copy " + item.title) },
          { label: "Search", onClick: (item) => Ampify.log("search " + item.title) },
        ] });
        Ampify.ui.menu.addItem("context", { label: "Alone", onClick: () => Ampify.log("alone") });)");
    CHECK(element->Start());
    std::vector<ampify::UiControl> controls = f.host.ui.Controls();
    CHECK(controls.size() == 4);
    if (controls.size() != 4) return;
    CHECK(!controls[0].clickable && controls[0].parent == 0 && controls[0].label == "Tools");
    CHECK(controls[1].clickable && controls[1].parent == controls[0].id);
    CHECK(controls[2].clickable && controls[2].parent == controls[0].id);
    CHECK(controls[3].clickable && controls[3].parent == 0);
    element->DeliverEvent("menuClick", R"({"id":)" + std::to_string(controls[2].id) + R"(,"title":"Merci"})");
    element->DeliverEvent("menuClick", R"({"id":)" + std::to_string(controls[1].id) + R"(,"title":"Merci"})");
    element->Pump();
    CHECK(f.Logged("[submenu] search Merci"));
    CHECK(f.Logged("[submenu] copy Merci"));
    element->Stop();
    CHECK(f.host.ui.Controls().empty());
}

TEST_CASE(Interface_AContextEntryGetsTheKindAndIdentifiersOfItsItem) {
    Fixture f;
    auto element = f.Make("items", "extension", R"("ui.menu")", R"(
        Ampify.ui.menu.addItem("context", { label: "Show", onClick: (detail) => Ampify.log(JSON.stringify(detail)) });)");
    CHECK(element->Start());
    std::vector<ampify::UiControl> controls = f.host.ui.Controls();
    CHECK(controls.size() == 1);
    if (controls.size() != 1) return;
    const std::string id = std::to_string(controls[0].id);
    element->DeliverEvent("menuClick", R"({"id":)" + id + R"(,"title":"Merci","item":{"kind":"album","id":"12","canonicalId":"c"}})");
    element->DeliverEvent("menuClick", R"({"id":)" + id + R"(,"title":"Merci","item":{"kind":"song"}})");
    element->DeliverEvent("menuClick", R"({"id":)" + id + R"(,"title":"Merci"})");
    element->DeliverEvent("menuClick", R"({"id":)" + id + R"(,"title":"Merci","item":"not an object"})");
    element->Pump();
    CHECK(f.Logged(R"([items] {"title":"Merci","item":{"kind":"album","id":"12","canonicalId":"c"}})"));
    CHECK(f.Logged(R"([items] {"title":"Merci","item":{"kind":"song"}})"));
    CHECK(f.Logged(R"([items] {"title":"Merci"})"));
    element->Stop();
}

TEST_CASE(Interface_ASubmenuIsRefusedEmptyTooLongOrNested) {
    Fixture f;
    auto element = f.Make("badsubmenu", "extension", R"("ui.menu")", R"(
        const tries = [
          { label: "Empty", items: [] },
          { label: "Nested", items: [{ label: "Inner", items: [{ label: "x", onClick: () => {} }] }] },
          { label: "Many", items: Array.from({ length: 21 }, () => ({ label: "x", onClick: () => {} })) },
          { label: "Text", items: "not a list" },
        ];
        for (const options of tries) {
          try { Ampify.ui.menu.addItem("context", options); Ampify.log("accepted " + options.label); }
          catch (e) { Ampify.log("refused " + options.label + " " + e.name); }
        })");
    CHECK(element->Start());
    CHECK(f.Logged("refused Empty TypeError"));
    CHECK(f.Logged("refused Nested TypeError"));
    CHECK(f.Logged("refused Many TypeError"));
    CHECK(f.Logged("refused Text TypeError"));
    CHECK(!f.Logged("accepted"));
}

TEST_CASE(Interface_ASidebarEntryCanStartOnAndBeSwitched) {
    Fixture f;
    auto element = f.Make("sideon", "extension", R"("ui.sidebar")", R"(
        const id = Ampify.ui.sidebar.addItem({ label: "Stats", icon: "", onClick: () => {}, active: true });
        Ampify.log("id " + typeof id);
        Ampify.ui.setActive(id, false);
        Ampify.ui.setActive(id, true);)");
    CHECK(element->Start());
    CHECK(f.Logged("id number"));
    std::vector<ampify::UiControl> controls = f.host.ui.Controls();
    CHECK(controls.size() == 1);
    if (controls.size() != 1) return;
    CHECK(controls[0].placement == "sidebar" && controls[0].active);
}

TEST_CASE(Interface_ASubmenuWithABadEntryLeavesNothingInTheModel) {
    Fixture f;
    auto element = f.Make("halfsubmenu", "extension", R"("ui.menu")", R"(
        try {
          Ampify.ui.menu.addItem("context", { label: "Half", items: [
            { label: "good", onClick: () => {} },
            { label: "no click" },
          ] });
          Ampify.log("accepted");
        } catch (e) { Ampify.log("refused " + e.name); })");
    CHECK(element->Start());
    CHECK(f.Logged("refused TypeError"));
    CHECK(f.host.ui.Controls().empty());
}

TEST_CASE(Interface_AnEntryCannotJoinTheSubmenuOfAnotherElementOrMenu) {
    ampify::UiModel model;
    int tools = model.AddControl("one", "menu-context", "Tools", "", false, 0);
    CHECK(tools != 0);
    CHECK(model.AddControl("one", "menu-context", "Inside", "", true, tools) != 0);
    CHECK(model.AddControl("two", "menu-context", "Foreign", "", true, tools) == 0);  // another element's
    CHECK(model.AddControl("one", "menu-app", "Other menu", "", true, tools) == 0);   // another menu
    CHECK(model.AddControl("one", "menu-context", "Ghost", "", true, 999) == 0);      // no such submenu
    CHECK(model.AddControl("one", "menu-context", "Text", "", false, tools) == 0);    // an entry must be clickable
    int inner = model.AddControl("one", "menu-context", "Entry", "", true, tools);
    CHECK(model.AddControl("one", "menu-context", "Deeper", "", true, inner) == 0);   // not a submenu itself
}

TEST_CASE(Interface_AButtonStartsOnAndSetActiveSwitchesItOnAndOff) {
    Fixture f;
    auto element = f.Make("toggle-button", "extension", R"("ui.playbar", "ui.topbar")", R"(
        const a = Ampify.ui.playbar.addButton({ label: "A", active: true, onClick: () => {} });
        const b = Ampify.ui.topbar.addButton({ label: "B", onClick: () => {} });
        Ampify.log("start " + a + " " + b);
        Ampify.log("on " + Ampify.ui.setActive(b, true));
        Ampify.log("off " + Ampify.ui.setActive(a, false));
        Ampify.log("unknown " + Ampify.ui.setActive(999, true));)");
    CHECK(element->Start());
    std::vector<ampify::UiControl> controls = f.host.ui.Controls();
    CHECK(controls.size() == 2);
    if (controls.size() != 2) return;
    CHECK(!controls[0].active);  // a: started on, then switched off
    CHECK(controls[1].active);   // b: started off, then switched on
    CHECK(f.Logged("[toggle-button] on true"));
    CHECK(f.Logged("[toggle-button] off true"));
    CHECK(f.Logged("[toggle-button] unknown false"));
    element->Stop();
}

TEST_CASE(Interface_AnElementCannotSwitchAnotherElementsButton) {
    ampify::UiModel model;
    int mine = model.AddControl("one", "playbar", "A", "", true, 0);
    CHECK(!model.SetControlActive("two", mine, true));
    CHECK(model.SetControlActive("one", mine, true));
    bool active = false;
    for (const ampify::UiControl& control : model.Controls()) {
        if (control.id == mine) active = control.active;
    }
    CHECK(active);
}

TEST_CASE(Interface_ABadTreeIsRefusedWhenItIsRegistered) {
    Fixture f;
    auto element = f.Make("badpanel", "extension", R"("ui.panel")",
                          R"(Ampify.ui.panel.register("bar", { type: "script", text: "x" });)");
    CHECK(!element->Start());
    CHECK(!f.host.ui.Panel("badpanel", "bar").has_value());
}

TEST_CASE(Interface_APanelIsStoredUnderItsName) {
    Fixture f;
    auto element = f.Make("panel", "extension", R"("ui.panel")",
                          R"(Ampify.ui.panel.register("lyrics-bar", { type: "stack", children: [{ type: "text", text: "Paroles" }] });)");
    CHECK(element->Start());
    auto panel = f.host.ui.Panel("panel", "lyrics-bar");
    CHECK(panel.has_value());
    CHECK(panel->tree.Find("type")->AsString() != nullptr);
    element->Stop();
}

TEST_CASE(Interface_PanelNamesAreLowercaseAndSimple) {
    Fixture f;
    auto element = f.Make("names", "extension", R"("ui.panel")",
                          R"(Ampify.ui.panel.register("../secret", { type: "text" });)");
    CHECK(!element->Start());
    CHECK(f.host.ui.Controls().empty());
}

TEST_CASE(Interface_PagesAreForAppsOnly) {
    Fixture f;
    auto extension = f.Make("pages-ext", "extension", R"("ui.page")",
                            R"(Ampify.ui.page.register("/home", { type: "text" });)");
    CHECK(!extension->Start());

    auto app = f.Make("pages-app", "app", R"("ui.page")",
                      R"(Ampify.ui.page.register("/home", { type: "text", text: "Accueil" });)");
    CHECK(app->Start());
    CHECK(f.host.ui.Page("pages-app", "/home").has_value());
    app->Stop();
}

TEST_CASE(Interface_NavigateAsksForAPage) {
    Fixture f;
    auto element = f.Make("nav", "extension", R"("ui.page")", R"(Ampify.ui.navigate("/library");)");
    CHECK(element->Start());
    auto route = f.host.ui.Route();
    CHECK(route.has_value());
    CHECK(route->first == "nav");
    CHECK(route->second == "/library");
}

TEST_CASE(Interface_ApplePlayerNeedsTheFullscreenPermission) {
    Fixture f;
    auto denied = f.Make("apple-no", "extension", "", R"(Ampify.ui.applePlayer.open();)");
    CHECK(!denied->Start());
    CHECK(f.host.ui.ApplePlayerRequests() == 0);

    auto allowed = f.Make("apple-yes", "extension", R"("ui.fullscreen")",
                          R"(Ampify.ui.applePlayer.open(); Ampify.ui.applePlayer.open();)");
    CHECK(allowed->Start());
    CHECK(f.host.ui.ApplePlayerRequests() == 2);
}

TEST_CASE(Interface_TitleNeedsTheWindowPermission) {
    Fixture f;
    auto denied = f.Make("title-no", "extension", "", R"(Ampify.ui.title.set("Demo");)");
    CHECK(!denied->Start());

    auto allowed = f.Make("title-yes", "extension", R"("ui.window")", R"(Ampify.ui.title.set("Demo");)");
    CHECK(allowed->Start());
    CHECK(f.host.ui.Title("title-yes") == std::optional<std::string>("Demo"));
}

TEST_CASE(Interface_ASurfaceCarriesTheElementNameUntilItSetsATitle) {
    Fixture f;
    auto plain = f.Make("plain", "app", R"("ui.page")", R"(Ampify.ui.page.register("/plain", { type: "text" });)");
    CHECK(plain->Start());
    CHECK(f.host.ui.Title("plain") == std::optional<std::string>("Demo"));

    auto titled = f.Make("titled", "extension", R"("ui.window")", R"(Ampify.ui.title.set("Other");)");
    CHECK(titled->Start());
    CHECK(f.host.ui.Title("titled") == std::optional<std::string>("Other"));
}

TEST_CASE(Interface_TheElementNameFollowsTheDisplayLanguage) {
    Fixture f;
    const std::string manifest =
        R"({"id":"translated","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)"
        R"("description":"Test.","kitApi":"1.0","main":"main.js","translations":{"fr":{"name":"Démo","description":"Essai."}}})";
    ampify::ManifestResult result = ampify::ParseManifest(manifest, "translated", 1, [](const std::string&) { return true; });
    CHECK(result.manifest.has_value());
    if (!result.manifest) return;
    fs::path dir = fs::temp_directory_path() / "ampify-tests" / "ui-translated";
    fs::create_directories(dir);
    std::ofstream(dir / "main.js") << "";
    std::string before = ampify::i18n::Language();
    ampify::i18n::SetLanguage("fr");
    ampify::ScriptExtension element(*result.manifest, dir, f.host, f.journal);
    CHECK(element.Start());
    ampify::i18n::SetLanguage(before);
    CHECK(f.host.ui.Title("translated") == std::optional<std::string>("Démo"));
}

TEST_CASE(Interface_StopRemovesWhatTheElementAdded) {
    Fixture f;
    auto element = f.Make("gone", "extension", R"("ui.topbar", "ui.panel")",
                          R"(Ampify.ui.topbar.addButton({ onClick: () => {} }); Ampify.ui.panel.register("bar", { type: "text" });)");
    CHECK(element->Start());
    CHECK(f.host.ui.Controls().size() == 1);
    element->Stop();
    CHECK(f.host.ui.Controls().empty());
    CHECK(!f.host.ui.Panel("gone", "bar").has_value());
}
