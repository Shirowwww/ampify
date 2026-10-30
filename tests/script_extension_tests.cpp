#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../core/manifest.hpp"
#include "../core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

#ifndef AMK_SOURCE_DIR
#define AMK_SOURCE_DIR "."
#endif

namespace {

namespace fs = std::filesystem;

fs::path ScratchDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "amk-tests" / ("element-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string Manifest(const std::string& id, const std::string& permissions) {
    return R"({"id":")" + id + R"(","name":"Démo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
           R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
}

// Everything an element needs in one place: a fake host, a journal in a string, and a clock the test moves.
struct Fixture {
    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal{out, [] { return std::string("t"); }};
    std::chrono::steady_clock::time_point now{std::chrono::seconds(1000)};

    amk::Clock clock() {
        return [this] { return now; };
    }

    // Writes the element's folder and returns the element. Each test uses its own id, so folders never clash.
    std::unique_ptr<amk::ScriptExtension> Make(const std::string& id, const std::string& permissions,
                                               const std::string& main, amk::js::Limits limits = {}) {
        fs::path dir = ScratchDir(id);
        std::ofstream(dir / "manifest.json") << Manifest(id, permissions);
        std::ofstream(dir / "main.js") << main;
        amk::ManifestResult result = amk::ParseManifest(Manifest(id, permissions), id, 1, [](const std::string&) {
            return true;
        });
        return std::make_unique<amk::ScriptExtension>(*result.manifest, dir, host, journal, clock(), limits);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

}  // namespace

TEST_CASE(Element_RunsMainAndLogsWithItsId) {
    Fixture f;
    auto element = f.Make("demo", "", R"(AMK.log("bonjour"); AMK.log(AMK.process + " " + AMK.locale);)");
    f.host.process = "ui";
    CHECK(element->Start());
    CHECK(element->running());
    CHECK(f.Logged("[demo] bonjour"));
    CHECK(f.Logged("[demo] ui fr-FR"));
    element->Stop();
    CHECK(!element->running());
}

TEST_CASE(Element_RejectsStorageWithoutThePermission) {
    Fixture f;
    auto element = f.Make("nostore", "", R"(AMK.storage.set("a", 1);)");
    CHECK(!element->Start());
    CHECK(element->lastError().find("PermissionDenied") != std::string::npos);
    CHECK(element->lastError().find("storage") != std::string::npos);
    CHECK(!element->running());
}

TEST_CASE(Element_StorageSurvivesARestart) {
    Fixture f;
    const std::string main =
        R"(const n = AMK.storage.get("launches", 0) + 1; AMK.storage.set("launches", n); AMK.log("lancement " + n);)";
    auto first = f.Make("counter", R"("storage")", main);
    CHECK(first->Start());
    first->Stop();
    CHECK(f.host.storage.count("counter") == 1);
    CHECK(f.host.storage["counter"] == R"({"launches":1})");

    auto second = f.Make("counter", R"("storage")", main);
    CHECK(second->Start());
    CHECK(f.Logged("lancement 2"));
}

TEST_CASE(Element_ConfigReadsTypedValuesAndWritesOnlyItsOwnKeys) {
    Fixture f;
    f.host.settings["extensions.cfg.volume"] = "7";
    auto element = f.Make("cfg", "", R"(
        AMK.log("v=" + (AMK.config.get("volume", 5) + 1));
        AMK.log("d=" + AMK.config.get("absent", 2.5));
        AMK.config.set("on", true);
        try { AMK.config.set("enabled", true); } catch (e) { AMK.log("refus " + e.name); }
    )");
    CHECK(element->Start());
    CHECK(f.Logged("v=8"));
    CHECK(f.Logged("d=2.5"));
    CHECK(f.Logged("refus TypeError"));
    CHECK(f.host.settings["extensions.cfg.on"] == "true");
    CHECK(f.host.settings.count("extensions.cfg.enabled") == 0);
}

TEST_CASE(Element_RunsOnStopHandlersInReverseOrder) {
    Fixture f;
    auto element = f.Make("order", "", R"(AMK.onStop(() => AMK.log("un")); AMK.onStop(() => AMK.log("deux"));)");
    CHECK(element->Start());
    element->Stop();
    std::string log = f.out.str();
    CHECK(log.find("deux") != std::string::npos);
    CHECK(log.find("deux") < log.find("un", log.find("deux")));
}

TEST_CASE(Element_CannotReplaceTheFrozenApi) {
    Fixture f;
    auto element = f.Make("frozen", "", R"(AMK.version = "9.9"; AMK.log(AMK.version);)");
    CHECK(element->Start());
    CHECK(f.Logged(std::string("[frozen] ") + amk::kKernelVersion));
}

TEST_CASE(Element_TimersFireWhenDueAndCanBeCleared) {
    Fixture f;
    auto element = f.Make("timers", "", R"(
        setTimeout(() => AMK.log("tic"), 100);
        let n = 0;
        const id = setInterval(() => { n++; AMK.log("t" + n); if (n === 2) clearInterval(id); }, 10);
    )");
    CHECK(element->Start());
    f.now += std::chrono::milliseconds(50);
    element->Pump();
    CHECK(f.Logged("t1"));
    CHECK(!f.Logged("tic"));
    f.now += std::chrono::milliseconds(20);
    element->Pump();
    CHECK(f.Logged("t2"));
    f.now += std::chrono::milliseconds(200);
    element->Pump();
    CHECK(f.Logged("tic"));
    CHECK(!f.Logged("t3"));
}

TEST_CASE(Element_ThrowingTimerDoesNotStopTheOthers) {
    Fixture f;
    auto element = f.Make("boom", "", R"(
        setTimeout(() => { throw new Error("boum"); }, 10);
        setTimeout(() => AMK.log("autre"), 10);
    )");
    CHECK(element->Start());
    f.now += std::chrono::milliseconds(20);
    element->Pump();
    CHECK(f.Logged("boum"));
    CHECK(f.Logged("autre"));
    CHECK(element->running());
}

TEST_CASE(Element_PlayerStateNeedsPlayerRead) {
    Fixture f;
    f.host.player.playing = true;
    amk::TrackInfo track;
    track.title = "Titre";
    f.host.player.track = track;
    auto denied = f.Make("noread", "", R"(try { AMK.player.state(); } catch (e) { AMK.log(e.name); })");
    CHECK(denied->Start());
    CHECK(f.Logged("PermissionDenied"));

    auto element = f.Make("read", R"("player.read")", R"(AMK.log(AMK.player.state().track.title);)");
    CHECK(element->Start());
    CHECK(f.Logged("[read] Titre"));
}

TEST_CASE(Element_PlayerEventsArriveOnThePumpNotOnTheCallingThread) {
    Fixture f;
    auto element = f.Make("events", R"("player.read")", R"(AMK.player.on("trackchange", (s) => AMK.log("piste " + s.track.title));)");
    CHECK(element->Start());

    amk::PlayerSnapshot snapshot;
    amk::TrackInfo track;
    track.title = "Nouveau";
    snapshot.track = track;
    element->DeliverPlayer("trackchange", snapshot);
    CHECK(!f.Logged("piste Nouveau"));
    element->Pump();
    CHECK(f.Logged("piste Nouveau"));
}

TEST_CASE(Element_PlayerCommandsNeedPlayerControl) {
    Fixture f;
    auto denied = f.Make("nocontrol", "", R"(try { AMK.player.next(); } catch (e) { AMK.log(e.name); })");
    CHECK(denied->Start());
    CHECK(f.host.commands.empty());

    auto element = f.Make("control", R"("player.control")", R"(AMK.player.next(); AMK.player.seek(1500);)");
    CHECK(element->Start());
    CHECK(f.host.commands.size() == 2);
    CHECK(f.host.commands[0] == amk::PlayerCommand::Next);
    CHECK(f.host.commands[1] == amk::PlayerCommand::Seek);
}

TEST_CASE(Element_GainIsClampedToTheKernelCeilingAndResetOnStop) {
    Fixture f;
    f.host.settings["audio.maxGainDb"] = "3";
    auto element = f.Make("gain", R"("audio.gain")", R"(AMK.log("gain=" + AMK.audio.setGainDb(20));)");
    CHECK(element->Start());
    CHECK(f.Logged("gain=3"));
    CHECK(f.host.gains.back().first == "gain");
    CHECK(f.host.gains.back().second == 3.0);
    element->Stop();
    CHECK(f.host.gains.back().second == 0.0);
}

TEST_CASE(Element_NotifyNeedsPermissionAndTruncates) {
    Fixture f;
    std::string text(600, 'x');
    std::string main = std::string(R"(AMK.notify({ title: "Salut", text: ")") + text + R"(" });)";
    auto element = f.Make("note", R"("notifications")", main);
    CHECK(element->Start());
    CHECK(f.host.notifications.size() == 1);
    CHECK(f.host.notifications[0].size() == std::string("Salut|").size() + 500);
    CHECK(f.host.notifications[0] == "Salut|" + std::string(500, 'x'));

    auto denied = f.Make("nonote", "", R"(AMK.notify({ title: "x" });)");
    CHECK(!denied->Start());
}

TEST_CASE(Element_KeyboardBindingIsNormalizedAndDelivered) {
    Fixture f;
    auto element = f.Make("keys", R"("ui.keyboard")", R"(
        AMK.keyboard.bind("ctrl+alt+right", () => AMK.log("suivant"));
        try { AMK.keyboard.bind("a", () => {}); } catch (e) { AMK.log("refus " + e.name); }
    )");
    CHECK(element->Start());
    CHECK(f.host.keys.count("Ctrl+Alt+Right") == 1);
    CHECK(f.Logged("refus RangeError"));
    element->DeliverKey("Ctrl+Alt+Right");
    element->Pump();
    CHECK(f.Logged("suivant"));
    element->Stop();
    CHECK(f.host.keys.empty());
}

TEST_CASE(Element_HttpReachesOnlyDeclaredHttpsHosts) {
    Fixture f;
    auto element = f.Make("net", R"("network:api.example.org")", R"(
        AMK.http.get("https://api.example.org/x").then((r) => AMK.log("code " + r.status + " " + r.text));
        AMK.http.get("https://evil.example/").catch((e) => AMK.log("refus " + e.name));
        AMK.http.get("http://api.example.org/").catch((e) => AMK.log("clair " + e.name));
    )");
    CHECK(element->Start());
    CHECK(f.Logged("code 200 ok"));
    CHECK(f.Logged("refus PermissionDenied"));
    CHECK(f.Logged("clair TypeError"));
    CHECK(f.host.requestedUrls.size() == 1);
}

TEST_CASE(Element_UriParsesAndBuildsLinks) {
    Fixture f;
    auto element = f.Make("uri", "", R"(
        AMK.log(JSON.stringify(AMK.uri.parse("https://music.apple.com/fr/album/nom/123?i=456")));
        AMK.log(AMK.uri.build("album", "123", { storefront: "fr" }));
        AMK.log(String(AMK.uri.parse("https://example.org/x")));
    )");
    CHECK(element->Start());
    CHECK(f.Logged(R"({"kind":"album","storefront":"fr","id":"123","trackId":"456"})"));
    CHECK(f.Logged("https://music.apple.com/fr/album/_/123"));
    CHECK(f.Logged("[uri] null"));
}

TEST_CASE(Element_IconsReturnSvgAndRejectUnknownNames) {
    Fixture f;
    auto element = f.Make("icons", "", R"(
        AMK.log(AMK.icons.get("play").slice(0, 4));
        try { AMK.icons.get("inconnue"); } catch (e) { AMK.log(e.name); }
    )");
    CHECK(element->Start());
    CHECK(f.Logged("[icons] <svg"));
    CHECK(f.Logged("RangeError"));
}

TEST_CASE(Element_FailsCleanlyWhenMainThrows) {
    Fixture f;
    auto element = f.Make("throws", "", R"(throw new Error("boum au démarrage");)");
    CHECK(!element->Start());
    CHECK(element->lastError().find("boum au démarrage") != std::string::npos);
    CHECK(!element->running());
}

TEST_CASE(Element_IsCutWhenItLoopsForever) {
    Fixture f;
    amk::js::Limits limits;
    limits.budget = std::chrono::milliseconds(50);
    auto element = f.Make("spin", "", "while (true) {}", limits);
    CHECK(!element->Start());
    CHECK(element->lastError().find("budget") != std::string::npos);
}

TEST_CASE(Element_ExamplesStartWithTheirOwnPermissions) {
    for (const char* name : {"hello-extension", "now-playing-notify", "keyboard-skip", "sleep-timer", "gain-boost"}) {
        Fixture f;
        fs::path dir = fs::path(AMK_SOURCE_DIR) / "examples" / name;
        std::ifstream manifestFile(dir / "manifest.json");
        std::stringstream manifestText;
        manifestText << manifestFile.rdbuf();
        amk::ManifestResult result = amk::ParseManifest(manifestText.str(), name, 1, [&](const std::string& file) {
            return fs::exists(dir / file);
        });
        CHECK(result.manifest.has_value());
        if (!result.manifest) continue;

        amk::ScriptExtension element(*result.manifest, dir, f.host, f.journal, f.clock());
        CHECK(element.Start());
        element.Stop();
        CHECK(!element.running());
    }
}

TEST_CASE(Element_MainMayBeAModuleThatImportsItsOwnFiles) {
    fs::path dir = ScratchDir("module-main");
    std::string manifest = R"({"id":"module-main","name":"Module","version":"1.0.0","kind":"extension",)"
                           R"("author":"Shirowwww","description":"Test.","kitApi":"1.0","main":"main.js",)"
                           R"("type":"module"})";
    std::ofstream(dir / "manifest.json") << manifest;
    std::ofstream(dir / "main.js") << "import { greet } from './lib/greet.js';\nAMK.log(greet('module'));\n";
    fs::create_directories(dir / "lib");
    std::ofstream(dir / "lib" / "greet.js") << "export const greet = (who) => 'bonjour ' + who;\n";
    amk::ManifestResult result = amk::ParseManifest(manifest, "module-main", 1, [](const std::string&) { return true; });
    CHECK(result.manifest && result.manifest->module);

    Fixture f;
    f.host.process = "ui";
    amk::ScriptExtension element(*result.manifest, dir, f.host, f.journal, f.clock());
    CHECK(element.Start());
    CHECK(f.Logged("[module-main] bonjour module"));
    element.Stop();

    // Without "type": "module", a static import is a syntax error: the element fails, the reason is logged.
    amk::ManifestResult classic = amk::ParseManifest(Manifest("module-main", ""), "module-main", 1,
                                                     [](const std::string&) { return true; });
    amk::ScriptExtension script(*classic.manifest, dir, f.host, f.journal, f.clock());
    CHECK(!script.Start());
    CHECK(script.lastError().find("SyntaxError") != std::string::npos);
}
