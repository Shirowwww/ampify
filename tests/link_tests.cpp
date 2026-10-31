// Ampify.link: the message channel between an app and the extension that has the same id.

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

std::string Manifest(const std::string& id, const std::string& kind) {
    return R"({"id":")" + id + R"(","name":"Lien","version":"1.0.0","kind":")" + kind +
           R"(","author":"Shirowwww","description":"Test.","kitApi":"1.0","main":"main.js","processes":["ui"]})";
}

// A kit with an extensions and an apps folder, read by one loader in the interface process.
struct LinkKit {
    fs::path root;
    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};
    ampify::ExtensionHost hosts{journal};
    ampify::ExtensionLoader loader;
    ampify::Settings settings;

    explicit LinkKit(const std::string& name)
        : root(Clean(name)), loader(hosts, host, journal, root / "extensions") {
        host.process = "ui";
        loader.SetAppRoot(root / "apps");
    }

    static fs::path Clean(const std::string& name) {
        fs::path folder = fs::temp_directory_path() / "ampify-tests" / ("link-" + name);
        fs::remove_all(folder);
        return folder;
    }

    void Add(const std::string& folder, const std::string& id, const std::string& main) {
        std::string kind = folder == "apps" ? "app" : "extension";
        fs::create_directories(root / folder / id);
        std::ofstream(root / folder / id / "manifest.json") << Manifest(id, kind);
        std::ofstream(root / folder / id / "main.js") << main;
        settings.SetBool((kind == "app" ? "apps." : "extensions.") + id + ".enabled", true);
    }

    void Start() {
        loader.Discover();
        hosts.StartAll(settings);
    }

    void Pump(int times = 3) {
        for (int i = 0; i < times; i++) loader.Pump();
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
};

}  // namespace

TEST_CASE(Link_AppAndExtensionExchangeJsonMessages) {
    LinkKit kit("exchange");
    kit.Add("extensions", "lyrics", R"(
        Ampify.link.on((message) => {
          Ampify.log("the extension gets " + message.text + " " + message.n);
          Ampify.link.post({ text: "thanks", list: [1, 2] });
        });)");
    kit.Add("apps", "lyrics", R"(
        Ampify.link.on((message) => Ampify.log("the app gets " + message.text + " " + message.list.length));
        Ampify.log("connected " + Ampify.link.connected());
        Ampify.log("sent " + Ampify.link.post({ text: "hello", n: 3 }));)");
    kit.Start();
    CHECK(kit.hosts.StateOf("lyrics") == ampify::ExtensionState::Running);
    CHECK(kit.hosts.StateOf("app:lyrics") == ampify::ExtensionState::Running);
    CHECK(kit.Logged("[lyrics] connected true"));
    CHECK(kit.Logged("[lyrics] sent true"));

    // Delivered on the next turns of the JavaScript thread, never inside post itself.
    CHECK(!kit.Logged("the extension gets"));
    kit.Pump();
    CHECK(kit.Logged("[lyrics] the extension gets hello 3"));
    CHECK(kit.Logged("[lyrics] the app gets thanks 2"));
    kit.hosts.StopAll();
}

TEST_CASE(Link_NoPeerMeansPostReturnsFalse) {
    LinkKit kit("alone");
    kit.Add("extensions", "alone", R"(Ampify.log("alone " + Ampify.link.post({}) + " " + Ampify.link.connected());)");
    kit.Add("apps", "other", R"(Ampify.log("other " + Ampify.link.post({}));)");  // another id: not linked
    kit.Start();
    CHECK(kit.Logged("[alone] alone false false"));
    CHECK(kit.Logged("[other] other false"));
    kit.hosts.StopAll();
}

TEST_CASE(Link_AStoppedPeerIsNotReachable) {
    LinkKit kit("stopped");
    kit.Add("extensions", "duo", R"(Ampify.link.on((m) => Ampify.log("received " + m));)");
    kit.Add("apps", "duo", R"(Ampify.log("sending " + Ampify.link.post("x"));)");  // posts each time it starts
    kit.Start();
    kit.Pump();
    CHECK(kit.Logged("[duo] sending true"));
    CHECK(kit.Logged("[duo] received x"));

    // The extension stops: the app, started again, finds nobody.
    kit.loader.Apply(ampify::ParseReloadRequests("stop duo\nrestart app:duo\n"), kit.settings);
    CHECK(kit.Logged("[duo] sending false"));
    CHECK(kit.hosts.StateOf("app:duo") == ampify::ExtensionState::Running);

    // Back on: reachable again.
    kit.loader.Apply(ampify::ParseReloadRequests("start duo\nrestart app:duo\n"), kit.settings);
    kit.Pump();
    CHECK(kit.out.str().rfind("sending true") > kit.out.str().find("sending false"));
    kit.hosts.StopAll();
}

TEST_CASE(Link_RefusesWhatIsNotJsonAndLongMessages) {
    LinkKit kit("refuse");
    kit.Add("extensions", "strict", R"(
        try { Ampify.link.post(undefined); } catch (e) { Ampify.log("empty " + e.name); }
        try { Ampify.link.post(() => 1); } catch (e) { Ampify.log("function " + e.name); }
        try { Ampify.link.post("x".repeat(70000)); } catch (e) { Ampify.log("long " + e.name); }
        try { Ampify.link.on(42); } catch (e) { Ampify.log("listen " + e.name); }
        const off = Ampify.link.on(() => {});
        off(); off();
        Ampify.log("members " + Object.isFrozen(Ampify.link));)");
    kit.Start();
    CHECK(kit.Logged("[strict] empty TypeError"));
    CHECK(kit.Logged("[strict] function TypeError"));
    CHECK(kit.Logged("[strict] long RangeError"));
    CHECK(kit.Logged("[strict] listen TypeError"));
    CHECK(kit.Logged("[strict] members true"));
    kit.hosts.StopAll();
}
