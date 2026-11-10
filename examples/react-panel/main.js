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
  de: {
    title: "React-Panel", view: "Ansicht", simple: "Einfach", full: "Vollständig", counter: "Zähler",
    clicks: "Klicks: {count}", addOne: "Eins addieren", addOneHelp: "Zählt den Zähler um eins hoch", timer: "Timer",
    turnOn: "Aktivieren", length: "Dauer ({unit})",
  },
  es: {
    title: "Panel React", view: "Vista", simple: "Simple", full: "Completa", counter: "Contador",
    clicks: "Clics: {count}", addOne: "Sumar uno", addOneHelp: "Suma uno al contador", timer: "Temporizador",
    turnOn: "Activar", length: "Duración ({unit})",
  },
  pt: {
    title: "Painel React", view: "Visualização", simple: "Simples", full: "Completa", counter: "Contador",
    clicks: "Cliques: {count}", addOne: "Somar um", addOneHelp: "Soma um ao contador", timer: "Timer",
    turnOn: "Ativar", length: "Duração ({unit})",
  },
  ru: {
    title: "Панель React", view: "Вид", simple: "Простой", full: "Полный", counter: "Счётчик",
    clicks: "Нажатий: {count}", addOne: "Прибавить один", addOneHelp: "Прибавляет единицу к счётчику", timer: "Таймер",
    turnOn: "Включить", length: "Длительность ({unit})",
  },
  ja: {
    title: "React パネル", view: "表示", simple: "シンプル", full: "フル", counter: "カウンター",
    clicks: "クリック数： {count}", addOne: "1 増やす", addOneHelp: "カウンターを 1 増やします", timer: "タイマー",
    turnOn: "オンにする", length: "長さ（{unit}）",
  },
  zh: {
    title: "React 面板", view: "视图", simple: "简单", full: "完整", counter: "计数器",
    clicks: "点击次数：{count}", addOne: "加一", addOneHelp: "给计数器加一", timer: "定时器",
    turnOn: "开启", length: "时长（{unit}）",
  },
  it: {
    title: "Pannello React", view: "Visualizzazione", simple: "Semplice", full: "Completa", counter: "Contatore",
    clicks: "Clic: {count}", addOne: "Aggiungi uno", addOneHelp: "Aggiunge uno al contatore", timer: "Timer",
    turnOn: "Attiva", length: "Durata ({unit})",
  },
  ko: {
    title: "React 패널", view: "보기", simple: "간단히", full: "전체", counter: "카운터",
    clicks: "클릭 수: {count}", addOne: "1 더하기", addOneHelp: "카운터에 1을 더합니다", timer: "타이머",
    turnOn: "켜기", length: "길이({unit})",
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
      h(Dropdown, {
        label: t("view"),
        value: view,
        options: [{ value: "simple", label: t("simple") }, { value: "full", label: t("full") }],
        onChange: setView,
      }),
      h(Counter),
      view === "full" ? h(SleepTimer) : null));
}

// The panel's frame shows the extension's name above the view, so the view does not repeat it.
Ampify.ReactDOM.render(h(Panel), "panel:react-panel");
Ampify.ready.then(() => Ampify.log("React panel ready"));
