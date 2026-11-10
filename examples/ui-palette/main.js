// A panel that shows the layout and control nodes of the interface palette: a grid of cells, tabs, a menu of items,
// and on its controls tab a check box, a spinner, a progress bar, a divider and a button greyed while it has nothing
// to do. Each component of Ampify.components is also a plain node (grid, tabs, tab, menu, item, checkbox, spinner,
// progress, divider). See docs/extensions/api.md, sections 5.5 and 5.9.
const { h, useState } = Ampify.React;
const { Card, Button, Grid, Tabs, Menu, Text, Checkbox, ProgressBar, Spinner, Divider } = Ampify.components;

// The texts follow the Windows display language, English otherwise.
Ampify.i18n.define({
  en: {
    title: "UI palette", gridTab: "Grid", menuTab: "Menu", controlsTab: "Controls", cell: "Cell {number}: {count}",
    more: "More", reset: "Reset the cells", off: "Not available", hint: "Each click on a cell adds one to it.",
    busy: "Busy", progress: "{count} of {max} clicks",
  },
  fr: {
    title: "Palette d'interface", gridTab: "Grille", menuTab: "Menu", controlsTab: "Contrôles",
    cell: "Case {number} : {count}", more: "Plus", reset: "Remettre les cases à zéro", off: "Indisponible",
    hint: "Chaque clic sur une case l'augmente de un.", busy: "Occupé", progress: "{count} clics sur {max}",
  },
  de: {
    title: "UI-Palette", gridTab: "Raster", menuTab: "Menü", controlsTab: "Steuerelemente",
    cell: "Feld {number}: {count}", more: "Mehr", reset: "Felder zurücksetzen", off: "Nicht verfügbar",
    hint: "Jeder Klick auf ein Feld zählt es um eins hoch.", busy: "Beschäftigt", progress: "{count} von {max} Klicks",
  },
  es: {
    title: "Paleta de interfaz", gridTab: "Cuadrícula", menuTab: "Menú", controlsTab: "Controles",
    cell: "Celda {number}: {count}", more: "Más", reset: "Reiniciar las celdas", off: "No disponible",
    hint: "Cada clic en una celda le suma uno.", busy: "Ocupado", progress: "{count} de {max} clics",
  },
  pt: {
    title: "Paleta de interface", gridTab: "Grade", menuTab: "Menu", controlsTab: "Controles",
    cell: "Célula {number}: {count}", more: "Mais", reset: "Zerar as células", off: "Indisponível",
    hint: "Cada clique numa célula soma um.", busy: "Ocupado", progress: "{count} de {max} cliques",
  },
  ru: {
    title: "Палитра интерфейса", gridTab: "Сетка", menuTab: "Меню", controlsTab: "Элементы управления",
    cell: "Ячейка {number}: {count}", more: "Ещё", reset: "Сбросить ячейки", off: "Недоступно",
    hint: "Каждый щелчок по ячейке прибавляет к ней единицу.", busy: "Занято", progress: "Щелчков: {count} из {max}",
  },
  ja: {
    title: "UI パレット", gridTab: "グリッド", menuTab: "メニュー", controlsTab: "コントロール", cell: "セル {number}: {count}",
    more: "その他", reset: "セルをリセット", off: "使えません", hint: "セルをクリックするたびに 1 増えます。",
    busy: "処理中", progress: "クリック {count} / {max}",
  },
  zh: {
    title: "界面组件", gridTab: "网格", menuTab: "菜单", controlsTab: "控件", cell: "格子 {number}：{count}", more: "更多",
    reset: "重置格子", off: "不可用", hint: "每点击一次格子，它就加一。", busy: "忙碌中", progress: "点击 {count} / {max}",
  },
  it: {
    title: "Palette dell'interfaccia", gridTab: "Griglia", menuTab: "Menu", controlsTab: "Controlli",
    cell: "Cella {number}: {count}", more: "Altro", reset: "Azzera le celle", off: "Non disponibile",
    hint: "Ogni clic su una cella ne aumenta il valore di uno.", busy: "Occupato", progress: "{count} clic su {max}",
  },
  ko: {
    title: "UI 팔레트", gridTab: "그리드", menuTab: "메뉴", controlsTab: "컨트롤", cell: "셀 {number}: {count}",
    more: "더 보기", reset: "셀 초기화", off: "사용할 수 없음", hint: "셀을 클릭할 때마다 1씩 늘어납니다.",
    busy: "작업 중", progress: "클릭 {count} / {max}",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const CELLS = 4;
const MAX_CLICKS = 10;

// The controls tab: while "Busy" is checked, a spinner and a bar that moves on its own; otherwise the bar counts the
// clicks on the cells. The reset button is greyed while every cell is at zero, which themes reach with :disabled.
function Controls({ total, reset }) {
  const [busy, setBusy] = useState(true);
  const bar = busy
    ? h(ProgressBar, {})
    : h(ProgressBar, { value: Math.min(total, MAX_CLICKS), max: MAX_CLICKS,
                      label: t("progress", { count: total, max: MAX_CLICKS }) });
  return h("stack", { gap: 12 },
    h(Checkbox, { label: t("busy"), checked: busy, onChange: setBusy }),
    busy ? h(Spinner, { size: 24 }) : null,
    bar,
    h(Divider, {}),
    h(Button, { label: t("reset"), disabled: total === 0, onClick: reset }));
}

function Palette() {
  const [tab, setTab] = useState("grid");
  const [counts, setCounts] = useState(Array(CELLS).fill(0));
  const bump = (index) => setCounts(counts.map((n, i) => (i === index ? n + 1 : n)));
  const reset = () => setCounts(Array(CELLS).fill(0));
  const total = counts.reduce((sum, n) => sum + n, 0);

  // Tabs: the one of `tab` is selected; the content below it follows.
  const picker = h(Tabs, {
    value: tab,
    onChange: setTab,
    options: [{ value: "grid", label: t("gridTab") }, { value: "menu", label: t("menuTab") },
              { value: "controls", label: t("controlsTab") }],
  });

  if (tab === "menu") {
    return h("stack", { gap: 12 }, picker,
      h(Menu, {
        label: t("more"),
        items: [
          { label: t("reset"), onClick: reset },
          { label: t("off"), disabled: true },
        ],
      }));
  }
  if (tab === "controls") return h("stack", { gap: 12 }, picker, h(Controls, { total, reset }));
  return h("stack", { gap: 12 }, picker,
    h(Text, { variant: "caption" }, t("hint")),
    h(Grid, { columns: 2, gap: 8 },
      ...counts.map((count, index) =>
        h(Button, { key: String(index), label: t("cell", { number: index + 1, count }), onClick: () => bump(index) }))));
}

Ampify.ReactDOM.render(h(Card, { title: t("title") }, h(Palette)), "panel:ui-palette");
Ampify.ready.then(() => Ampify.log("UI palette ready"));
