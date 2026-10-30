// Tests of the module react (core/prelude/react.cpp): AMK.React renders the primitive trees of core/ui_model,
// re-renders on a state change, runs and cleans up effects, passes contexts down, and AMK.components adds the
// tooltip attributes. The primitives and attributes it uses are tested with the model, in ui_model_tests.cpp.

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../core/journal.hpp"
#include "../core/manifest.hpp"
#include "../core/script_extension.hpp"
#include "../core/ui_model.hpp"
#include "fake_host.hpp"
#include "test.hpp"

#ifndef AMK_SOURCE_DIR
#define AMK_SOURCE_DIR "."
#endif

namespace {

namespace fs = std::filesystem;

std::string ManifestText(const std::string& id, const std::string& kind, const std::string& permissions) {
    return R"({"id":")" + id + R"(","name":"Démo","version":"1.0.0","kind":")" + kind +
           R"(","author":"Shirowwww","description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" +
           permissions + "]}";
}

struct Fixture {
    amk::test::FakeHost host;
    std::ostringstream out;
    amk::Journal journal{out, [] { return std::string("t"); }};

    std::unique_ptr<amk::ScriptExtension> Make(const std::string& id, const std::string& main,
                                               const std::string& permissions = R"("ui.panel")",
                                               const std::string& kind = "extension") {
        fs::path dir = fs::temp_directory_path() / "amk-tests" / ("react-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        std::ofstream(dir / "manifest.json") << ManifestText(id, kind, permissions);
        std::ofstream(dir / "main.js") << main;
        amk::ManifestResult result = amk::ParseManifest(ManifestText(id, kind, permissions), id, 1,
                                                        [](const std::string&) { return true; });
        return std::make_unique<amk::ScriptExtension>(*result.manifest, dir, host, journal);
    }

    std::string Panel(const std::string& id, const std::string& panel) const {
        auto slot = host.ui.Panel(id, panel);
        return slot ? slot->tree.Dump() : std::string("(absent)");
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }

    size_t Count(const std::string& text) const {
        size_t count = 0;
        const std::string all = out.str();
        for (size_t at = all.find(text); at != std::string::npos; at = all.find(text, at + 1)) count++;
        return count;
    }

    size_t Position(const std::string& text) const { return out.str().find(text); }
};

// The first node of the tree whose attribute `key` equals `value`, depth first.
const amk::Json* FindNode(const amk::Json& node, const std::string& key, const std::string& value) {
    if (const amk::Json* found = node.Find(key)) {
        if (const std::string* text = found->AsString(); text && *text == value) return &node;
    }
    if (const amk::Json* children = node.Find("children")) {
        if (const amk::Json::Array* items = children->AsArray()) {
            for (const amk::Json& child : *items) {
                if (const amk::Json* found = FindNode(child, key, value)) return found;
            }
        }
    }
    return nullptr;
}

// The callback id held by `attribute` on the node found by FindNode, or 0.
int CallbackOf(const amk::UiModel& ui, const std::string& id, const std::string& panel, const std::string& key,
               const std::string& value, const std::string& attribute) {
    auto slot = ui.Panel(id, panel);
    if (!slot) return 0;
    const amk::Json* node = FindNode(slot->tree, key, value);
    const amk::Json* callback = node ? node->Find(attribute) : nullptr;
    const double* number = callback ? callback->AsNumber() : nullptr;
    return number ? static_cast<int>(*number) : 0;
}

}  // namespace

TEST_CASE(React_RendersTheExpectedPrimitiveTree) {
    Fixture f;
    auto element = f.Make("tree", R"(
        const { h, Fragment } = AMK.React;
        function Title(props) { return h("h1", null, "Bonjour ", props.name); }
        function App() {
          return h("div", { gap: 8, style: { color: "red" }, className: "x" },
            h(Title, { name: "l'utilisateur" }),
            false,
            null,
            h(Fragment, null, h("text", { text: "Un" }), 2),
            h("button", { label: "OK", onClick: () => {} }),
            h("row", null, [h("text", { key: "a" }, "A"), h("text", { key: "b" }, "B")]));
        }
        AMK.ReactDOM.render(h(App), "panel:demo");
    )");
    CHECK(element->Start());
    CHECK(f.Panel("tree", "demo") ==
          R"({"type":"stack","gap":8,"className":"x","children":[{"type":"text","variant":"title","text":"Bonjour l'utilisateur"},)"
          R"({"type":"text","text":"Un"},{"type":"text","text":"2"},{"type":"button","label":"OK","onClick":1},)"
          R"({"type":"row","children":[{"type":"text","text":"A"},{"type":"text","text":"B"}]}]})");
    element->Stop();
}

TEST_CASE(React_AClickRendersAgainWithTheNewStateOnce) {
    Fixture f;
    auto element = f.Make("counter", R"(
        const { h, useState } = AMK.React;
        let renders = 0;
        function Counter() {
          const [count, setCount] = useState(0);
          renders++;
          return h("stack", null, h("text", null, "Compte : " + count),
            h("button", { label: "Plus", onClick: () => { setCount((c) => c + 1); setCount((c) => c + 1); } }));
        }
        AMK.ReactDOM.render(h(Counter), "panel:compteur");
        AMK.onStop(() => AMK.log("rendus " + renders));
    )");
    CHECK(element->Start());
    CHECK(f.Panel("counter", "compteur") ==
          R"({"type":"stack","children":[{"type":"text","text":"Compte : 0"},{"type":"button","label":"Plus","onClick":1}]})");

    int plus = CallbackOf(f.host.ui, "counter", "compteur", "label", "Plus", "onClick");
    CHECK(plus == 1);
    element->DeliverClick(plus);
    element->Pump();
    CHECK(f.Panel("counter", "compteur") ==
          R"({"type":"stack","children":[{"type":"text","text":"Compte : 2"},{"type":"button","label":"Plus","onClick":1}]})");
    element->Stop();
    CHECK(f.Logged("rendus 2"));  // two setState calls in one click give one render
}

TEST_CASE(React_ChangeEventsReachTheComponents) {
    Fixture f;
    auto element = f.Make("settings", R"(
        const { h, useState } = AMK.React;
        const { Toggle, Slider, Dropdown, TextField } = AMK.components;
        function Settings() {
          const [on, setOn] = useState(false);
          const [level, setLevel] = useState(10);
          const [mode, setMode] = useState("a");
          const [name, setName] = useState("");
          return h("stack", null,
            h(Toggle, { label: "Actif", value: on, onChange: setOn }),
            h(Slider, { label: "Niveau", min: 0, max: 100, value: level, onChange: setLevel }),
            h(Dropdown, { label: "Mode", value: mode, options: ["a", { value: "b", label: "Bé" }], onChange: setMode }),
            h(TextField, { label: "Nom", value: name, placeholder: "Votre nom", onChange: setName }));
        }
        AMK.ReactDOM.render(h(Settings), "panel:reglages");
    )");
    CHECK(element->Start());
    CHECK(f.Panel("settings", "reglages") ==
          R"({"type":"stack","children":[{"type":"toggle","label":"Actif","checked":false,"onChange":1},)"
          R"({"type":"slider","label":"Niveau","min":0,"max":100,"value":10,"onChange":2},)"
          R"({"type":"dropdown","label":"Mode","value":"a","onChange":3,"children":[{"type":"option","value":"a","label":"a"},)"
          R"({"type":"option","value":"b","label":"Bé"}]},)"
          R"({"type":"textfield","label":"Nom","value":"","placeholder":"Votre nom","onChange":4}]})");

    element->DeliverEvent("change", R"({"id":1,"value":true})");
    element->DeliverEvent("change", R"({"id":2,"value":42})");
    element->DeliverEvent("change", R"({"id":3,"value":"b"})");
    element->DeliverEvent("change", R"({"id":4,"value":"l'utilisateur"})");
    element->Pump();
    CHECK(f.Panel("settings", "reglages") ==
          R"({"type":"stack","children":[{"type":"toggle","label":"Actif","checked":true,"onChange":1},)"
          R"({"type":"slider","label":"Niveau","min":0,"max":100,"value":42,"onChange":2},)"
          R"({"type":"dropdown","label":"Mode","value":"b","onChange":3,"children":[{"type":"option","value":"a","label":"a"},)"
          R"({"type":"option","value":"b","label":"Bé"}]},)"
          R"({"type":"textfield","label":"Nom","value":"l'utilisateur","placeholder":"Votre nom","onChange":4}]})");

    // An unknown id or a broken payload changes nothing and does not stop the element.
    element->DeliverEvent("change", R"({"id":99,"value":1})");
    element->DeliverEvent("change", "pas du json");
    element->Pump();
    CHECK(element->running());
    element->Stop();
}

TEST_CASE(React_EffectCleanupsRunOnUnmountAndOnStop) {
    Fixture f;
    auto element = f.Make("effects", R"(
        const { h, useState, useEffect } = AMK.React;
        function Clock(props) {
          useEffect(() => {
            AMK.log("monté " + props.name);
            const timer = setInterval(() => {}, 1000);
            return () => { clearInterval(timer); AMK.log("démonté " + props.name); };
          }, []);
          return h("text", null, props.name);
        }
        function App() {
          const [show, setShow] = useState(true);
          return h("stack", null,
            show ? h(Clock, { name: "a" }) : null,
            h(Clock, { name: "b" }),
            h("button", { label: "cacher", onClick: () => setShow(false) }));
        }
        AMK.ReactDOM.render(h(App), "panel:effets");
    )");
    CHECK(element->Start());
    CHECK(f.Logged("monté a") && f.Logged("monté b"));
    CHECK(f.Position("monté a") < f.Position("monté b"));

    element->DeliverClick(CallbackOf(f.host.ui, "effects", "effets", "label", "cacher", "onClick"));
    element->Pump();
    CHECK(f.Logged("démonté a"));
    CHECK(!f.Logged("démonté b"));
    CHECK(f.Count("monté b") == 1);  // an effect with [] runs once
    CHECK(f.Panel("effects", "effets") ==
          R"({"type":"stack","children":[{"type":"text","text":"b"},{"type":"button","label":"cacher","onClick":1}]})");

    element->Stop();
    CHECK(f.Logged("démonté b"));
}

TEST_CASE(React_AnEffectRunsAgainWhenItsDependenciesChange) {
    Fixture f;
    auto element = f.Make("deps", R"(
        const { h, useState, useEffect, useMemo, useCallback, useRef } = AMK.React;
        function App() {
          const [n, setN] = useState(0);
          const renders = useRef(0);
          renders.current++;
          const double = useMemo(() => n * 2, [n]);
          const next = useCallback(() => setN(n + 1), [n]);
          useEffect(() => {
            AMK.log("effet " + n + " rendu " + renders.current);
            return () => AMK.log("fin " + n);
          }, [n]);
          return h("button", { label: "n=" + n + " double=" + double, onClick: next });
        }
        AMK.ReactDOM.render(h(App), "panel:deps");
    )");
    CHECK(element->Start());
    CHECK(f.Logged("effet 0 rendu 1"));
    element->DeliverClick(1);
    element->Pump();
    CHECK(f.Logged("fin 0"));
    CHECK(f.Logged("effet 1 rendu 2"));
    CHECK(f.Position("fin 0") < f.Position("effet 1"));
    CHECK(f.Panel("deps", "deps") == R"({"type":"button","label":"n=1 double=2","onClick":1})");
    element->Stop();
    CHECK(f.Logged("fin 1"));
}

TEST_CASE(React_ContextReachesNestedComponentsAndFollowsItsProvider) {
    Fixture f;
    auto element = f.Make("context", R"(
        const { h, createContext, useContext, useState } = AMK.React;
        const Theme = createContext("clair");
        function Label() { return h("text", null, "thème " + useContext(Theme)); }
        function App() {
          const [theme, setTheme] = useState("sombre");
          return h("stack", null,
            h(Theme.Provider, { value: theme },
              h("row", null, h(Label)),
              h(Theme.Consumer, null, (value) => h("text", null, "consommateur " + value))),
            h(Label),
            h("button", { label: "changer", onClick: () => setTheme("rose") }));
        }
        AMK.ReactDOM.render(h(App), "panel:contexte");
    )");
    CHECK(element->Start());
    CHECK(f.Panel("context", "contexte") ==
          R"({"type":"stack","children":[{"type":"row","children":[{"type":"text","text":"thème sombre"}]},)"
          R"({"type":"text","text":"consommateur sombre"},{"type":"text","text":"thème clair"},)"
          R"({"type":"button","label":"changer","onClick":1}]})");
    element->DeliverClick(1);
    element->Pump();
    CHECK(f.Panel("context", "contexte") ==
          R"({"type":"stack","children":[{"type":"row","children":[{"type":"text","text":"thème rose"}]},)"
          R"({"type":"text","text":"consommateur rose"},{"type":"text","text":"thème clair"},)"
          R"({"type":"button","label":"changer","onClick":1}]})");
    element->Stop();
}

TEST_CASE(React_TooltipAddsItsAttributesToTheWrappedPrimitive) {
    Fixture f;
    auto element = f.Make("tooltip", R"(
        const { h } = AMK.React;
        const { Tooltip, TooltipWrapper, Button } = AMK.components;
        AMK.ReactDOM.render(h("stack", null,
          h(Tooltip, { label: "Relancer le morceau", placement: "bottom", delay: 300 },
            h(Button, { label: "Relancer", onClick: () => {} })),
          h(TooltipWrapper, { content: "Aide" }, "?")), "panel:bulles");
    )");
    CHECK(element->Start());
    CHECK(f.Panel("tooltip", "bulles") ==
          R"({"type":"stack","children":[{"type":"button","label":"Relancer","onClick":1,)"
          R"("tooltip":"Relancer le morceau","tooltipPlacement":"bottom","tooltipDelay":300},)"
          R"({"type":"text","text":"?","tooltip":"Aide"}]})");
    element->Stop();

    Fixture g;
    auto bad = g.Make("badtip", R"(
        AMK.ReactDOM.render(AMK.React.h("button", { label: "x", tooltipPlacement: "milieu" }), "panel:bulles");
    )");
    CHECK(!bad->Start());
    CHECK(bad->lastError().find("tooltipPlacement") != std::string::npos);
}

TEST_CASE(React_KeysKeepTheStateOfReorderedChildren) {
    Fixture f;
    auto element = f.Make("keys", R"(
        const { h, useState } = AMK.React;
        function Item(props) {
          const [first] = useState(() => { AMK.log("init " + props.id); return props.id; });
          return h("text", null, props.id + "=" + first);
        }
        function App() {
          const [order, setOrder] = useState(["a", "b"]);
          return h("stack", null, order.map((id) => h(Item, { key: id, id })),
            h("button", { label: "inverser", onClick: () => setOrder(["b", "a"]) }));
        }
        AMK.ReactDOM.render(h(App), "panel:cles");
    )");
    CHECK(element->Start());
    element->DeliverClick(1);
    element->Pump();
    CHECK(f.Panel("keys", "cles") ==
          R"({"type":"stack","children":[{"type":"text","text":"b=b"},{"type":"text","text":"a=a"},)"
          R"({"type":"button","label":"inverser","onClick":1}]})");
    CHECK(f.Count("init ") == 2);
    element->Stop();
}

TEST_CASE(React_APopupIsUpdatedInPlaceAndClosedOnUnmount) {
    Fixture f;
    auto element = f.Make("popup", R"(
        const { h, useState } = AMK.React;
        const root = AMK.ReactDOM.createRoot("popup:infos");
        function Info() {
          const [n, setN] = useState(0);
          return h("row", null,
            h("button", { label: "vu " + n, onClick: () => setN(n + 1) }),
            h("button", { label: "fermer", onClick: () => root.unmount() }));
        }
        root.render(h(Info));
    )", R"("ui.popup")");
    CHECK(element->Start());
    CHECK(f.host.ui.Popups().size() == 1);
    element->DeliverClick(1);
    element->Pump();
    auto popups = f.host.ui.Popups();
    CHECK(popups.size() == 1);
    CHECK(popups[0].id == "infos");
    CHECK(popups[0].tree.Dump() ==
          R"({"type":"row","children":[{"type":"button","label":"vu 1","onClick":1},{"type":"button","label":"fermer","onClick":2}]})");
    element->DeliverClick(2);
    element->Pump();
    CHECK(f.host.ui.Popups().empty());
    element->Stop();

    Fixture g;
    auto denied = g.Make("nopopup", R"(AMK.ReactDOM.render(AMK.React.h("text", null, "x"), "popup");)", "");
    CHECK(!denied->Start());
    CHECK(denied->lastError().find("ui.popup") != std::string::npos);
}

TEST_CASE(React_ReadyResolvesOnTheFirstPump) {
    Fixture f;
    auto element = f.Make("ready", R"(AMK.ready.then(() => AMK.log("prêt"));)", "");
    CHECK(element->Start());
    CHECK(!f.Logged("prêt"));
    element->Pump();
    CHECK(f.Logged("[ready] prêt"));
    element->Stop();
}

TEST_CASE(React_AnAppRenderFunctionGoesToItsPage) {
    Fixture f;
    auto element = f.Make("appdemo", R"(
        const { h, useState } = AMK.React;
        function Home() {
          const [n, setN] = useState(0);
          return h("button", { label: "Page " + n, onClick: () => setN(n + 1) });
        }
        function render() { return h(Home); }
        AMK.ready.then(() => AMK.log("page " + (typeof render)));
    )", R"("ui.page")", "app");
    CHECK(element->Start());
    element->Pump();
    auto page = f.host.ui.Page("appdemo", "/appdemo");
    CHECK(page.has_value());
    if (page) CHECK(page->tree.Dump() == R"({"type":"button","label":"Page 0","onClick":1})");
    element->DeliverClick(1);
    element->Pump();
    page = f.host.ui.Page("appdemo", "/appdemo");
    if (page) CHECK(page->tree.Dump() == R"({"type":"button","label":"Page 1","onClick":1})");
    CHECK(f.Logged("page function"));
    element->Stop();
}

TEST_CASE(React_MistakesAreReportedInFrench) {
    Fixture f;
    auto text = f.Make("badchild", R"(
        AMK.ReactDOM.render(AMK.React.h("text", null, AMK.React.h("button")), "panel:x");
    )");
    CHECK(!text->Start());
    CHECK(text->lastError().find("n'accepte que du texte") != std::string::npos);

    auto target = f.Make("badtarget", R"(AMK.ReactDOM.render(AMK.React.h("text"), "fenetre");)");
    CHECK(!target->Start());
    CHECK(target->lastError().find("cible inconnue") != std::string::npos);

    auto hook = f.Make("badhook", R"(
        try { AMK.React.useState(0); } catch (error) { AMK.log(error.message); }
    )");
    CHECK(hook->Start());
    CHECK(f.Logged("un hook ne s'appelle que pendant le rendu"));
    hook->Stop();

    auto denied = f.Make("nopanel", R"(AMK.ReactDOM.render(AMK.React.h("text"), "panel:x");)", "");
    CHECK(!denied->Start());
    CHECK(denied->lastError().find("ui.panel") != std::string::npos);
}

TEST_CASE(React_AStateThatChangesOnEveryRenderIsCut) {
    Fixture f;
    auto element = f.Make("loop", R"(
        const { h, useState, useEffect } = AMK.React;
        function Loop() {
          const [n, setN] = useState(0);
          useEffect(() => { setN(n + 1); });
          return h("text", null, String(n));
        }
        AMK.ReactDOM.render(h(Loop), "panel:boucle");
    )");
    CHECK(element->Start());
    CHECK(f.Logged("rendus à la suite"));
    CHECK(element->running());
    element->Stop();
}

TEST_CASE(React_TopbarButtonsStillGetTheirClicks) {
    Fixture f;
    auto element = f.Make("mixed", R"(
        const { h } = AMK.React;
        AMK.ReactDOM.render(h("button", { label: "dans le panneau", onClick: () => AMK.log("panneau") }), "panel:p");
        AMK.ui.topbar.addButton({ label: "Barre", onClick: () => AMK.log("barre") });
    )", R"("ui.panel","ui.topbar")");
    CHECK(element->Start());
    auto controls = f.host.ui.Controls();
    CHECK(controls.size() == 1);
    CHECK(controls[0].id == 2);  // the callback id 1 went to the panel's button: one counter for both
    element->DeliverClick(controls[0].id);
    element->DeliverClick(1);
    element->Pump();
    CHECK(f.Logged("[mixed] barre"));
    CHECK(f.Logged("[mixed] panneau"));
    element->Stop();
}

TEST_CASE(React_TheReactPanelExampleRuns) {
    Fixture f;
    const std::string name = "react-panel";
    fs::path dir = fs::path(AMK_SOURCE_DIR) / "examples" / name;
    std::ifstream manifestFile(dir / "manifest.json");
    std::stringstream manifestText;
    manifestText << manifestFile.rdbuf();
    amk::ManifestResult result = amk::ParseManifest(manifestText.str(), name, 1, [&](const std::string& file) {
        return fs::exists(dir / file);
    });
    CHECK(result.manifest.has_value());
    if (!result.manifest) return;

    amk::ScriptExtension element(*result.manifest, dir, f.host, f.journal);
    CHECK(element.Start());
    element.Pump();
    CHECK(f.Logged("Panneau React prêt"));
    auto slot = f.host.ui.Panel(name, "react-panel");
    CHECK(slot.has_value());
    if (!slot) return;
    std::string error;
    CHECK(amk::ValidateTree(slot->tree, &error));
    CHECK(FindNode(slot->tree, "title", "Minuterie") == nullptr);

    int plus = CallbackOf(f.host.ui, name, "react-panel", "label", "Plus un", "onClick");
    element.DeliverClick(plus);
    element.Pump();
    slot = f.host.ui.Panel(name, "react-panel");
    CHECK(slot && FindNode(slot->tree, "text", "Clics : 1") != nullptr);

    int view = CallbackOf(f.host.ui, name, "react-panel", "label", "Affichage", "onChange");
    element.DeliverEvent("change", R"({"id":)" + std::to_string(view) + R"(,"value":"complet"})");
    element.Pump();
    int toggle = CallbackOf(f.host.ui, name, "react-panel", "label", "Activer", "onChange");
    CHECK(toggle > 0);
    element.DeliverEvent("change", R"({"id":)" + std::to_string(toggle) + R"(,"value":true})");
    element.Pump();
    CHECK(f.Logged("Minuterie armée : 30 min"));
    element.Stop();
    CHECK(f.Logged("Minuterie annulée"));
}
