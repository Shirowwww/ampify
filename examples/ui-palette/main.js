// A panel that shows the layout and control nodes of the interface palette: a grid of cells, tabs and a menu of items.
// Ampify.components has Grid, Tabs and Menu; each one is also a plain node (grid, tabs, tab, menu, item). See
// docs/extensions/api.md, sections 5.5 and 5.9.
const { h, useState } = Ampify.React;
const { Card, Button, Grid, Tabs, Menu, Text } = Ampify.components;

// The texts follow the Windows display language, English otherwise.
Ampify.i18n.define({
  en: {
    title: "UI palette", gridTab: "Grid", menuTab: "Menu", cell: "Cell {number}: {count}", more: "More",
    reset: "Reset the cells", off: "Not available", hint: "Each click on a cell adds one to it.",
  },
  fr: {
    title: "Palette d'interface", gridTab: "Grille", menuTab: "Menu", cell: "Case {number} : {count}", more: "Plus",
    reset: "Remettre les cases à zéro", off: "Indisponible", hint: "Chaque clic sur une case l'augmente de un.",
  },
  de: {
    title: "UI-Palette", gridTab: "Raster", menuTab: "Menü", cell: "Feld {number}: {count}", more: "Mehr",
    reset: "Felder zurücksetzen", off: "Nicht verfügbar", hint: "Jeder Klick auf ein Feld zählt es um eins hoch.",
  },
  es: {
    title: "Paleta de interfaz", gridTab: "Cuadrícula", menuTab: "Menú", cell: "Celda {number}: {count}", more: "Más",
    reset: "Reiniciar las celdas", off: "No disponible", hint: "Cada clic en una celda le suma uno.",
  },
  pt: {
    title: "Paleta de interface", gridTab: "Grade", menuTab: "Menu", cell: "Célula {number}: {count}", more: "Mais",
    reset: "Zerar as células", off: "Indisponível", hint: "Cada clique numa célula soma um.",
  },
  ru: {
    title: "Палитра интерфейса", gridTab: "Сетка", menuTab: "Меню", cell: "Ячейка {number}: {count}", more: "Ещё",
    reset: "Сбросить ячейки", off: "Недоступно", hint: "Каждый щелчок по ячейке прибавляет к ней единицу.",
  },
  ja: {
    title: "UI パレット", gridTab: "グリッド", menuTab: "メニュー", cell: "セル {number}: {count}", more: "その他",
    reset: "セルをリセット", off: "使えません", hint: "セルをクリックするたびに 1 増えます。",
  },
  zh: {
    title: "界面组件", gridTab: "网格", menuTab: "菜单", cell: "格子 {number}：{count}", more: "更多",
    reset: "重置格子", off: "不可用", hint: "每点击一次格子，它就加一。",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const CELLS = 4;

function Palette() {
  const [tab, setTab] = useState("grid");
  const [counts, setCounts] = useState(Array(CELLS).fill(0));
  const bump = (index) => setCounts(counts.map((n, i) => (i === index ? n + 1 : n)));

  // Tabs: the one of `tab` is on; the content below it follows.
  const picker = h(Tabs, {
    value: tab,
    onChange: setTab,
    options: [{ value: "grid", label: t("gridTab") }, { value: "menu", label: t("menuTab") }],
  });

  if (tab === "menu") {
    return h("stack", { gap: 12 }, picker,
      h(Menu, {
        label: t("more"),
        items: [
          { label: t("reset"), onClick: () => setCounts(Array(CELLS).fill(0)) },
          { label: t("off"), disabled: true },
        ],
      }));
  }
  return h("stack", { gap: 12 }, picker,
    h(Text, { variant: "caption" }, t("hint")),
    h(Grid, { columns: 2, gap: 8 },
      ...counts.map((count, index) =>
        h(Button, { key: String(index), label: t("cell", { number: index + 1, count }), onClick: () => bump(index) }))));
}

Ampify.ReactDOM.render(h(Card, { title: t("title") }, h(Palette)), "panel:ui-palette");
Ampify.ready.then(() => Ampify.log("UI palette ready"));
