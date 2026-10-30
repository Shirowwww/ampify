#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../core/extension.hpp"
#include "../core/extension_loader.hpp"
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
    CHECK(report.loaded.size() == 2);
    CHECK(report.loaded[0] == "app-ok");
    CHECK(report.loaded[1] == "good");
    CHECK(report.rejected.size() == 2);
    CHECK(out.str().find("[bad-id] rejeté") != std::string::npos);
    CHECK(out.str().find("type theme non chargé") != std::string::npos);
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
    WriteElement(root, "ext", Manifest("ext", "extension"), R"(AMK.log("ext");)");
    WriteElement(root, "page", Manifest("page", "app"), R"(AMK.log("page");)");

    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal(out, [] { return std::string("t"); });
    amk::ExtensionHost hosts(journal);
    amk::ExtensionLoader loader(hosts, host, journal, root);
    CHECK(loader.Discover().loaded.size() == 2);

    amk::Settings settings;
    settings.SetBool("extensions.ext.enabled", true);
    settings.SetBool("apps.page.enabled", true);  // the key `amk app enable` writes
    settings.SetBool("expose_api", false);
    hosts.StartAll(settings);
    CHECK(hosts.StateOf("ext") == amk::ExtensionState::Off);
    CHECK(hosts.StateOf("page") == amk::ExtensionState::Off);

    settings.SetBool("expose_api", true);
    CHECK(loader.Restart("ext", settings));
    CHECK(loader.Restart("page", settings));
    CHECK(hosts.StateOf("ext") == amk::ExtensionState::Running);
    CHECK(hosts.StateOf("page") == amk::ExtensionState::Running);
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
