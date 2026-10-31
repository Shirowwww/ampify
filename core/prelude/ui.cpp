// Module ui: Ampify.ui. The kit keeps what the element asks for, and the Windows host draws it (M8).
// Natives: core/natives/ui.cpp.

namespace ampify::prelude {

extern const char kUi[];
const char kUi[] = R"AMPIFYJS(
  const uiCallbacks = new Map();

  const treeText = (vue) => {
    const json = JSON.stringify(vue);
    if (json === undefined) throw new TypeError("the interface must be an object");
    return json;
  };

  const addControl = (placement, options) => {
    const note = options || {};
    if (typeof note.onClick !== "function") throw new TypeError("ui: onClick expects a function");
    const label = note.label === undefined ? "" : text(note.label);
    const icon = typeof note.icon === "string" ? note.icon : "";
    const id = native.uiControl(placement, label, icon);
    uiCallbacks.set(id, note.onClick);
    return id;
  };

  Ampify.ui = Object.freeze({
    topbar: Object.freeze({ addButton: (options) => addControl("topbar", options) }),
    playbar: Object.freeze({ addButton: (options) => addControl("playbar", options) }),
    sidebar: Object.freeze({ addItem: (options) => addControl("sidebar", options) }),
    menu: Object.freeze({
      addItem(kind, options) {
        if (kind !== "context" && kind !== "app") throw new RangeError('menu.addItem expects "context" or "app"');
        return addControl("menu-" + kind, options);
      },
    }),
    popup: Object.freeze({ open: (vue) => native.uiPopup(treeText(vue)) }),
    panel: Object.freeze({ register: (id, vue) => native.uiPanel(text(id), treeText(vue)) }),
    page: Object.freeze({ register: (path, vue) => native.uiPage(text(path), treeText(vue)) }),
    title: Object.freeze({ set: (title) => native.uiTitle(text(title)) }),
    navigate: (path) => native.uiNavigate(text(path)),
  });

  dispatch.click = (id) => {
    const fn = uiCallbacks.get(id);
    if (fn) runGuarded("button " + id, fn, []);
  };
)AMPIFYJS";

}  // namespace ampify::prelude
