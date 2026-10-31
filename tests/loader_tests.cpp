#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../core/extension.hpp"
#include "../core/extension_loader.hpp"
#include "../core/reload.hpp"
#include "../core/settings.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

// Writes one element folder under `root`. `manifest` is the full manifest.json text.
void WriteElement(const fs::path& root, const std::string& name, const std::string& manifest, const std::string& main) {
    fs::create_directories(root / name);
    std::ofstream(root / name / "manifest.json") << manifest;
    if (!main.empty()) std::ofstream(root / name / "main.js") << main;
}

std::string Manifest(const std::string& id, const std::string& kind, const std::string& extra = "") {
    return R"({"id":")" + id + R"(","name":"Essai","version":"1.0.0","kind":")" + kind +
           R"(","author":"Shirowwww","description":"Test.","kitApi":"1.0","main":"main.js")" + extra + "}";
}

fs::path Root(const std::string& name) {
    fs::path root = fs::temp_directory_path() / "ampify-tests" / ("loader-" + name);
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

}  // namespace

TEST_CASE(Loader_RegistersValidElementsAndSkipsTheRest) {
    fs::path root = Root("discover");
    WriteElement(root, "good", Manifest("good", "extension"), R"(Ampify.log("hello");)");
    WriteElement(root, "app-ok", Manifest("app-ok", "app"), R"(Ampify.log("app");)");
    WriteElement(root, "bad-id", Manifest("other", "extension"), R"(Ampify.log("x");)");  // id differs from folder
    WriteElement(root, "theme", Manifest("theme", "theme", R"(,"colors":"c.ini")"), "Ampify.log('x');");
    std::ofstream(root / "theme" / "c.ini") << "[Base]\n";
    fs::create_directories(root / "no-manifest");

    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost hosts(journal);
    ampify::ExtensionLoader loader(hosts, host, journal, root);

    ampify::LoadReport report = loader.Discover();
    CHECK(report.loaded == std::vector<std::string>{"good"});
    CHECK(report.rejected.size() == 3);
    CHECK(out.str().find("[bad-id] rejected") != std::string::npos);
    CHECK(out.str().find("type theme not loaded") != std::string::npos);
    CHECK(out.str().find("[app-ok] type app not loaded from the folder extensions") != std::string::npos);
    CHECK(hosts.StateOf("good") == ampify::ExtensionState::Off);  // disabled until the user enables it
}

TEST_CASE(Loader_SkipsElementsDeclaredForTheOtherProcess) {
    fs::path root = Root("processes");
    WriteElement(root, "uionly", Manifest("uionly", "extension", R"(,"processes":["ui"])"), R"(Ampify.log("ui");)");
    WriteElement(root, "anywhere", Manifest("anywhere", "extension", R"(,"processes":["ui","engine"])"),
                 R"(Ampify.log("ok");)");
    WriteElement(root, "plain", Manifest("plain", "extension"), R"(Ampify.log("ui");)");  // no processes: interface only

    ampify::test::FakeHost host;
    host.process = "engine";
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost hosts(journal);
    ampify::ExtensionLoader loader(hosts, host, journal, root);

    ampify::LoadReport report = loader.Discover();
    CHECK(report.loaded == std::vector<std::string>{"anywhere"});
    CHECK((report.skipped == std::vector<std::string>{"plain", "uionly"}));
    CHECK(out.str().find("[uionly] skipped in the process engine") != std::string::npos);
}

TEST_CASE(Loader_MissingRootIsNotAnError) {
    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost hosts(journal);
    ampify::ExtensionLoader loader(hosts, host, journal, Root("absent") / "nothing-here");
    CHECK(loader.Discover().loaded.empty());
}

TEST_CASE(Loader_EnabledElementsStartAndOneFailureDoesNotStopTheOthers) {
    fs::path root = Root("isolation");
    WriteElement(root, "solid", Manifest("solid", "extension"), R"(Ampify.log("solid");)");
    WriteElement(root, "broken", Manifest("broken", "extension"), R"(throw new Error("panne");)");

    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost hosts(journal);
    ampify::ExtensionLoader loader(hosts, host, journal, root);
    CHECK(loader.Discover().loaded.size() == 2);

    ampify::Settings settings;
    settings.SetBool("extensions.solid.enabled", true);
    settings.SetBool("extensions.broken.enabled", true);
    hosts.StartAll(settings);

    CHECK(hosts.StateOf("solid") == ampify::ExtensionState::Running);
    CHECK(hosts.StateOf("broken") == ampify::ExtensionState::Failed);
    CHECK(out.str().find("[solid] solid") != std::string::npos);

    hosts.StopAll();
    CHECK(hosts.StateOf("solid") == ampify::ExtensionState::Off);
}

TEST_CASE(Loader_TargetedClicksAndEventsReachOnlyTheirElement) {
    fs::path root = Root("targeted");
    WriteElement(root, "one", Manifest("one", "extension", R"(,"permissions":["ui.panel"])"),
                 R"(const { h, useState } = Ampify.React;
function P() {
  const [n, setN] = useState(0);
  return h("stack", null, h("text", null, "n=" + n),
    h("button", { label: "more", onClick: () => { setN(n + 1); Ampify.log("a click"); } }),
    h("toggle", { label: "t", onChange: (e) => Ampify.log("toggle " + e.value) }));
}
Ampify.ReactDOM.render(h(P), "panel:p");)");
    WriteElement(root, "two", Manifest("two", "extension"), R"(Ampify.log("two");)");

    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost hosts(journal);
    ampify::ExtensionLoader loader(hosts, host, journal, root);
    CHECK(loader.Discover().loaded.size() == 2);
    ampify::Settings settings;
    settings.SetBool("extensions.one.enabled", true);
    settings.SetBool("extensions.two.enabled", true);
    hosts.StartAll(settings);
    loader.Pump();

    std::optional<ampify::UiSlot> panel = host.Ui().Panel("one", "p");
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

    loader.DeliverClickTo("two", click);  // another element: nothing happens
    loader.Pump();
    CHECK(out.str().find("a click") == std::string::npos);

    loader.DeliverClickTo("one", click);
    loader.DeliverEventTo("one", "change", R"({"id":)" + std::to_string(change) + R"(,"value":true})");
    loader.Pump();
    CHECK(out.str().find("[one] a click") != std::string::npos);
    CHECK(out.str().find("[one] toggle true") != std::string::npos);
    std::optional<ampify::UiSlot> after = host.Ui().Panel("one", "p");
    CHECK(after && after->tree.Dump().find("n=1") != std::string::npos);
    hosts.StopAll();
}

TEST_CASE(Loader_AMenuEntryReceivesTheNameOfItsItem) {
    fs::path root = Root("menu-entries");
    WriteElement(root, "menus", Manifest("menus", "extension", R"(,"permissions":["ui.menu"])"),
                 R"(Ampify.ui.menu.addItem("context", { label: "Find lyrics", onClick: (item) => Ampify.log("lyrics of " + item.title) });
Ampify.ui.menu.addItem("app", { label: "About", onClick: (item) => Ampify.log("about " + JSON.stringify(item)) });)");

    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost hosts(journal);
    ampify::ExtensionLoader loader(hosts, host, journal, root);
    CHECK(loader.Discover().loaded.size() == 1);
    ampify::Settings settings;
    settings.SetBool("extensions.menus.enabled", true);
    hosts.StartAll(settings);
    loader.Pump();

    std::vector<ampify::UiControl> controls = host.Ui().Controls();
    CHECK(controls.size() == 2);
    if (controls.size() != 2) return;
    CHECK(controls[0].placement == "menu-context" && controls[1].placement == "menu-app");
    loader.DeliverEventTo("menus", "menuClick", R"({"id":)" + std::to_string(controls[0].id) + R"(,"title":"Merci"})");
    loader.DeliverEventTo("menus", "menuClick", R"({"id":)" + std::to_string(controls[1].id) + R"(,"title":""})");
    loader.DeliverEventTo("menus", "menuClick", "not json");
    loader.Pump();
    CHECK(out.str().find("[menus] lyrics of Merci") != std::string::npos);
    CHECK(out.str().find("[menus] about {}") != std::string::npos);
    CHECK(out.str().find("menuClick") != std::string::npos);  // the bad payload is reported, nothing runs
    hosts.StopAll();
}

TEST_CASE(Loader_ExposeApiOffKeepsEveryScriptOff) {
    fs::path root = Root("expose-api");
    WriteElement(root / "extensions", "ext", Manifest("ext", "extension"), R"(Ampify.log("ext");)");
    WriteElement(root / "apps", "page", Manifest("page", "app"), R"(Ampify.log("page");)");

    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost hosts(journal);
    ampify::ExtensionLoader loader(hosts, host, journal, root / "extensions");
    loader.SetAppRoot(root / "apps");
    CHECK(loader.Discover().loaded.size() == 2);

    ampify::Settings settings;
    settings.SetBool("extensions.ext.enabled", true);
    settings.SetBool("apps.page.enabled", true);  // the key `ampify app enable` writes
    settings.SetBool("expose_api", false);
    hosts.StartAll(settings);
    CHECK(hosts.StateOf("ext") == ampify::ExtensionState::Off);
    CHECK(hosts.StateOf("app:page") == ampify::ExtensionState::Off);

    settings.SetBool("expose_api", true);
    CHECK(loader.Restart("ext", settings));
    CHECK(loader.Restart("app:page", settings));
    CHECK(hosts.StateOf("ext") == ampify::ExtensionState::Running);
    CHECK(hosts.StateOf("app:page") == ampify::ExtensionState::Running);
    hosts.StopAll();
}

TEST_CASE(Loader_RestartReloadsAnElementWithItsNewSettings) {
    fs::path root = Root("restart");
    WriteElement(root, "reload", Manifest("reload", "extension"), R"(Ampify.log("loaded");)");

    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost hosts(journal);
    ampify::ExtensionLoader loader(hosts, host, journal, root);
    loader.Discover();

    ampify::Settings settings;
    settings.SetBool("extensions.reload.enabled", true);
    hosts.StartAll(settings);
    CHECK(hosts.StateOf("reload") == ampify::ExtensionState::Running);

    settings.SetBool("extensions.reload.enabled", false);
    CHECK(loader.Restart("reload", settings));
    CHECK(hosts.StateOf("reload") == ampify::ExtensionState::Off);
    CHECK(!loader.Restart("unknown", settings));
}

TEST_CASE(Loader_PlayerEventsAndKeysReachOnlyTheRightRunningElements) {
    fs::path root = Root("events");
    WriteElement(root, "listener", Manifest("listener", "extension", R"(,"permissions":["player.read","ui.keyboard"])"),
                 R"(Ampify.player.on("statechange", (s) => Ampify.log("state " + s.playing));
                    Ampify.keyboard.bind("F5", () => Ampify.log("key"));)");
    WriteElement(root, "idle", Manifest("idle", "extension"), R"(Ampify.log("nothing");)");

    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    ampify::ExtensionHost hosts(journal);
    ampify::ExtensionLoader loader(hosts, host, journal, root);
    loader.Discover();

    ampify::Settings settings;
    settings.SetBool("extensions.listener.enabled", true);
    hosts.StartAll(settings);  // "idle" stays off

    ampify::PlayerSnapshot snapshot;
    snapshot.playing = true;
    loader.DeliverPlayer("statechange", snapshot);
    loader.DeliverKey("listener", "F5");
    loader.DeliverKey("idle", "F5");  // not its key, and it is off
    loader.Pump();

    CHECK(out.str().find("[listener] state true") != std::string::npos);
    CHECK(out.str().find("[listener] key") != std::string::npos);
    CHECK(out.str().find("[idle] nothing") == std::string::npos);
    hosts.StopAll();
    CHECK(host.keys.empty());
}

namespace {

// A kit with its folders, a loader that knows them, and settings to switch the elements.
struct KitSetup {
    fs::path root;
    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};
    ampify::ExtensionHost hosts{journal};
    ampify::ExtensionLoader loader;
    ampify::Settings settings;

    explicit KitSetup(const std::string& name) : root(Root(name)), loader(hosts, host, journal, root / "extensions") {
        host.process = "ui";
        fs::create_directories(root / "extensions");
        fs::create_directories(root / "apps");
        loader.SetAppRoot(root / "apps");
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
    void Request(const std::string& text) { loader.Apply(ampify::ParseReloadRequests(text), settings); }
};

}  // namespace

TEST_CASE(Loader_DiscoversAppsInTheirOwnFolder) {
    KitSetup s("apps-folder");
    WriteElement(s.root / "apps", "page", Manifest("page", "app"), R"(Ampify.log("page opened");)");
    WriteElement(s.root / "apps", "wrong", Manifest("wrong", "extension"), R"(Ampify.log("x");)");
    WriteElement(s.root / "apps", ".page.previous", Manifest("page", "app"), R"(Ampify.log("x");)");  // work in progress

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
    WriteElement(s.root / "extensions", "duo", Manifest("duo", "extension"), R"(Ampify.log("extension");)");
    WriteElement(s.root / "apps", "duo", Manifest("duo", "app"), R"(Ampify.log("app");)");
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
    WriteElement(s.root / "extensions", "fresh", Manifest("fresh", "extension"), R"(Ampify.log("new");)");
    s.settings.SetBool("extensions.fresh.enabled", true);
    s.Request("start fresh\n");
    CHECK(s.hosts.StateOf("fresh") == ampify::ExtensionState::Running);
    CHECK(s.Logged("[fresh] new"));
    CHECK(s.Logged("Start requested: fresh"));

    // The same for an app, with its own target.
    WriteElement(s.root / "apps", "fresh", Manifest("fresh", "app"), R"(Ampify.log("page");)");
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
    WriteElement(s.root / "extensions", "quiet", Manifest("quiet", "extension"), R"(Ampify.log("x");)");
    s.loader.Discover();
    s.Request("start quiet\n");
    CHECK(s.hosts.StateOf("quiet") == ampify::ExtensionState::Off);
}

TEST_CASE(Loader_RescanAddsNewFoldersAndDropsRemovedOnes) {
    KitSetup s("rescan");
    WriteElement(s.root / "extensions", "old", Manifest("old", "extension"), R"(Ampify.log("old");)");
    s.loader.Discover();
    s.settings.SetBool("extensions.old.enabled", true);
    s.settings.SetBool("apps.later.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("old") == ampify::ExtensionState::Running);

    fs::remove_all(s.root / "extensions" / "old");
    WriteElement(s.root / "apps", "later", Manifest("later", "app"), R"(Ampify.log("arrived");)");
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
    WriteElement(s.root / "extensions", "grow", Manifest("grow", "extension"),
                 R"(try { Ampify.storage.set("k", 1); Ampify.log("storage allowed"); } catch (e) { Ampify.log(e.name); })");
    s.loader.Discover();
    s.settings.SetBool("extensions.grow.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.Logged("[grow] PermissionDenied"));

    // ampify dev or ampify update rewrote the folder with a new permission.
    std::ofstream(s.root / "extensions" / "grow" / "manifest.json")
        << Manifest("grow", "extension", R"(,"permissions":["storage"],"processes":["ui"])");
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
    WriteElement(s.root / "extensions", "dev", Manifest("dev", "extension"), R"(Ampify.log("copied by dev");)");
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
