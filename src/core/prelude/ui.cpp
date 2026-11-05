// Module ui: Ampify.ui. The kit keeps what the element asks for, and the Windows host draws it (M8).
// Natives: src/core/natives/ui.cpp.

namespace ampify::prelude {

extern const char kUi[];
const char kUi[] = R"AMPIFYJS(
  const uiCallbacks = new Map();

  const treeText = (vue) => {
    const json = JSON.stringify(vue);
    if (json === undefined) throw new TypeError("the interface must be an object");
    return json;
  };

  // A top bar or play bar button without onClick is a text that only shows something (Volume percentage).
  const addControl = (placement, options, parent) => {
    const note = options || {};
    const bar = placement === "topbar" || placement === "playbar";
    const clickable = !(bar && note.onClick === undefined);
    if (clickable && typeof note.onClick !== "function") throw new TypeError("ui: onClick expects a function");
    const label = note.label === undefined ? "" : text(note.label);
    const icon = typeof note.icon === "string" ? note.icon : "";
    const id = native.uiControl(placement, label, icon, clickable, parent || 0);
    if (clickable) uiCallbacks.set(id, note.onClick);
    if (note.active) native.uiControlActive(id, true);
    return id;
  };

  const kMaxSubmenuEntries = 20;

  // A menu entry with `items` is a submenu: its entries are { label, icon, onClick }, one level only.
  const addMenuItem = (placement, options) => {
    const note = options || {};
    if (note.items === undefined) return addControl(placement, note, 0);
    if (!Array.isArray(note.items) || note.items.length === 0 || note.items.length > kMaxSubmenuEntries) {
      throw new TypeError("menu.addItem: items expects 1 to " + kMaxSubmenuEntries + " entries");
    }
    // Every entry is checked before the submenu exists: a bad one must not leave a half-built menu behind.
    for (const entry of note.items) {
      if (entry && entry.items !== undefined) throw new TypeError("menu.addItem: a submenu holds entries, not submenus");
      if (!entry || typeof entry.onClick !== "function") throw new TypeError("menu.addItem: each entry expects an onClick function");
    }
    const label = note.label === undefined ? "" : text(note.label);
    const icon = typeof note.icon === "string" ? note.icon : "";
    const submenu = native.uiControl(placement, label, icon, false, 0);
    for (const entry of note.items) addControl(placement, entry, submenu);
    return submenu;
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

  // A panel the user closed with its Close button: it leaves the element's model, as panel.remove does, so register
  // shows it again, and its onClose handlers run (an element keeps its button's state in step with them).
  // From the host, as JSON text: {"panel": "<id>"}.
  const panelClosers = new Map();
  dispatch.panelClose = (payload) => {
    let id = "";
    try {
      id = String(JSON.parse(payload).panel);
    } catch (error) {
      native.log("[error] panelClose: " + describe(error));
      return;
    }
    native.uiPanelRemove(id);
    for (const fn of [...(panelClosers.get(id) || [])]) runGuarded("panel onClose", fn, []);
  };

  Ampify.ui = Object.freeze({
    topbar: Object.freeze({ addButton: (options) => addControl("topbar", options) }),
    playbar: Object.freeze({ addButton: (options) => addControl("playbar", options) }),
    sidebar: Object.freeze({ addItem: (options) => addControl("sidebar", options) }),
    menu: Object.freeze({
      addItem(kind, options) {
        if (kind !== "context" && kind !== "app") throw new RangeError('menu.addItem expects "context" or "app"');
        return addMenuItem("menu-" + kind, options);
      },
    }),
    popup: Object.freeze({ open: (vue) => native.uiPopup(treeText(vue)) }),
    fullscreen,
    applePlayer: Object.freeze({ open: () => native.uiApplePlayer() }),
    panel: Object.freeze({
      register: (id, vue) => native.uiPanel(text(id), treeText(vue)),
      remove: (id) => native.uiPanelRemove(text(id)),
      onClose(id, fn) {
        if (typeof fn !== "function") throw new TypeError("panel.onClose expects a function");
        const key = text(id);
        if (!panelClosers.has(key)) panelClosers.set(key, new Set());
        panelClosers.get(key).add(fn);
        return () => panelClosers.get(key).delete(fn);
      },
    }),
    page: Object.freeze({ register: (path, vue) => native.uiPage(text(path), treeText(vue)) }),
    title: Object.freeze({ set: (title) => native.uiTitle(text(title)) }),
    // A button of the top bar or the play bar this element added, by the id addButton gave.
    setLabel: (id, label) => native.uiControlLabel(Number(id), text(label)),
    // A top bar or play bar button this element added, on or off: on, it takes the accent colour (addButton's `active`).
    setActive: (id, on) => native.uiControlActive(Number(id), Boolean(on)),
    navigate(path) {
      native.uiNavigate(text(path));
      if (dispatch.pageShow) dispatch.pageShow(text(path));  // module react
    },
  });

  dispatch.click = (id) => {
    const fn = uiCallbacks.get(id);
    if (fn) runGuarded("button " + id, fn, []);
  };

  // An entry of Apple Music's menus: a context entry receives the name of the item its menu is about and, when Apple
  // Music's data gives it, that item's identifiers and kind: { title, item: { kind, id?, canonicalId? } }.
  dispatch.menuClick = (payload) => {
    let event;
    try {
      event = JSON.parse(payload);
    } catch (error) {
      native.log("[error] menuClick: " + describe(error));
      return;
    }
    const detail = {};
    if (event && event.title) detail.title = String(event.title);
    const item = event && event.item;
    if (item && typeof item === "object") {
      detail.item = { kind: String(item.kind) };
      if (item.id) detail.item.id = String(item.id);
      if (item.canonicalId) detail.item.canonicalId = String(item.canonicalId);
      Object.freeze(detail.item);
    }
    const fn = uiCallbacks.get(Number(event && event.id));
    if (fn) runGuarded("menu " + event.id, fn, [detail]);
  };
)AMPIFYJS";

}  // namespace ampify::prelude
