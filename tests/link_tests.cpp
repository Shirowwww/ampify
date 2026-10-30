// AMK.link: the message channel between an app and the extension that has the same id.

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
    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal{out, [] { return std::string("t"); }};
    amk::ExtensionHost hosts{journal};
    amk::ExtensionLoader loader;
    amk::Settings settings;

    explicit LinkKit(const std::string& name)
        : root(Clean(name)), loader(hosts, host, journal, root / "extensions") {
        host.process = "ui";
        loader.SetAppRoot(root / "apps");
    }

    static fs::path Clean(const std::string& name) {
        fs::path folder = fs::temp_directory_path() / "amk-tests" / ("link-" + name);
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
    kit.Add("extensions", "paroles", R"(
        AMK.link.on((message) => {
          AMK.log("l'extension reçoit " + message.text + " " + message.n);
          AMK.link.post({ text: "merci", list: [1, 2] });
        });)");
    kit.Add("apps", "paroles", R"(
        AMK.link.on((message) => AMK.log("l'app reçoit " + message.text + " " + message.list.length));
        AMK.log("connectée " + AMK.link.connected());
        AMK.log("envoyé " + AMK.link.post({ text: "bonjour", n: 3 }));)");
    kit.Start();
    CHECK(kit.hosts.StateOf("paroles") == amk::ExtensionState::Running);
    CHECK(kit.hosts.StateOf("app:paroles") == amk::ExtensionState::Running);
    CHECK(kit.Logged("[paroles] connectée true"));
    CHECK(kit.Logged("[paroles] envoyé true"));

    // Delivered on the next turns of the JavaScript thread, never inside post itself.
    CHECK(!kit.Logged("l'extension reçoit"));
    kit.Pump();
    CHECK(kit.Logged("[paroles] l'extension reçoit bonjour 3"));
    CHECK(kit.Logged("[paroles] l'app reçoit merci 2"));
    kit.hosts.StopAll();
}

TEST_CASE(Link_NoPeerMeansPostReturnsFalse) {
    LinkKit kit("alone");
    kit.Add("extensions", "seule", R"(AMK.log("seule " + AMK.link.post({}) + " " + AMK.link.connected());)");
    kit.Add("apps", "autre", R"(AMK.log("autre " + AMK.link.post({}));)");  // another id: not linked
    kit.Start();
    CHECK(kit.Logged("[seule] seule false false"));
    CHECK(kit.Logged("[autre] autre false"));
    kit.hosts.StopAll();
}

TEST_CASE(Link_AStoppedPeerIsNotReachable) {
    LinkKit kit("stopped");
    kit.Add("extensions", "duo", R"(AMK.link.on((m) => AMK.log("reçu " + m));)");
    kit.Add("apps", "duo", R"(AMK.log("envoi " + AMK.link.post("x"));)");  // posts each time it starts
    kit.Start();
    kit.Pump();
    CHECK(kit.Logged("[duo] envoi true"));
    CHECK(kit.Logged("[duo] reçu x"));

    // The extension stops: the app, started again, finds nobody.
    kit.loader.Apply(amk::ParseReloadRequests("stop duo\nrestart app:duo\n"), kit.settings);
    CHECK(kit.Logged("[duo] envoi false"));
    CHECK(kit.hosts.StateOf("app:duo") == amk::ExtensionState::Running);

    // Back on: reachable again.
    kit.loader.Apply(amk::ParseReloadRequests("start duo\nrestart app:duo\n"), kit.settings);
    kit.Pump();
    CHECK(kit.out.str().rfind("envoi true") > kit.out.str().find("envoi false"));
    kit.hosts.StopAll();
}

TEST_CASE(Link_RefusesWhatIsNotJsonAndLongMessages) {
    LinkKit kit("refuse");
    kit.Add("extensions", "strict", R"(
        try { AMK.link.post(undefined); } catch (e) { AMK.log("vide " + e.name); }
        try { AMK.link.post(() => 1); } catch (e) { AMK.log("fonction " + e.name); }
        try { AMK.link.post("x".repeat(70000)); } catch (e) { AMK.log("long " + e.name); }
        try { AMK.link.on(42); } catch (e) { AMK.log("écoute " + e.name); }
        const off = AMK.link.on(() => {});
        off(); off();
        AMK.log("membres " + Object.isFrozen(AMK.link));)");
    kit.Start();
    CHECK(kit.Logged("[strict] vide TypeError"));
    CHECK(kit.Logged("[strict] fonction TypeError"));
    CHECK(kit.Logged("[strict] long RangeError"));
    CHECK(kit.Logged("[strict] écoute TypeError"));
    CHECK(kit.Logged("[strict] membres true"));
    kit.hosts.StopAll();
}
