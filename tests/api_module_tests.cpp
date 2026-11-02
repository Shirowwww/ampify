// The Ampify object is assembled from one prelude fragment and one natives file per module (src/core/api/modules.cpp).
// These tests pin what an extension sees, so that moving code between modules cannot change the API.

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

#include "../src/core/api/bridge.hpp"
#include "../src/core/api/module.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

struct Fixture {
    ampify::test::FakeHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};

    std::unique_ptr<ampify::ScriptExtension> Make(const std::string& id, const std::string& main,
                                               const std::string& permissions = "") {
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("module-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "manifest.json") << manifest;
        std::ofstream(dir / "main.js") << main;
        ampify::ManifestResult result = ampify::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, host, journal);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }

    // The text of the first journal line that starts with `[id] prefix`, without the prefix.
    std::string Line(const std::string& id, const std::string& prefix) const {
        const std::string marker = "[" + id + "] " + prefix;
        std::istringstream lines(out.str());
        for (std::string line; std::getline(lines, line);) {
            size_t at = line.find(marker);
            if (at != std::string::npos) return line.substr(at + marker.size());
        }
        return {};
    }
};

// Describes each member of Ampify, two levels further down: type (functions with their arity, "p" when they can be
// constructed, and their name), and "!" when frozen. One journal line per member and per global, plus the member
// order, so that a new module adds members without touching the expectations below. Those were taken from the
// single-file prelude, before it was split into modules.
const char* const kDescribeSurface = R"(
    const kind = (v) => typeof v === "function" ? "f" + v.length + (v.prototype ? "p" : "") + "=" + v.name : typeof v;
    const frozen = (v) => (typeof v === "object" || typeof v === "function") && Object.isFrozen(v) ? "!" : "";
    const show = (v, depth) => {
        let s = kind(v) + frozen(v);
        if (depth > 0 && typeof v === "object" && v !== null) {
            s += "{" + Object.keys(v).map((k) => k + ":" + show(v[k], depth - 1)).join(",") + "}";
        }
        return s;
    };
    Ampify.log("frozen " + Object.isFrozen(Ampify));
    Ampify.log("order " + Object.keys(Ampify).join(","));
    for (const key of Object.keys(Ampify)) Ampify.log("member " + key + " = " + show(Ampify[key], 2));
    for (const name of ["setTimeout", "setInterval", "clearTimeout", "clearInterval", "queueMicrotask", "console"]) {
        Ampify.log("global " + name + " = " + show(globalThis[name], 1));
    }
)";

struct Expected {
    const char* name;
    const char* description;
};

const Expected kMembers[] = {
    {"version", "string"},
    {"kitApi", "string"},
    {"process", "string"},
    {"locale", "string"},
    {"log", "f1=log"},
    {"onStop", "f1=onStop"},
    {"config", "object!{get:f2=get,set:f2=set}"},
    {"storage", "object!{get:f2=get,set:f2=set,remove:f1=remove}"},
    // toggle, queue and lyrics added in kitApi 1.x, compatible additions
    {"player", "object!{state:f0=state,on:f2=on,play:f0=play,pause:f0=pause,next:f0=next,previous:f0=previous,"
               "seek:f1=seek,toggle:f0=toggle,lyrics:f0=lyrics,artwork:f0=artwork,queue:object!{list:f0=list,add:f2=add,remove:f1=remove}}"},
    {"audio", "object!{setGainDb:f1=setGainDb,onFrame:f1=onFrame,analyze:f0=analyze,loudness:f0=loudness,"
              "volume:f0=volume,eq:object!{setBand:f2=setBand,preset:f1=preset,reset:f0=reset,bands:f0=bands,"
              "presets:f0=presets}}"},
    {"keyboard", "object!{bind:f2=bind}"},
    {"notify", "f1="},
    {"http", "object!{get:f1=get,post:f2=post,postForm:f2=postForm,open:f1=open}"},  // post, postForm and open added in kitApi 1.x, compatible additions
    {"icons", "object!{get:f1=get,names:f0=names}"},
    {"uri", "object!{parse:f1=parse,build:f3=build}"},
    {"color", "object!{extract:f3=extract,fromArtwork:f1=fromArtwork}"},
    {"fonts", "object!{roles:f0=roles,style:f1=style}"},
    {"ui", "object!{topbar:object!{addButton:f1=addButton},playbar:object!{addButton:f1=addButton},"
           "sidebar:object!{addItem:f1=addItem},"
           "menu:object!{addItem:f2=addItem},popup:object!{open:f1=open},"
           "fullscreen:object!{open:f1=open,close:f0=close,onClose:f1=onClose},applePlayer:object!{open:f0=open},"
           "panel:object!{register:f2=register},"
           "page:object!{register:f2=register},title:object!{set:f1=set},"
           "setLabel:f2=setLabel,navigate:f1=navigate}"},  // setLabel added in kitApi 1.x, a compatible addition
};

const Expected kGlobals[] = {
    {"setTimeout", "f2="},
    {"setInterval", "f2="},
    {"clearTimeout", "f1p=cancel"},
    {"clearInterval", "f1p=cancel"},
    {"queueMicrotask", "f1="},
    {"console", "object{log:f0=log,info:f0=info,warn:f0=warn,error:f0=error}"},
};

}  // namespace

TEST_CASE(Prelude_AmpifySurfaceIsUnchanged) {
    Fixture f;
    auto element = f.Make("surface", kDescribeSurface);
    CHECK(element->Start());
    CHECK(f.Line("surface", "frozen ") == "true");
    for (const Expected& member : kMembers) {
        const std::string actual = f.Line("surface", std::string("member ") + member.name + " = ");
        if (actual != member.description) {
            ampify::test::ReportFailure(__FILE__, __LINE__, std::string("Ampify.") + member.name + " : " + actual);
        }
    }
    for (const Expected& global : kGlobals) {
        const std::string actual = f.Line("surface", std::string("global ") + global.name + " = ");
        if (actual != global.description) {
            ampify::test::ReportFailure(__FILE__, __LINE__, std::string(global.name) + " : " + actual);
        }
    }

    // The members above keep their order. Members added by new modules may come anywhere.
    std::string order = "," + f.Line("surface", "order ") + ",";
    size_t previous = 0;
    for (const Expected& member : kMembers) {
        size_t at = order.find(std::string(",") + member.name + ",");
        CHECK(at != std::string::npos && at >= previous);
        previous = at == std::string::npos ? previous : at;
    }
}

TEST_CASE(ApiModules_HaveUniqueNamesAndNatives) {
    Fixture f;
    auto element = f.Make("natives", "");
    ampify::js::Runtime runtime;
    JSContext* ctx = runtime.context();
    std::set<std::string> modules;
    std::set<std::string> natives;
    for (const ampify::api::ApiModule& module : ampify::api::Modules()) {
        CHECK(modules.insert(module.name).second);
        CHECK(module.prelude != nullptr || module.defineNatives != nullptr);
        if (!module.defineNatives) continue;
        JSValue object = JS_NewObject(ctx);
        ampify::api::NativeScope scope(ctx, object, *element);
        module.defineNatives(scope);
        JSPropertyEnum* names = nullptr;
        uint32_t count = 0;
        CHECK(JS_GetOwnPropertyNames(ctx, &names, &count, object, JS_GPN_STRING_MASK) == 0);
        for (uint32_t i = 0; i < count; i++) {
            const char* name = JS_AtomToCString(ctx, names[i].atom);
            CHECK(natives.insert(name).second);  // two modules defining the same native would overwrite each other
            JS_FreeCString(ctx, name);
        }
        JS_FreePropertyEnum(ctx, names, count);
        runtime.Free(object);
    }
    CHECK(modules.count("core") == 1 && modules.count("ui") == 1);
    CHECK(natives.count("log") == 1 && natives.count("uiTitle") == 1 && natives.count("locale") == 1);
}

TEST_CASE(Element_DeliverEventCallsTheNamedDispatchEntry) {
    Fixture f;
    auto element = f.Make("events", R"(Ampify.keyboard.bind("ctrl+alt+right", () => Ampify.log("next"));)",
                          R"("ui.keyboard")");
    CHECK(element->Start());
    element->DeliverEvent("key", "Ctrl+Alt+Right");
    element->Pump();
    CHECK(f.Logged("[events] next"));

    // An entry that no module defines is logged and does not stop the element.
    element->DeliverEvent("absent", "{}");
    element->Pump();
    CHECK(element->running());
    element->Stop();
}

namespace {
struct CounterState {
    int value = 0;
};
struct OtherState {
    int value = 0;
};
}  // namespace

TEST_CASE(Element_ModuleStateIsPerTypeAndDroppedWithTheRuntime) {
    Fixture f;
    auto element = f.Make("state", "");
    ampify::ScriptBridge::State<CounterState>(element.get()).value = 3;
    CHECK(ampify::ScriptBridge::State<CounterState>(element.get()).value == 3);
    CHECK(ampify::ScriptBridge::State<OtherState>(element.get()).value == 0);
    CHECK(element->Start());
    CHECK(ampify::ScriptBridge::State<CounterState>(element.get()).value == 3);
    element->Stop();
    CHECK(ampify::ScriptBridge::State<CounterState>(element.get()).value == 0);
}

namespace {

// Capabilities as a module would declare them in its own header.
struct EchoHost {
    static constexpr std::string_view kCapabilityName = "test.echo";
    virtual ~EchoHost() = default;
    virtual std::string Echo(const std::string& text) = 0;
};

struct ChimeHost {
    static constexpr std::string_view kCapabilityName = "test.chime";
    virtual ~ChimeHost() = default;
    virtual int Chimes() = 0;
};

// A host that offers EchoHost only. FakeHost comes first, so EchoHost sits at another address in the object.
struct EchoingHost : ampify::test::FakeHost, EchoHost {
    std::string Echo(const std::string& text) override { return "echo " + text; }
    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<EchoHost>(name, this)) return found;
        return FakeHost::QueryCapability(name);
    }
};

}  // namespace

TEST_CASE(Host_CapabilityIsNullUnlessTheHostOffersIt) {
    ampify::test::FakeHost plain;
    ampify::Host& plainHost = plain;
    CHECK(plainHost.Capability<EchoHost>() == nullptr);

    EchoingHost echoing;
    ampify::Host& host = echoing;
    EchoHost* echo = host.Capability<EchoHost>();
    CHECK(echo != nullptr);
    CHECK(echo == static_cast<EchoHost*>(&echoing));
    if (echo) CHECK(echo->Echo("hello") == "echo hello");
    CHECK(host.Capability<ChimeHost>() == nullptr);
}
