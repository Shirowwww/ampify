// A panel written like a Spicetify extension in React, without JSX: Ampify.React replaces React, Ampify.components
// replaces Spicetify.ReactComponent, and Ampify.ReactDOM.render sends the tree to an Ampify panel.
// See docs/extensions/api.md, section 5.7.
const { h, useState, useEffect, createContext, useContext } = Ampify.React;
const { Card, Button, Toggle, Slider, Dropdown, Tooltip, Text } = Ampify.components;

// A setting shared by all the components of the panel.
const Unit = createContext("min");

function Counter() {
  const [count, setCount] = useState(0);
  return h(Card, { title: "Counter" },
    h(Text, null, "Clicks: " + count),
    h(Tooltip, { label: "Adds one to the counter", placement: "top" },
      h(Button, { label: "Add one", onClick: () => setCount((n) => n + 1) })));
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

  return h(Card, { title: "Timer" },
    h(Toggle, { label: "Turn on", value: enabled, onChange: setEnabled }),
    h(Slider, { label: "Length (" + unit + ")", min: 5, max: 120, step: 5, value: minutes, onChange: setMinutes }));
}

function Panel() {
  const [view, setView] = useState("simple");
  return h(Unit.Provider, { value: "min" },
    h("stack", null,
      h(Text, { variant: "title" }, "React panel"),
      h(Dropdown, {
        label: "View",
        value: view,
        options: [{ value: "simple", label: "Simple" }, { value: "full", label: "Full" }],
        onChange: setView,
      }),
      h(Counter),
      view === "full" ? h(SleepTimer) : null));
}

Ampify.ReactDOM.render(h(Panel), "panel:react-panel");
Ampify.ready.then(() => Ampify.log("React panel ready"));
