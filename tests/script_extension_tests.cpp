#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../src/core/manifest.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"
#include "pump.hpp"
#include "test.hpp"

#ifndef AMPIFY_SOURCE_DIR
#define AMPIFY_SOURCE_DIR "."
#endif

namespace {

namespace fs = std::filesystem;

fs::path ScratchDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("element-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string Manifest(const std::string& id, const std::string& permissions) {
    return R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
           R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
}

// Everything an element needs in one place: a fake host, a journal in a string, and a clock the test moves.
struct Fixture {
    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};
    std::chrono::steady_clock::time_point now{std::chrono::seconds(1000)};

    ampify::Clock clock() {
        return [this] { return now; };
    }

    // Writes the element's folder and returns the element. Each test uses its own id, so folders never clash.
    std::unique_ptr<ampify::ScriptExtension> Make(const std::string& id, const std::string& permissions,
                                               const std::string& main, ampify::js::Limits limits = {}) {
        fs::path dir = ScratchDir(id);
        std::ofstream(dir / "manifest.json") << Manifest(id, permissions);
        std::ofstream(dir / "main.js") << main;
        ampify::ManifestResult result = ampify::ParseManifest(Manifest(id, permissions), id, 1, [](const std::string&) {
            return true;
        });
        return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, host, journal, clock(), limits);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

}  // namespace

TEST_CASE(Element_RunsMainAndLogsWithItsId) {
    Fixture f;
    auto element = f.Make("demo", "", R"(Ampify.log("hello"); Ampify.log(Ampify.process + " " + Ampify.locale);)");
    f.host.process = "ui";
    CHECK(element->Start());
    CHECK(element->running());
    CHECK(f.Logged("[demo] hello"));
    CHECK(f.Logged("[demo] ui fr-FR"));
    element->Stop();
    CHECK(!element->running());
}

TEST_CASE(Element_RejectsStorageWithoutThePermission) {
    Fixture f;
    auto element = f.Make("nostore", "", R"(Ampify.storage.set("a", 1);)");
    CHECK(!element->Start());
    CHECK(element->lastError().find("PermissionDenied") != std::string::npos);
    CHECK(element->lastError().find("storage") != std::string::npos);
    CHECK(!element->running());
}

TEST_CASE(Element_StorageSurvivesARestart) {
    Fixture f;
    const std::string main =
        R"(const n = Ampify.storage.get("launches", 0) + 1; Ampify.storage.set("launches", n); Ampify.log("launch " + n);)";
    auto first = f.Make("counter", R"("storage")", main);
    CHECK(first->Start());
    first->Stop();
    CHECK(f.host.storage.count("counter") == 1);
    CHECK(f.host.storage["counter"] == R"({"launches":1})");

    auto second = f.Make("counter", R"("storage")", main);
    CHECK(second->Start());
    CHECK(f.Logged("launch 2"));
}

TEST_CASE(Element_ConfigReadsTypedValuesAndWritesOnlyItsOwnKeys) {
    Fixture f;
    f.host.settings["extensions.cfg.volume"] = "7";
    auto element = f.Make("cfg", "", R"(
        Ampify.log("v=" + (Ampify.config.get("volume", 5) + 1));
        Ampify.log("d=" + Ampify.config.get("absent", 2.5));
        Ampify.config.set("on", true);
        try { Ampify.config.set("enabled", true); } catch (e) { Ampify.log("refused " + e.name); }
    )");
    CHECK(element->Start());
    CHECK(f.Logged("v=8"));
    CHECK(f.Logged("d=2.5"));
    CHECK(f.Logged("refused TypeError"));
    CHECK(f.host.settings["extensions.cfg.on"] == "true");
    CHECK(f.host.settings.count("extensions.cfg.enabled") == 0);
}

TEST_CASE(Element_ConfigGivesTheDeclaredDefaultsWithTheirType) {
    Fixture f;
    std::string manifest = Manifest("decl", "");
    manifest.insert(manifest.rfind('}'), R"(,"settings":[
        {"key":"seconds","label":"Seconds","type":"toggle","default":true},
        {"key":"size","label":"Size","type":"number","default":12},
        {"key":"side","label":"Side","type":"choice","options":["Left","Right"],"default":"Left"},
        {"key":"count","label":"Count","type":"number","default":3},
        {"key":"sound","label":"Sound","type":"toggle","default":true}])");
    ampify::ManifestResult result = ampify::ParseManifest(manifest, "decl", 1, [](const std::string&) { return true; });
    CHECK(result.manifest.has_value());
    if (!result.manifest) return;
    f.host.settings["extensions.decl.size"] = "20";
    f.host.settings["extensions.decl.count"] = "";      // emptied by hand
    f.host.settings["extensions.decl.sound"] = "maybe";  // neither a yes nor a no
    fs::path dir = ScratchDir("decl");
    std::ofstream(dir / "main.js") << R"(
        Ampify.log("seconds=" + (Ampify.config.get("seconds") === true));
        Ampify.log("size=" + (Ampify.config.get("size") + 1));
        Ampify.log("side=" + Ampify.config.get("side"));
        Ampify.log("given=" + Ampify.config.get("side", "Right"));
        Ampify.log("absent=" + Ampify.config.get("absent"));
        Ampify.log("count=" + Ampify.config.get("count"));
        Ampify.log("sound=" + Ampify.config.get("sound"));
    )";
    ampify::ScriptExtension element(*result.manifest, dir, f.host, f.journal, f.clock(), {});
    CHECK(element.Start());
    CHECK(f.Logged("seconds=true"));
    CHECK(f.Logged("size=21"));  // the stored value, read as a number
    CHECK(f.Logged("side=Left"));
    CHECK(f.Logged("given=Right"));  // a default given in the call wins
    CHECK(f.Logged("absent=undefined"));
    CHECK(f.Logged("count=3"));     // an empty value is not 0
    CHECK(f.Logged("sound=true"));  // nor is an unknown word false
}

TEST_CASE(Element_RunsOnStopHandlersInReverseOrder) {
    Fixture f;
    auto element = f.Make("order", "", R"(Ampify.onStop(() => Ampify.log("one")); Ampify.onStop(() => Ampify.log("two"));)");
    CHECK(element->Start());
    element->Stop();
    std::string log = f.out.str();
    CHECK(log.find("two") != std::string::npos);
    CHECK(log.find("two") < log.find("one", log.find("two")));
}

TEST_CASE(Element_CannotReplaceTheFrozenApi) {
    Fixture f;
    auto element = f.Make("frozen", "", R"(Ampify.version = "9.9"; Ampify.log(Ampify.version);)");
    CHECK(element->Start());
    CHECK(f.Logged(std::string("[frozen] ") + ampify::kKernelVersion));
}

TEST_CASE(Element_TimersFireWhenDueAndCanBeCleared) {
    Fixture f;
    auto element = f.Make("timers", "", R"(
        setTimeout(() => Ampify.log("tic"), 100);
        let n = 0;
        const id = setInterval(() => { n++; Ampify.log("t" + n); if (n === 2) clearInterval(id); }, 10);
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
        setTimeout(() => { throw new Error("boom"); }, 10);
        setTimeout(() => Ampify.log("other"), 10);
    )");
    CHECK(element->Start());
    f.now += std::chrono::milliseconds(20);
    element->Pump();
    CHECK(f.Logged("boom"));
    CHECK(f.Logged("other"));
    CHECK(element->running());
}

TEST_CASE(Element_PlayerStateNeedsPlayerRead) {
    Fixture f;
    f.host.player.playing = true;
    ampify::TrackInfo track;
    track.title = "Title";
    f.host.player.track = track;
    auto denied = f.Make("noread", "", R"(try { Ampify.player.state(); } catch (e) { Ampify.log(e.name); })");
    CHECK(denied->Start());
    CHECK(f.Logged("PermissionDenied"));

    auto element = f.Make("read", R"("player.read")", R"(Ampify.log(Ampify.player.state().track.title);)");
    CHECK(element->Start());
    CHECK(f.Logged("[read] Title"));
}

TEST_CASE(Element_PlayerEventsArriveOnThePumpNotOnTheCallingThread) {
    Fixture f;
    auto element = f.Make("events", R"("player.read")", R"(Ampify.player.on("trackchange", (s) => Ampify.log("track " + s.track.title));)");
    CHECK(element->Start());

    ampify::PlayerSnapshot snapshot;
    ampify::TrackInfo track;
    track.title = "New";
    snapshot.track = track;
    element->DeliverPlayer("trackchange", snapshot);
    CHECK(!f.Logged("track New"));
    element->Pump();
    CHECK(f.Logged("track New"));
}

TEST_CASE(Element_PlayerCommandsNeedPlayerControl) {
    Fixture f;
    auto denied = f.Make("nocontrol", "", R"(try { Ampify.player.next(); } catch (e) { Ampify.log(e.name); })");
    CHECK(denied->Start());
    CHECK(f.host.commands.empty());

    auto element = f.Make("control", R"("player.control")", R"(Ampify.player.next(); Ampify.player.seek(1500);)");
    CHECK(element->Start());
    CHECK(f.host.commands.size() == 2);
    CHECK(f.host.commands[0] == ampify::PlayerCommand::Next);
    CHECK(f.host.commands[1] == ampify::PlayerCommand::Seek);
}

TEST_CASE(Element_GainIsClampedToTheKernelCeilingAndResetOnStop) {
    Fixture f;
    f.host.settings["audio.maxGainDb"] = "3";
    auto element = f.Make("gain", R"("audio.gain")", R"(Ampify.log("gain=" + Ampify.audio.setGainDb(20));)");
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
    std::string main = std::string(R"(Ampify.notify({ title: "Salut", text: ")") + text + R"(" });)";
    auto element = f.Make("note", R"("notifications")", main);
    CHECK(element->Start());
    CHECK(f.host.notifications.size() == 1);
    CHECK(f.host.notifications[0].size() == std::string("Salut|").size() + 500);
    CHECK(f.host.notifications[0] == "Salut|" + std::string(500, 'x'));

    auto denied = f.Make("nonote", "", R"(Ampify.notify({ title: "x" });)");
    CHECK(!denied->Start());
}

TEST_CASE(Element_KeyboardBindingIsNormalizedAndDelivered) {
    Fixture f;
    auto element = f.Make("keys", R"("ui.keyboard")", R"(
        Ampify.keyboard.bind("ctrl+alt+right", () => Ampify.log("next"));
        try { Ampify.keyboard.bind("a", () => {}); } catch (e) { Ampify.log("refused " + e.name); }
    )");
    CHECK(element->Start());
    CHECK(f.host.keys.count("Ctrl+Alt+Right") == 1);
    CHECK(f.Logged("refused RangeError"));
    element->DeliverKey("Ctrl+Alt+Right");
    element->Pump();
    CHECK(f.Logged("next"));
    element->Stop();
    CHECK(f.host.keys.empty());
}

TEST_CASE(Element_HttpReachesOnlyDeclaredHttpsHosts) {
    Fixture f;
    auto element = f.Make("net", R"("network:api.example.org")", R"(
        Ampify.http.get("https://api.example.org/x").then((r) => Ampify.log("code " + r.status + " " + r.text));
        Ampify.http.get("https://evil.example/").catch((e) => Ampify.log("refused " + e.name));
        Ampify.http.get("http://api.example.org/").catch((e) => Ampify.log("plain " + e.name));
    )");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("code 200 ok"); }));
    CHECK(f.Logged("refused PermissionDenied"));
    CHECK(f.Logged("plain TypeError"));
    CHECK(f.host.RequestCount() == 1);
}

TEST_CASE(Element_UriParsesAndBuildsLinks) {
    Fixture f;
    auto element = f.Make("uri", "", R"(
        Ampify.log(JSON.stringify(Ampify.uri.parse("https://music.apple.com/fr/album/name/123?i=456")));
        Ampify.log(Ampify.uri.build("album", "123", { storefront: "fr" }));
        Ampify.log(String(Ampify.uri.parse("https://example.org/x")));
    )");
    CHECK(element->Start());
    CHECK(f.Logged(R"({"kind":"album","storefront":"fr","id":"123","trackId":"456"})"));
    CHECK(f.Logged("https://music.apple.com/fr/album/_/123"));
    CHECK(f.Logged("[uri] null"));
}

TEST_CASE(Element_IconsReturnSvgAndRejectUnknownNames) {
    Fixture f;
    auto element = f.Make("icons", "", R"(
        Ampify.log(Ampify.icons.get("play").slice(0, 4));
        try { Ampify.icons.get("unknown"); } catch (e) { Ampify.log(e.name); }
    )");
    CHECK(element->Start());
    CHECK(f.Logged("[icons] <svg"));
    CHECK(f.Logged("RangeError"));
}

TEST_CASE(Element_FailsCleanlyWhenMainThrows) {
    Fixture f;
    auto element = f.Make("throws", "", R"(throw new Error("boom at start");)");
    CHECK(!element->Start());
    CHECK(element->lastError().find("boom at start") != std::string::npos);
    CHECK(!element->running());
}

TEST_CASE(Element_IsCutWhenItLoopsForever) {
    Fixture f;
    ampify::js::Limits limits;
    limits.budget = std::chrono::milliseconds(50);
    auto element = f.Make("spin", "", "while (true) {}", limits);
    CHECK(!element->Start());
    CHECK(element->lastError().find("budget") != std::string::npos);
}

TEST_CASE(Element_ExamplesStartWithTheirOwnPermissions) {
    for (const char* name : {"hello-extension", "keyboard-skip", "sleep-timer", "discord-presence", "scrobbler"}) {
        Fixture f;
        fs::path dir = fs::path(AMPIFY_SOURCE_DIR) / "examples" / name;
        std::ifstream manifestFile(dir / "manifest.json");
        std::stringstream manifestText;
        manifestText << manifestFile.rdbuf();
        ampify::ManifestResult result = ampify::ParseManifest(manifestText.str(), name, 1, [&](const std::string& file) {
            return fs::exists(dir / file);
        });
        CHECK(result.manifest.has_value());
        if (!result.manifest) continue;

        ampify::ScriptExtension element(*result.manifest, dir, f.host, f.journal, f.clock());
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
    std::ofstream(dir / "main.js") << "import { greet } from './lib/greet.js';\nAmpify.log(greet('module'));\n";
    fs::create_directories(dir / "lib");
    std::ofstream(dir / "lib" / "greet.js") << "export const greet = (who) => 'hello ' + who;\n";
    ampify::ManifestResult result = ampify::ParseManifest(manifest, "module-main", 1, [](const std::string&) { return true; });
    CHECK(result.manifest && result.manifest->module);

    Fixture f;
    f.host.process = "ui";
    ampify::ScriptExtension element(*result.manifest, dir, f.host, f.journal, f.clock());
    CHECK(element.Start());
    CHECK(f.Logged("[module-main] hello module"));
    element.Stop();

    // Without "type": "module", a static import is a syntax error: the element fails, the reason is logged.
    ampify::ManifestResult classic = ampify::ParseManifest(Manifest("module-main", ""), "module-main", 1,
                                                     [](const std::string&) { return true; });
    ampify::ScriptExtension script(*classic.manifest, dir, f.host, f.journal, f.clock());
    CHECK(!script.Start());
    CHECK(script.lastError().find("SyntaxError") != std::string::npos);
}
