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

  // The fullscreen surface: the session the kernel gave (0 while closed), and the onClose handlers. They run when
  // the user closes it (button, Escape) or another element takes the screen, not on close().
  let fullscreenSession = 0;
  const fullscreenClosers = new Set();

  const fullscreen = Object.freeze({
    open(vue) {
      const session = native.uiFullscreen(treeText(vue), fullscreenSession);
      if (session) fullscreenSession = session;
    },
    close() {
      if (fullscreenSession) native.uiFullscreenClose(fullscreenSession);
      fullscreenSession = 0;
    },
    onClose(fn) {
      if (typeof fn !== "function") throw new TypeError("fullscreen.onClose expects a function");
      fullscreenClosers.add(fn);
      return () => fullscreenClosers.delete(fn);
    },
  });

  // From the host, as JSON text: {"session": <the session that was closed>}. An older session is ignored.
  dispatch.fullscreenClose = (payload) => {
    let session = 0;
    try {
      session = Number(JSON.parse(payload).session);
    } catch (error) {
      native.log("[error] fullscreenClose: " + describe(error));
      return;
    }
    if (!session || session !== fullscreenSession) return;
    fullscreenSession = 0;
    native.uiFullscreenClose(session);
    for (const fn of [...fullscreenClosers]) runGuarded("fullscreen onClose", fn, []);
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
    fullscreen,
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
