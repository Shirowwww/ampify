#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/core/extension.hpp"
#include "../src/core/extension_loader.hpp"
#include "../src/core/reload.hpp"
#include "../src/core/settings.hpp"
#include "fake_host.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;
using ampify::test::ManifestSpec;
using ampify::test::ScratchDir;

// Writes the element folder `name` under `root`.
void WriteElement(const fs::path& root, const std::string& name, const ManifestSpec& spec, const std::string& main) {
    ampify::test::WriteElement(root / name, spec, main);
}

}  // namespace

TEST_CASE(Loader_RegistersValidElementsAndSkipsTheRest) {
    ScratchDir root("discover");
    WriteElement(root, "good", {.id = "good"}, R"(Ampify.log("hello");)");
    WriteElement(root, "app-ok", {.id = "app-ok", .kind = "app"}, R"(Ampify.log("app");)");
    WriteElement(root, "bad-id", {.id = "other"}, R"(Ampify.log("x");)");  // id differs from folder
    WriteElement(root, "theme", {.id = "theme", .kind = "theme", .extra = R"("colors":"c.ini")"}, "Ampify.log('x');");
    std::ofstream(root / "theme" / "c.ini") << "[Base]\n";
    fs::create_directories(root / "no-manifest");

    ampify::test::KernelFixture k(root);

    ampify::LoadReport report = k.loader.Discover();
    CHECK(report.loaded == std::vector<std::string>{"good"});
    CHECK(report.rejected.size() == 3);
    CHECK(k.out.str().find("[bad-id] rejected") != std::string::npos);
    CHECK(k.out.str().find("type theme not loaded") != std::string::npos);
    CHECK(k.out.str().find("[app-ok] type app not loaded from the folder extensions") != std::string::npos);
    CHECK(k.hosts.StateOf("good") == ampify::ExtensionState::Off);  // disabled until the user enables it
}

TEST_CASE(Loader_SkipsElementsDeclaredForTheOtherProcess) {
    ScratchDir root("processes");
    WriteElement(root, "uionly", {.id = "uionly", .processes = R"("ui")"}, R"(Ampify.log("ui");)");
    WriteElement(root, "anywhere", {.id = "anywhere", .processes = R"("ui","engine")"},
                 R"(Ampify.log("ok");)");
    WriteElement(root, "plain", {.id = "plain"}, R"(Ampify.log("ui");)");  // no processes: interface only

    ampify::test::KernelFixture k(root, "", "engine");

    ampify::LoadReport report = k.loader.Discover();
    CHECK(report.loaded == std::vector<std::string>{"anywhere"});
    CHECK((report.skipped == std::vector<std::string>{"plain", "uionly"}));
    CHECK(k.out.str().find("[uionly] skipped in the process engine") != std::string::npos);
}

TEST_CASE(Loader_MissingRootIsNotAnError) {
    ScratchDir root("absent");
    ampify::test::KernelFixture k(root, "nothing-here");
    CHECK(k.loader.Discover().loaded.empty());
}

TEST_CASE(Loader_EnabledElementsStartAndOneFailureDoesNotStopTheOthers) {
    ScratchDir root("isolation");
    WriteElement(root, "solid", {.id = "solid"}, R"(Ampify.log("solid");)");
    WriteElement(root, "broken", {.id = "broken"}, R"(throw new Error("panne");)");

    ampify::test::KernelFixture k(root);
    CHECK(k.loader.Discover().loaded.size() == 2);

    k.settings.SetBool("extensions.solid.enabled", true);
    k.settings.SetBool("extensions.broken.enabled", true);
    k.hosts.StartAll(k.settings);

    CHECK(k.hosts.StateOf("solid") == ampify::ExtensionState::Running);
    CHECK(k.hosts.StateOf("broken") == ampify::ExtensionState::Failed);
    CHECK(k.out.str().find("[solid] solid") != std::string::npos);

    k.hosts.StopAll();
    CHECK(k.hosts.StateOf("solid") == ampify::ExtensionState::Off);
}

TEST_CASE(Loader_TargetedClicksAndEventsReachOnlyTheirElement) {
    ScratchDir root("targeted");
    WriteElement(root, "one", {.id = "one", .permissions = R"("ui.panel")"},
                 R"(const { h, useState } = Ampify.React;
function P() {
  const [n, setN] = useState(0);
  return h("stack", null, h("text", null, "n=" + n),
    h("button", { label: "more", onClick: () => { setN(n + 1); Ampify.log("a click"); } }),
    h("toggle", { label: "t", onChange: (e) => Ampify.log("toggle " + e.value) }));
}
Ampify.ReactDOM.render(h(P), "panel:p");)");
    WriteElement(root, "two", {.id = "two"}, R"(Ampify.log("two");)");

    ampify::test::KernelFixture k(root);
    CHECK(k.loader.Discover().loaded.size() == 2);
    k.settings.SetBool("extensions.one.enabled", true);
    k.settings.SetBool("extensions.two.enabled", true);
    k.hosts.StartAll(k.settings);
    k.loader.Pump();

    std::optional<ampify::UiSlot> panel = k.host.Ui().Panel("one", "p");
    CHECK(panel.has_value());
    if (!panel) return;
    const ampify::Json* children = panel->tree.Find("children");
    const ampify::Json::Array* items = children ? children->AsArray() : nullptr;
    CHECK(items && items->size() == 3);
    if (!items || items->size() != 3) return;
    const ampify::Json* onClick = (*items)[1].Find("onClick");
    const ampify::Json* onChange = (*items)[2].Find("onChange");
    CHECK(onClick && onChange);
    if (!onClick || !onChange) return;
    int click = static_cast<int>(*onClick->AsNumber());
    int change = static_cast<int>(*onChange->AsNumber());

    k.loader.DeliverClickTo("two", click);  // another element: nothing happens
    k.loader.Pump();
    CHECK(k.out.str().find("a click") == std::string::npos);

    k.loader.DeliverClickTo("one", click);
    k.loader.DeliverEventTo("one", "change", R"({"id":)" + std::to_string(change) + R"(,"value":true})");
    k.loader.Pump();
    CHECK(k.out.str().find("[one] a click") != std::string::npos);
    CHECK(k.out.str().find("[one] toggle true") != std::string::npos);
    std::optional<ampify::UiSlot> after = k.host.Ui().Panel("one", "p");
    CHECK(after && after->tree.Dump().find("n=1") != std::string::npos);
    k.hosts.StopAll();
}

TEST_CASE(Loader_AMenuEntryReceivesTheNameOfItsItem) {
    ScratchDir root("menu-entries");
    WriteElement(root, "menus", {.id = "menus", .permissions = R"("ui.menu")"},
                 R"(Ampify.ui.menu.addItem("context", { label: "Find lyrics", onClick: (item) => Ampify.log("lyrics of " + item.title) });
Ampify.ui.menu.addItem("app", { label: "About", onClick: (item) => Ampify.log("about " + JSON.stringify(item)) });)");

    ampify::test::KernelFixture k(root);
    CHECK(k.loader.Discover().loaded.size() == 1);
    k.settings.SetBool("extensions.menus.enabled", true);
    k.hosts.StartAll(k.settings);
    k.loader.Pump();

    std::vector<ampify::UiControl> controls = k.host.Ui().Controls();
    CHECK(controls.size() == 2);
    if (controls.size() != 2) return;
    CHECK(controls[0].placement == "menu-context" && controls[1].placement == "menu-app");
    k.loader.DeliverEventTo("menus", "menuClick", R"({"id":)" + std::to_string(controls[0].id) + R"(,"title":"Merci"})");
    k.loader.DeliverEventTo("menus", "menuClick", R"({"id":)" + std::to_string(controls[1].id) + R"(,"title":""})");
    k.loader.DeliverEventTo("menus", "menuClick", "not json");
    k.loader.Pump();
    CHECK(k.out.str().find("[menus] lyrics of Merci") != std::string::npos);
    CHECK(k.out.str().find("[menus] about {}") != std::string::npos);
    CHECK(k.out.str().find("menuClick") != std::string::npos);  // the bad payload is reported, nothing runs
    k.hosts.StopAll();
}

TEST_CASE(Loader_ExposeApiOffKeepsEveryScriptOff) {
    ScratchDir root("expose-api");
    WriteElement(root / "extensions", "ext", {.id = "ext"}, R"(Ampify.log("ext");)");
    WriteElement(root / "apps", "page", {.id = "page", .kind = "app"}, R"(Ampify.log("page");)");

    ampify::test::KernelFixture k(root, "extensions");
    k.loader.SetAppRoot(root / "apps");
    CHECK(k.loader.Discover().loaded.size() == 2);

    k.settings.SetBool("extensions.ext.enabled", true);
    k.settings.SetBool("apps.page.enabled", true);  // the key `ampify app enable` writes
    k.settings.SetBool("expose_api", false);
    k.hosts.StartAll(k.settings);
    CHECK(k.hosts.StateOf("ext") == ampify::ExtensionState::Off);
    CHECK(k.hosts.StateOf("app:page") == ampify::ExtensionState::Off);

    k.settings.SetBool("expose_api", true);
    CHECK(k.loader.Restart("ext", k.settings));
    CHECK(k.loader.Restart("app:page", k.settings));
    CHECK(k.hosts.StateOf("ext") == ampify::ExtensionState::Running);
    CHECK(k.hosts.StateOf("app:page") == ampify::ExtensionState::Running);
    k.hosts.StopAll();
}

TEST_CASE(Loader_AnElementThatShipsWithTheKitStartsUntilTurnedOff) {
    ScratchDir root("bundled");
    WriteElement(root / "extensions", "now-playing", {.id = "now-playing"}, R"(Ampify.log("np");)");
    WriteElement(root / "apps", "marketplace", {.id = "marketplace", .kind = "app"}, R"(Ampify.log("market");)");
    WriteElement(root / "apps", "page", {.id = "page", .kind = "app"}, R"(Ampify.log("page");)");

    ampify::test::KernelFixture k(root, "extensions");
    k.loader.SetAppRoot(root / "apps");
    CHECK(k.loader.Discover().loaded.size() == 3);

    // a new install: no key written yet
    k.hosts.StartAll(k.settings);
    CHECK(k.hosts.StateOf("now-playing") == ampify::ExtensionState::Running);
    CHECK(k.hosts.StateOf("app:marketplace") == ampify::ExtensionState::Running);
    CHECK(k.hosts.StateOf("app:page") == ampify::ExtensionState::Off);  // a community app waits for the user

    k.settings.SetBool("apps.marketplace.enabled", false);  // what `ampify app disable marketplace` writes
    k.loader.Restart("app:marketplace", k.settings);
    CHECK(k.hosts.StateOf("app:marketplace") == ampify::ExtensionState::Off);
    k.hosts.StopAll();
}

TEST_CASE(Loader_RestartReloadsAnElementWithItsNewSettings) {
    ScratchDir root("restart");
    WriteElement(root, "reload", {.id = "reload"}, R"(Ampify.log("loaded");)");

    ampify::test::KernelFixture k(root);
    k.loader.Discover();

    k.settings.SetBool("extensions.reload.enabled", true);
    k.hosts.StartAll(k.settings);
    CHECK(k.hosts.StateOf("reload") == ampify::ExtensionState::Running);

    k.settings.SetBool("extensions.reload.enabled", false);
    CHECK(k.loader.Restart("reload", k.settings));
    CHECK(k.hosts.StateOf("reload") == ampify::ExtensionState::Off);
    CHECK(!k.loader.Restart("unknown", k.settings));
}

TEST_CASE(Loader_PlayerEventsAndKeysReachOnlyTheRightRunningElements) {
    ScratchDir root("events");
    WriteElement(root, "listener", {.id = "listener", .permissions = R"("player.read","ui.keyboard")"},
                 R"(Ampify.player.on("statechange", (s) => Ampify.log("state " + s.playing));
                    Ampify.keyboard.bind("F5", () => Ampify.log("key"));)");
    WriteElement(root, "idle", {.id = "idle"}, R"(Ampify.log("nothing");)");

    ampify::test::KernelFixture k(root);
    k.loader.Discover();

    k.settings.SetBool("extensions.listener.enabled", true);
    k.hosts.StartAll(k.settings);  // "idle" stays off

    ampify::PlayerSnapshot snapshot;
    snapshot.playing = true;
    k.loader.DeliverPlayer("statechange", snapshot);
    k.loader.DeliverKey("listener", "F5");
    k.loader.DeliverKey("idle", "F5");  // not its key, and it is off
    k.loader.Pump();

    CHECK(k.out.str().find("[listener] state true") != std::string::npos);
    CHECK(k.out.str().find("[listener] key") != std::string::npos);
    CHECK(k.out.str().find("[idle] nothing") == std::string::npos);
    k.hosts.StopAll();
    CHECK(k.host.keys.empty());
}

namespace {

// A kit with its folders, a loader that knows them, and settings to switch the elements.
struct KitSetup : ampify::test::KitFixture {
    explicit KitSetup(const std::string& name) : KitFixture(name) {}

    void Request(const std::string& text) { loader.Apply(ampify::ParseReloadRequests(text), settings); }
};

}  // namespace

TEST_CASE(Loader_DiscoversAppsInTheirOwnFolder) {
    KitSetup s("apps-folder");
    WriteElement(s.root / "apps", "page", {.id = "page", .kind = "app"}, R"(Ampify.log("page opened");)");
    WriteElement(s.root / "apps", "wrong", {.id = "wrong"}, R"(Ampify.log("x");)");
    WriteElement(s.root / "apps", ".page.previous", {.id = "page", .kind = "app"}, R"(Ampify.log("x");)");  // work in progress

    ampify::LoadReport report = s.loader.Discover();
    CHECK(report.loaded == std::vector<std::string>{"app:page"});
    CHECK(report.rejected == std::vector<std::string>{"wrong"});
    CHECK(s.Logged("[wrong] type extension not loaded from the folder apps"));

    // apps.<id>.enabled switches an app, not extensions.<id>.enabled.
    s.settings.SetBool("extensions.page.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("app:page") == ampify::ExtensionState::Off);
    s.settings.SetBool("apps.page.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("app:page") == ampify::ExtensionState::Running);
    CHECK(s.Logged("[page] page opened"));
    s.hosts.StopAll();
}

TEST_CASE(Loader_AnAppAndAnExtensionMayShareAnId) {
    KitSetup s("shared-id");
    WriteElement(s.root / "extensions", "duo", {.id = "duo"}, R"(Ampify.log("extension");)");
    WriteElement(s.root / "apps", "duo", {.id = "duo", .kind = "app"}, R"(Ampify.log("app");)");
    CHECK(s.loader.Discover().loaded == (std::vector<std::string>{"duo", "app:duo"}));

    s.settings.SetBool("extensions.duo.enabled", true);
    s.settings.SetBool("apps.duo.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("duo") == ampify::ExtensionState::Running);
    CHECK(s.hosts.StateOf("app:duo") == ampify::ExtensionState::Running);

    // Stopping the extension leaves the app running.
    s.Request("stop duo\n");
    CHECK(s.hosts.StateOf("duo") == ampify::ExtensionState::Off);
    CHECK(s.hosts.StateOf("app:duo") == ampify::ExtensionState::Running);
    s.hosts.StopAll();
}

TEST_CASE(Loader_StartRequestLoadsAnElementInstalledAfterTheStart) {
    KitSetup s("start-new");
    CHECK(s.loader.Discover().loaded.empty());
    s.hosts.StartAll(s.settings);

    // ampify install, then ampify extension enable: the folder and the setting appear while Apple Music runs.
    WriteElement(s.root / "extensions", "fresh", {.id = "fresh"}, R"(Ampify.log("new");)");
    s.settings.SetBool("extensions.fresh.enabled", true);
    s.Request("start fresh\n");
    CHECK(s.hosts.StateOf("fresh") == ampify::ExtensionState::Running);
    CHECK(s.Logged("[fresh] new"));
    CHECK(s.Logged("Start requested: fresh"));

    // The same for an app, with its own target.
    WriteElement(s.root / "apps", "fresh", {.id = "fresh", .kind = "app"}, R"(Ampify.log("page");)");
    s.settings.SetBool("apps.fresh.enabled", true);
    s.Request("start app:fresh\n");
    CHECK(s.hosts.StateOf("app:fresh") == ampify::ExtensionState::Running);

    s.Request("stop fresh\nstop app:fresh\n");
    CHECK(s.hosts.StateOf("fresh") == ampify::ExtensionState::Off);
    CHECK(s.hosts.StateOf("app:fresh") == ampify::ExtensionState::Off);

    s.Request("start missing\n");
    CHECK(s.Logged("[missing] not found in Ampify"));
}

TEST_CASE(Loader_StartRequestKeepsADisabledElementOff) {
    KitSetup s("start-disabled");
    WriteElement(s.root / "extensions", "quiet", {.id = "quiet"}, R"(Ampify.log("x");)");
    s.loader.Discover();
    s.Request("start quiet\n");
    CHECK(s.hosts.StateOf("quiet") == ampify::ExtensionState::Off);
}

TEST_CASE(Loader_RescanAddsNewFoldersAndDropsRemovedOnes) {
    KitSetup s("rescan");
    WriteElement(s.root / "extensions", "old", {.id = "old"}, R"(Ampify.log("old");)");
    s.loader.Discover();
    s.settings.SetBool("extensions.old.enabled", true);
    s.settings.SetBool("apps.later.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("old") == ampify::ExtensionState::Running);

    fs::remove_all(s.root / "extensions" / "old");
    WriteElement(s.root / "apps", "later", {.id = "later", .kind = "app"}, R"(Ampify.log("arrived");)");
    s.Request("rescan\n");
    CHECK(!s.hosts.Has("old"));
    CHECK(s.Logged("Extension old stopped"));
    CHECK(s.Logged("[old] removed: its folder no longer exists"));
    CHECK(s.hosts.StateOf("app:later") == ampify::ExtensionState::Running);  // enabled, so started at once
    CHECK(s.loader.elements().size() == 1);

    // A second rescan changes nothing.
    ampify::LoadReport again = s.loader.Rescan(s.settings);
    CHECK(again.loaded.empty());
    CHECK(s.loader.elements().size() == 1);
    s.hosts.StopAll();
}

TEST_CASE(Loader_RestartReadsTheManifestAgain) {
    KitSetup s("restart-manifest");
    WriteElement(s.root / "extensions", "grow", {.id = "grow"},
                 R"(try { Ampify.storage.set("k", 1); Ampify.log("storage allowed"); } catch (e) { Ampify.log(e.name); })");
    s.loader.Discover();
    s.settings.SetBool("extensions.grow.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.Logged("[grow] PermissionDenied"));

    // ampify dev or ampify update rewrote the folder with a new permission.
    std::ofstream(s.root / "extensions" / "grow" / "manifest.json")
        << ampify::test::ManifestJson({.id = "grow", .permissions = R"("storage")", .processes = R"("ui")"});
    s.Request("restart grow\n");
    CHECK(s.hosts.StateOf("grow") == ampify::ExtensionState::Running);
    CHECK(s.Logged("[grow] storage allowed"));

    // A manifest broken by hand leaves the element stopped, with the reason in the journal.
    std::ofstream(s.root / "extensions" / "grow" / "manifest.json") << "{ casse";
    s.Request("restart grow\n");
    CHECK(!s.hosts.Has("grow"));
    CHECK(s.Logged("[grow] cannot reload"));
}

TEST_CASE(Loader_RestartOfANewFolderLoadsIt) {
    KitSetup s("restart-new");
    s.loader.Discover();
    WriteElement(s.root / "extensions", "dev", {.id = "dev"}, R"(Ampify.log("copied by dev");)");
    s.settings.SetBool("extensions.dev.enabled", true);
    CHECK(s.loader.Restart("dev", s.settings));
    CHECK(s.hosts.StateOf("dev") == ampify::ExtensionState::Running);
    s.hosts.StopAll();
}

TEST_CASE(Loader_RequestsReachANativeExtensionToo) {
    KitSetup s("native");
    s.loader.Discover();
    int starts = 0;
    s.hosts.Add({"atmos-leveler", "Atmos", false, [&] { return ++starts > 0; }, [] {}});
    s.settings.SetBool("extensions.atmos-leveler.enabled", true);
    s.Request("start atmos-leveler\n");
    CHECK(s.hosts.StateOf("atmos-leveler") == ampify::ExtensionState::Running);
    s.Request("stop atmos-leveler\n");
    CHECK(s.hosts.StateOf("atmos-leveler") == ampify::ExtensionState::Off);
    CHECK(starts == 1);
}
