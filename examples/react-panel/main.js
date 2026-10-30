// Un panneau écrit comme une extension Spicetify en React, sans JSX : AMK.React remplace React, AMK.components
// remplace Spicetify.ReactComponent, et AMK.ReactDOM.render envoie l'arbre dans un panneau du kit.
// Voir docs/extensions/API.md, section 5.7.
const { h, useState, useEffect, createContext, useContext } = AMK.React;
const { Card, Button, Toggle, Slider, Dropdown, Tooltip, Text } = AMK.components;

// Un réglage partagé par tous les composants du panneau.
const Unit = createContext("min");

function Counter() {
  const [count, setCount] = useState(0);
  return h(Card, { title: "Compteur" },
    h(Text, null, "Clics : " + count),
    h(Tooltip, { label: "Ajoute un au compteur", placement: "top" },
      h(Button, { label: "Plus un", onClick: () => setCount((n) => n + 1) })));
}

function SleepTimer() {
  const unit = useContext(Unit);
  const [enabled, setEnabled] = useState(false);
  const [minutes, setMinutes] = useState(30);

  // L'effet arme la minuterie, et son nettoyage l'annule : quand on la coupe, change la durée ou ferme la carte.
  useEffect(() => {
    if (!enabled) return undefined;
    AMK.log("Minuterie armée : " + minutes + " " + unit);
    const timer = setTimeout(() => AMK.log("Minuterie terminée"), minutes * 60000);
    return () => {
      clearTimeout(timer);
      AMK.log("Minuterie annulée");
    };
  }, [enabled, minutes]);

  return h(Card, { title: "Minuterie" },
    h(Toggle, { label: "Activer", value: enabled, onChange: setEnabled }),
    h(Slider, { label: "Durée (" + unit + ")", min: 5, max: 120, step: 5, value: minutes, onChange: setMinutes }));
}

function Panel() {
  const [view, setView] = useState("simple");
  return h(Unit.Provider, { value: "min" },
    h("stack", null,
      h(Text, { variant: "title" }, "Panneau React"),
      h(Dropdown, {
        label: "Affichage",
        value: view,
        options: [{ value: "simple", label: "Simple" }, { value: "complet", label: "Complet" }],
        onChange: setView,
      }),
      h(Counter),
      view === "complet" ? h(SleepTimer) : null));
}

AMK.ReactDOM.render(h(Panel), "panel:react-panel");
AMK.ready.then(() => AMK.log("Panneau React prêt"));
