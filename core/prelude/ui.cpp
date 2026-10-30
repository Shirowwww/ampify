// Module ui: AMK.ui. The kit keeps what the element asks for, and the Windows host draws it (M8).
// Natives: core/natives/ui.cpp.

namespace amk::prelude {

extern const char kUi[];
const char kUi[] = R"AMKJS(
  const uiCallbacks = new Map();

  const treeText = (vue) => {
    const json = JSON.stringify(vue);
    if (json === undefined) throw new TypeError("l'interface doit être un objet");
    return json;
  };

  const addControl = (placement, options) => {
    const note = options || {};
    if (typeof note.onClick !== "function") throw new TypeError("ui : onClick attend une fonction");
    const label = note.label === undefined ? "" : text(note.label);
    const icon = typeof note.icon === "string" ? note.icon : "";
    const id = native.uiControl(placement, label, icon);
    uiCallbacks.set(id, note.onClick);
    return id;
  };

  AMK.ui = Object.freeze({
    topbar: Object.freeze({ addButton: (options) => addControl("topbar", options) }),
    playbar: Object.freeze({ addButton: (options) => addControl("playbar", options) }),
    menu: Object.freeze({
      addItem(kind, options) {
        if (kind !== "context" && kind !== "app") throw new RangeError('menu.addItem attend "context" ou "app"');
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
    if (fn) runGuarded("bouton " + id, fn, []);
  };
)AMKJS";

}  // namespace amk::prelude
