// A panel written like a Spicetify extension in React, without JSX: Ampify.React replaces React, Ampify.components
// replaces Spicetify.ReactComponent, and Ampify.ReactDOM.render sends the tree to an Ampify panel.
// See docs/extensions/api.md, section 5.7.
const { h, useState, useEffect, createContext, useContext } = Ampify.React;
const { Card, Button, Toggle, Slider, Dropdown, Tooltip, Text } = Ampify.components;

// The texts follow the Windows display language, English otherwise.
Ampify.i18n.define({
  en: {
    title: "React panel", view: "View", simple: "Simple", full: "Full", counter: "Counter", clicks: "Clicks: {count}",
    addOne: "Add one", addOneHelp: "Adds one to the counter", timer: "Timer", turnOn: "Turn on", length: "Length ({unit})",
  },
  fr: {
    title: "Panneau React", view: "Affichage", simple: "Simple", full: "Complet", counter: "Compteur",
    clicks: "Clics : {count}", addOne: "Ajouter un", addOneHelp: "Ajoute un au compteur", timer: "Minuteur",
    turnOn: "Activer", length: "Durée ({unit})",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

// A setting shared by all the components of the panel.
const Unit = createContext("min");

function Counter() {
  const [count, setCount] = useState(0);
  return h(Card, { title: t("counter") },
    h(Text, null, t("clicks", { count })),
    h(Tooltip, { label: t("addOneHelp"), placement: "top" },
      h(Button, { label: t("addOne"), onClick: () => setCount((n) => n + 1) })));
}

function SleepTimer() {
  const unit = useContext(Unit);
  const [enabled, setEnabled] = useState(false);
  const [minutes, setMinutes] = useState(30);

  // The effect arms the timer, and its cleanup cancels it: when it is turned off, the length changes or the card closes.
  useEffect(() => {
    if (!enabled) return undefined;
    Ampify.log("Timer armed: " + minutes + " " + unit);
    const timer = setTimeout(() => Ampify.log("Timer finished"), minutes * 60000);
    return () => {
      clearTimeout(timer);
      Ampify.log("Timer cancelled");
    };
  }, [enabled, minutes]);

  return h(Card, { title: t("timer") },
    h(Toggle, { label: t("turnOn"), value: enabled, onChange: setEnabled }),
    h(Slider, { label: t("length", { unit }), min: 5, max: 120, step: 5, value: minutes, onChange: setMinutes }));
}

function Panel() {
  const [view, setView] = useState("simple");
  return h(Unit.Provider, { value: "min" },
    h("stack", null,
      h(Text, { variant: "title" }, t("title")),
      h(Dropdown, {
        label: t("view"),
        value: view,
        options: [{ value: "simple", label: t("simple") }, { value: "full", label: t("full") }],
        onChange: setView,
      }),
      h(Counter),
      view === "full" ? h(SleepTimer) : null));
}

Ampify.ReactDOM.render(h(Panel), "panel:react-panel");
Ampify.ready.then(() => Ampify.log("React panel ready"));
