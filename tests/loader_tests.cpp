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
    fs::path root = fs::temp_directory_path() / "amk-tests" / ("loader-" + name);
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

}  // namespace

TEST_CASE(Loader_RegistersValidElementsAndSkipsTheRest) {
    fs::path root = Root("discover");
    WriteElement(root, "good", Manifest("good", "extension"), R"(AMK.log("bonjour");)");
    WriteElement(root, "app-ok", Manifest("app-ok", "app"), R"(AMK.log("app");)");
    WriteElement(root, "bad-id", Manifest("other", "extension"), R"(AMK.log("x");)");  // id differs from folder
    WriteElement(root, "theme", Manifest("theme", "theme", R"(,"colors":"c.ini")"), "AMK.log('x');");
    std::ofstream(root / "theme" / "c.ini") << "[Base]\n";
    fs::create_directories(root / "no-manifest");

    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal(out, [] { return std::string("t"); });
    amk::ExtensionHost hosts(journal);
    amk::ExtensionLoader loader(hosts, host, journal, root);

    amk::LoadReport report = loader.Discover();
    CHECK(report.loaded == std::vector<std::string>{"good"});
    CHECK(report.rejected.size() == 3);
    CHECK(out.str().find("[bad-id] rejeté") != std::string::npos);
    CHECK(out.str().find("type theme non chargé") != std::string::npos);
    CHECK(out.str().find("[app-ok] type app non chargé depuis le dossier extensions") != std::string::npos);
    CHECK(hosts.StateOf("good") == amk::ExtensionState::Off);  // disabled until the user enables it
}

TEST_CASE(Loader_SkipsElementsDeclaredForTheOtherProcess) {
    fs::path root = Root("processes");
    WriteElement(root, "uionly", Manifest("uionly", "extension", R"(,"processes":["ui"])"), R"(AMK.log("ui");)");
    WriteElement(root, "anywhere", Manifest("anywhere", "extension", R"(,"processes":["ui","engine"])"),
                 R"(AMK.log("ok");)");
    WriteElement(root, "plain", Manifest("plain", "extension"), R"(AMK.log("ui");)");  // no processes: interface only

    amk::test::FakeHost host;
    host.process = "engine";
    std::ostringstream out;
    amk::Journal journal(out, [] { return std::string("t"); });
    amk::ExtensionHost hosts(journal);
    amk::ExtensionLoader loader(hosts, host, journal, root);

    amk::LoadReport report = loader.Discover();
    CHECK(report.loaded == std::vector<std::string>{"anywhere"});
    CHECK((report.skipped == std::vector<std::string>{"plain", "uionly"}));
    CHECK(out.str().find("[uionly] ignoré dans le processus engine") != std::string::npos);
}

TEST_CASE(Loader_MissingRootIsNotAnError) {
    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal(out, [] { return std::string("t"); });
    amk::ExtensionHost hosts(journal);
    amk::ExtensionLoader loader(hosts, host, journal, Root("absent") / "nothing-here");
    CHECK(loader.Discover().loaded.empty());
}

TEST_CASE(Loader_EnabledElementsStartAndOneFailureDoesNotStopTheOthers) {
    fs::path root = Root("isolation");
    WriteElement(root, "solid", Manifest("solid", "extension"), R"(AMK.log("solide");)");
    WriteElement(root, "broken", Manifest("broken", "extension"), R"(throw new Error("panne");)");

    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal(out, [] { return std::string("t"); });
    amk::ExtensionHost hosts(journal);
    amk::ExtensionLoader loader(hosts, host, journal, root);
    CHECK(loader.Discover().loaded.size() == 2);

    amk::Settings settings;
    settings.SetBool("extensions.solid.enabled", true);
    settings.SetBool("extensions.broken.enabled", true);
    hosts.StartAll(settings);

    CHECK(hosts.StateOf("solid") == amk::ExtensionState::Running);
    CHECK(hosts.StateOf("broken") == amk::ExtensionState::Failed);
    CHECK(out.str().find("[solid] solide") != std::string::npos);

    hosts.StopAll();
    CHECK(hosts.StateOf("solid") == amk::ExtensionState::Off);
}

TEST_CASE(Loader_ExposeApiOffKeepsEveryScriptOff) {
    fs::path root = Root("expose-api");
    WriteElement(root / "extensions", "ext", Manifest("ext", "extension"), R"(AMK.log("ext");)");
    WriteElement(root / "apps", "page", Manifest("page", "app"), R"(AMK.log("page");)");

    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal(out, [] { return std::string("t"); });
    amk::ExtensionHost hosts(journal);
    amk::ExtensionLoader loader(hosts, host, journal, root / "extensions");
    loader.SetAppRoot(root / "apps");
    CHECK(loader.Discover().loaded.size() == 2);

    amk::Settings settings;
    settings.SetBool("extensions.ext.enabled", true);
    settings.SetBool("apps.page.enabled", true);  // the key `amk app enable` writes
    settings.SetBool("expose_api", false);
    hosts.StartAll(settings);
    CHECK(hosts.StateOf("ext") == amk::ExtensionState::Off);
    CHECK(hosts.StateOf("app:page") == amk::ExtensionState::Off);

    settings.SetBool("expose_api", true);
    CHECK(loader.Restart("ext", settings));
    CHECK(loader.Restart("app:page", settings));
    CHECK(hosts.StateOf("ext") == amk::ExtensionState::Running);
    CHECK(hosts.StateOf("app:page") == amk::ExtensionState::Running);
    hosts.StopAll();
}

TEST_CASE(Loader_RestartReloadsAnElementWithItsNewSettings) {
    fs::path root = Root("restart");
    WriteElement(root, "reload", Manifest("reload", "extension"), R"(AMK.log("chargé");)");

    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal(out, [] { return std::string("t"); });
    amk::ExtensionHost hosts(journal);
    amk::ExtensionLoader loader(hosts, host, journal, root);
    loader.Discover();

    amk::Settings settings;
    settings.SetBool("extensions.reload.enabled", true);
    hosts.StartAll(settings);
    CHECK(hosts.StateOf("reload") == amk::ExtensionState::Running);

    settings.SetBool("extensions.reload.enabled", false);
    CHECK(loader.Restart("reload", settings));
    CHECK(hosts.StateOf("reload") == amk::ExtensionState::Off);
    CHECK(!loader.Restart("unknown", settings));
}

TEST_CASE(Loader_PlayerEventsAndKeysReachOnlyTheRightRunningElements) {
    fs::path root = Root("events");
    WriteElement(root, "listener", Manifest("listener", "extension", R"(,"permissions":["player.read","ui.keyboard"])"),
                 R"(AMK.player.on("statechange", (s) => AMK.log("état " + s.playing));
                    AMK.keyboard.bind("F5", () => AMK.log("touche"));)");
    WriteElement(root, "idle", Manifest("idle", "extension"), R"(AMK.log("rien");)");

    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal(out, [] { return std::string("t"); });
    amk::ExtensionHost hosts(journal);
    amk::ExtensionLoader loader(hosts, host, journal, root);
    loader.Discover();

    amk::Settings settings;
    settings.SetBool("extensions.listener.enabled", true);
    hosts.StartAll(settings);  // "idle" stays off

    amk::PlayerSnapshot snapshot;
    snapshot.playing = true;
    loader.DeliverPlayer("statechange", snapshot);
    loader.DeliverKey("listener", "F5");
    loader.DeliverKey("idle", "F5");  // not its key, and it is off
    loader.Pump();

    CHECK(out.str().find("[listener] état true") != std::string::npos);
    CHECK(out.str().find("[listener] touche") != std::string::npos);
    CHECK(out.str().find("[idle] rien") == std::string::npos);
    hosts.StopAll();
    CHECK(host.keys.empty());
}

namespace {

// A kit with its folders, a loader that knows them, and settings to switch the elements.
struct KitSetup {
    fs::path root;
    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal{out, [] { return std::string("t"); }};
    amk::ExtensionHost hosts{journal};
    amk::ExtensionLoader loader;
    amk::Settings settings;

    explicit KitSetup(const std::string& name) : root(Root(name)), loader(hosts, host, journal, root / "extensions") {
        host.process = "ui";
        fs::create_directories(root / "extensions");
        fs::create_directories(root / "apps");
        loader.SetAppRoot(root / "apps");
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
    void Request(const std::string& text) { loader.Apply(amk::ParseReloadRequests(text), settings); }
};

}  // namespace

TEST_CASE(Loader_DiscoversAppsInTheirOwnFolder) {
    KitSetup s("apps-folder");
    WriteElement(s.root / "apps", "page", Manifest("page", "app"), R"(AMK.log("page ouverte");)");
    WriteElement(s.root / "apps", "wrong", Manifest("wrong", "extension"), R"(AMK.log("x");)");
    WriteElement(s.root / "apps", ".page.previous", Manifest("page", "app"), R"(AMK.log("x");)");  // work in progress

    amk::LoadReport report = s.loader.Discover();
    CHECK(report.loaded == std::vector<std::string>{"app:page"});
    CHECK(report.rejected == std::vector<std::string>{"wrong"});
    CHECK(s.Logged("[wrong] type extension non chargé depuis le dossier apps"));

    // apps.<id>.enabled switches an app, not extensions.<id>.enabled.
    s.settings.SetBool("extensions.page.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("app:page") == amk::ExtensionState::Off);
    s.settings.SetBool("apps.page.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("app:page") == amk::ExtensionState::Running);
    CHECK(s.Logged("[page] page ouverte"));
    s.hosts.StopAll();
}

TEST_CASE(Loader_AnAppAndAnExtensionMayShareAnId) {
    KitSetup s("shared-id");
    WriteElement(s.root / "extensions", "duo", Manifest("duo", "extension"), R"(AMK.log("extension");)");
    WriteElement(s.root / "apps", "duo", Manifest("duo", "app"), R"(AMK.log("app");)");
    CHECK(s.loader.Discover().loaded == (std::vector<std::string>{"duo", "app:duo"}));

    s.settings.SetBool("extensions.duo.enabled", true);
    s.settings.SetBool("apps.duo.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("duo") == amk::ExtensionState::Running);
    CHECK(s.hosts.StateOf("app:duo") == amk::ExtensionState::Running);

    // Stopping the extension leaves the app running.
    s.Request("stop duo\n");
    CHECK(s.hosts.StateOf("duo") == amk::ExtensionState::Off);
    CHECK(s.hosts.StateOf("app:duo") == amk::ExtensionState::Running);
    s.hosts.StopAll();
}

TEST_CASE(Loader_StartRequestLoadsAnElementInstalledAfterTheStart) {
    KitSetup s("start-new");
    CHECK(s.loader.Discover().loaded.empty());
    s.hosts.StartAll(s.settings);

    // amk install, then amk extension enable: the folder and the setting appear while Apple Music runs.
    WriteElement(s.root / "extensions", "fresh", Manifest("fresh", "extension"), R"(AMK.log("nouvelle");)");
    s.settings.SetBool("extensions.fresh.enabled", true);
    s.Request("start fresh\n");
    CHECK(s.hosts.StateOf("fresh") == amk::ExtensionState::Running);
    CHECK(s.Logged("[fresh] nouvelle"));
    CHECK(s.Logged("Démarrage demandé : fresh"));

    // The same for an app, with its own target.
    WriteElement(s.root / "apps", "fresh", Manifest("fresh", "app"), R"(AMK.log("page");)");
    s.settings.SetBool("apps.fresh.enabled", true);
    s.Request("start app:fresh\n");
    CHECK(s.hosts.StateOf("app:fresh") == amk::ExtensionState::Running);

    s.Request("stop fresh\nstop app:fresh\n");
    CHECK(s.hosts.StateOf("fresh") == amk::ExtensionState::Off);
    CHECK(s.hosts.StateOf("app:fresh") == amk::ExtensionState::Off);

    s.Request("start missing\n");
    CHECK(s.Logged("[missing] introuvable dans le kit"));
}

TEST_CASE(Loader_StartRequestKeepsADisabledElementOff) {
    KitSetup s("start-disabled");
    WriteElement(s.root / "extensions", "quiet", Manifest("quiet", "extension"), R"(AMK.log("x");)");
    s.loader.Discover();
    s.Request("start quiet\n");
    CHECK(s.hosts.StateOf("quiet") == amk::ExtensionState::Off);
}

TEST_CASE(Loader_RescanAddsNewFoldersAndDropsRemovedOnes) {
    KitSetup s("rescan");
    WriteElement(s.root / "extensions", "old", Manifest("old", "extension"), R"(AMK.log("ancienne");)");
    s.loader.Discover();
    s.settings.SetBool("extensions.old.enabled", true);
    s.settings.SetBool("apps.later.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.hosts.StateOf("old") == amk::ExtensionState::Running);

    fs::remove_all(s.root / "extensions" / "old");
    WriteElement(s.root / "apps", "later", Manifest("later", "app"), R"(AMK.log("arrivée");)");
    s.Request("rescan\n");
    CHECK(!s.hosts.Has("old"));
    CHECK(s.Logged("Extension old arrêtée"));
    CHECK(s.Logged("[old] retiré : son dossier n'existe plus"));
    CHECK(s.hosts.StateOf("app:later") == amk::ExtensionState::Running);  // enabled, so started at once
    CHECK(s.loader.elements().size() == 1);

    // A second rescan changes nothing.
    amk::LoadReport again = s.loader.Rescan(s.settings);
    CHECK(again.loaded.empty());
    CHECK(s.loader.elements().size() == 1);
    s.hosts.StopAll();
}

TEST_CASE(Loader_RestartReadsTheManifestAgain) {
    KitSetup s("restart-manifest");
    WriteElement(s.root / "extensions", "grow", Manifest("grow", "extension"),
                 R"(try { AMK.storage.set("k", 1); AMK.log("stockage permis"); } catch (e) { AMK.log(e.name); })");
    s.loader.Discover();
    s.settings.SetBool("extensions.grow.enabled", true);
    s.hosts.StartAll(s.settings);
    CHECK(s.Logged("[grow] PermissionDenied"));

    // amk dev or amk update rewrote the folder with a new permission.
    std::ofstream(s.root / "extensions" / "grow" / "manifest.json")
        << Manifest("grow", "extension", R"(,"permissions":["storage"],"processes":["ui"])");
    s.Request("restart grow\n");
    CHECK(s.hosts.StateOf("grow") == amk::ExtensionState::Running);
    CHECK(s.Logged("[grow] stockage permis"));

    // A manifest broken by hand leaves the element stopped, with the reason in the journal.
    std::ofstream(s.root / "extensions" / "grow" / "manifest.json") << "{ casse";
    s.Request("restart grow\n");
    CHECK(!s.hosts.Has("grow"));
    CHECK(s.Logged("[grow] rechargement impossible"));
}

TEST_CASE(Loader_RestartOfANewFolderLoadsIt) {
    KitSetup s("restart-new");
    s.loader.Discover();
    WriteElement(s.root / "extensions", "dev", Manifest("dev", "extension"), R"(AMK.log("copié par dev");)");
    s.settings.SetBool("extensions.dev.enabled", true);
    CHECK(s.loader.Restart("dev", s.settings));
    CHECK(s.hosts.StateOf("dev") == amk::ExtensionState::Running);
    s.hosts.StopAll();
}

TEST_CASE(Loader_RequestsReachANativeExtensionToo) {
    KitSetup s("native");
    s.loader.Discover();
    int starts = 0;
    s.hosts.Add({"atmos-leveler", "Atmos", false, [&] { return ++starts > 0; }, [] {}});
    s.settings.SetBool("extensions.atmos-leveler.enabled", true);
    s.Request("start atmos-leveler\n");
    CHECK(s.hosts.StateOf("atmos-leveler") == amk::ExtensionState::Running);
    s.Request("stop atmos-leveler\n");
    CHECK(s.hosts.StateOf("atmos-leveler") == amk::ExtensionState::Off);
    CHECK(starts == 1);
}
