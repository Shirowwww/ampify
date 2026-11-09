// Module ui: Ampify.ui. The kit keeps what the element asks for, and the Windows host draws it (M8).
// Natives: src/core/natives/ui.cpp.

namespace ampify::prelude {

extern const char kUi[];
const char kUi[] = R"AMPIFYJS(
  const uiCallbacks = new Map();

  // A tree written by hand may hold onClick and onChange functions, as a React tree does: each becomes a callback id
  // kept per surface and node path, so a tree sent again reuses its ids and the handlers of a gone node are dropped.
  const rawCallbacks = new Map();  // callback id -> function
  const rawSlots = new Map();      // surface -> Map(node path and event -> callback id)

  const callbackFor = (slots, slot, fn) => {
    let id = slots.get(slot);
    if (id === undefined) {
      id = native.reactHandlerId();
      slots.set(slot, id);
    }
    rawCallbacks.set(id, fn);
    return id;
  };

  const childPath = (path, child, index) =>
    path + "/" + (child && typeof child === "object" && child.key !== undefined ? "k" + child.key : index);

  const withCallbacks = (node, slots, path, used) => {
    if (!node || typeof node !== "object" || Array.isArray(node)) return node;
    const copy = {};
    for (const key of Object.keys(node)) {
      const value = node[key];
      if ((key === "onClick" || key === "onChange") && typeof value === "function") {
        used.add(path + "." + key);
        copy[key] = callbackFor(slots, path + "." + key, value);
      } else if (key === "children" && Array.isArray(value)) {
        copy.children = value.map((child, index) => withCallbacks(child, slots, childPath(path, child, index), used));
      } else {
        copy[key] = value;
      }
    }
    return copy;
  };

  const treeText = (vue, surface) => {
    let tree = vue;
    if (surface !== undefined) {
      const slots = rawSlots.get(surface) || new Map();
      rawSlots.set(surface, slots);
      const used = new Set();
      tree = withCallbacks(vue, slots, "", used);
      for (const [slot, id] of [...slots]) {
        if (used.has(slot)) continue;
        slots.delete(slot);
        rawCallbacks.delete(id);
      }
    }
    const json = JSON.stringify(tree);
    if (json === undefined) throw new TypeError("the interface must be an object");
    return json;
  };

  // A surface that is gone (a panel removed or closed, the fullscreen view closed) lets go of its handlers.
  const dropSurface = (surface) => {
    const slots = rawSlots.get(surface);
    if (!slots) return;
    for (const id of slots.values()) rawCallbacks.delete(id);
    rawSlots.delete(surface);
  };

  // A named popup (popup.show): shown again under the same name, it changes in place. popup.close takes the path of
  // the user's Close (dispatch.popupClose, module react), which also unmounts a ReactDOM popup of that name.
  const showPopup = (name, vue) => {
    const surface = "popup:" + text(name);
    const fresh = !rawSlots.has(surface);  // a refused update leaves the popup on screen, with its handlers
    try {
      native.reactPopup(text(name), treeText(vue, surface));
    } catch (error) {
      if (fresh) dropSurface(surface);
      throw error;
    }
  };
  dispatch.uiPopupClosed = (name) => dropSurface("popup:" + text(name));

  // Each popup.open is a popup of its own, stacked over the others: one surface each, so two popups of the same shape
  // never share a callback id. The host tells nothing when an unnamed popup closes, so its handlers stay.
  let popupSerial = 0;
  const openPopup = (vue) => {
    const surface = "popup:#" + ++popupSerial;
    try {
      return native.uiPopup(treeText(vue, surface));
    } catch (error) {
      dropSurface(surface);
      throw error;
    }
  };

  // A top bar or play bar button without onClick is a text that only shows something (Volume percentage).
  const addControl = (placement, options, parent) => {
    const note = options || {};
    const kinds = kindsOf(placement, note.kinds);
    const bar = placement === "topbar" || placement === "playbar";
    const clickable = !(bar && note.onClick === undefined);
    if (clickable && typeof note.onClick !== "function") throw new TypeError("ui: onClick expects a function");
    const label = note.label === undefined ? "" : text(note.label);
    const icon = typeof note.icon === "string" ? note.icon : "";
    const id = native.uiControl(placement, label, icon, clickable, parent || 0);
    if (clickable) uiCallbacks.set(id, note.onClick);
    if (note.active) native.uiControlActive(id, true);
    if (note.disabled) native.uiControlDisabled(id, true);
    applyKinds(id, kinds);
    return id;
  };

  // A stretch of the progress line for a play bar button, as parts of the song (0 to 1): no `to` marks the start alone,
  // no `from` clears it. The kernel keeps them on the line and in order.
  const markLine = (id, from, to) => {
    const part = (value) => {
      if (value === undefined || value === null) return -1;
      if (typeof value !== "number" || !Number.isFinite(value)) throw new TypeError("ui: mark expects numbers from 0 to 1");
      return Math.max(0, value);
    };
    const start = part(from);
    return native.uiControlMark(Number(id), start, start < 0 ? -1 : part(to));
  };

  // The kernel checks each kind's name; an unknown one takes the new entry out again before throwing.
  const applyKinds = (id, kinds) => {
    if (!kinds) return;
    try {
      native.uiControlKinds(id, kinds);
    } catch (error) {
      native.uiControlRemove(id);
      uiCallbacks.delete(id);
      throw error;
    }
  };

  // A context menu entry's `kinds` (["song", "album"]): the menus it goes in. Checked before the entry exists, as text.
  const kindsOf = (placement, kinds) => {
    if (kinds === undefined) return "";
    if (placement !== "menu-context") throw new TypeError("ui: kinds is for the entries of context menus");
    if (!Array.isArray(kinds) || kinds.length === 0 || kinds.some((kind) => typeof kind !== "string" || kind.includes(","))) {
      throw new TypeError("menu.addItem: kinds expects a list of kinds of item, such as [\"song\", \"album\"]");
    }
    return kinds.join(",");
  };

  // Takes out a button or menu entry this element added; its click handler and a submenu's entries go with it.
  const removeControl = (id) => {
    const control = Number(id);
    const removed = native.uiControlRemove(control);
    if (removed) uiCallbacks.delete(control);
    return removed;
  };

  const kMaxSubmenuEntries = JSON.parse(native.uiVocabulary).maxSubmenuEntries;

  // A menu entry with `items` is a submenu: its entries are { label, icon, onClick }, one level only.
  const addMenuItem = (placement, options) => {
    const note = options || {};
    if (note.items === undefined) return addControl(placement, note, 0);
    if (!Array.isArray(note.items) || note.items.length === 0 || note.items.length > kMaxSubmenuEntries) {
      throw new TypeError("menu.addItem: items expects 1 to " + kMaxSubmenuEntries + " entries");
    }
    // Every entry is checked before the submenu exists: a bad one must not leave a half-built menu behind.
    const kinds = kindsOf(placement, note.kinds);
    for (const entry of note.items) {
      if (entry && entry.items !== undefined) throw new TypeError("menu.addItem: a submenu holds entries, not submenus");
      if (!entry || typeof entry.onClick !== "function") throw new TypeError("menu.addItem: each entry expects an onClick function");
    }
    const label = note.label === undefined ? "" : text(note.label);
    const icon = typeof note.icon === "string" ? note.icon : "";
    const submenu = native.uiControl(placement, label, icon, false, 0);
    if (note.disabled) native.uiControlDisabled(submenu, true);
    applyKinds(submenu, kinds);
    for (const entry of note.items) addControl(placement, entry, submenu);
    return submenu;
  };

  // The fullscreen surface: the session the kernel gave (0 while closed), and the onClose handlers. They run when
  // the user closes it (button, Escape) or another element takes the screen, not on close().
  let fullscreenSession = 0;
  const fullscreenClosers = new Set();

  const fullscreen = Object.freeze({
    open(vue) {
      const session = native.uiFullscreen(treeText(vue, "fullscreen"), fullscreenSession);
      if (session) fullscreenSession = session;
    },
    close() {
      if (fullscreenSession) native.uiFullscreenClose(fullscreenSession);
      fullscreenSession = 0;
      dropSurface("fullscreen");
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
    dropSurface("fullscreen");
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
    dropSurface("panel:" + id);
    for (const fn of [...(panelClosers.get(id) || [])]) runGuarded("panel onClose", fn, []);
  };

  Ampify.ui = Object.freeze({
    topbar: Object.freeze({ addButton: (options) => addControl("topbar", options), remove: (id) => removeControl(id) }),
    playbar: Object.freeze({
      addButton: (options) => addControl("playbar", options),
      remove: (id) => removeControl(id),
      mark: (id, from, to) => markLine(id, from, to),
    }),
    sidebar: Object.freeze({ addItem: (options) => addControl("sidebar", options), remove: (id) => removeControl(id) }),
    menu: Object.freeze({
      addItem(kind, options) {
        if (kind !== "context" && kind !== "app") throw new RangeError('menu.addItem expects "context" or "app"');
        return addMenuItem("menu-" + kind, options);
      },
      remove: (id) => removeControl(id),
    }),
    popup: Object.freeze({
      open: (vue) => openPopup(vue),
      show: (name, vue) => showPopup(name, vue),
      close: (name) => dispatch.popupClose(JSON.stringify({ popup: text(name) })),
    }),
    fullscreen,
    applePlayer: Object.freeze({ open: () => native.uiApplePlayer() }),
    panel: Object.freeze({
      register: (id, vue) => native.uiPanel(text(id), treeText(vue, "panel:" + text(id))),
      remove(id) {
        const removed = native.uiPanelRemove(text(id));
        dropSurface("panel:" + text(id));
        return removed;
      },
      onClose(id, fn) {
        if (typeof fn !== "function") throw new TypeError("panel.onClose expects a function");
        const key = text(id);
        if (!panelClosers.has(key)) panelClosers.set(key, new Set());
        panelClosers.get(key).add(fn);
        return () => panelClosers.get(key).delete(fn);
      },
    }),
    page: Object.freeze({ register: (path, vue) => native.uiPage(text(path), treeText(vue, "page:" + text(path))) }),
    title: Object.freeze({
      set: (title) => native.uiTitle(text(title)),
      get: () => native.uiTitleGet(),
      reset: () => native.uiTitleReset(),
    }),
    // A button of the top bar or the play bar this element added, by the id addButton gave.
    setLabel: (id, label) => native.uiControlLabel(Number(id), text(label)),
    // A top bar or play bar button this element added, on or off: on, it takes the accent colour (addButton's `active`).
    setActive: (id, on) => native.uiControlActive(Number(id), Boolean(on)),
    // A button or menu entry this element added, greyed (it ignores clicks) or back to normal.
    setDisabled: (id, disabled) => native.uiControlDisabled(Number(id), Boolean(disabled)),
    navigate(path) {
      native.uiNavigate(text(path));
      if (dispatch.pageShow) dispatch.pageShow(text(path));  // module react
    },
  });

  dispatch.click = (id) => {
    const fn = uiCallbacks.get(id);
    if (fn) return runGuarded("button " + id, fn, []);
    const raw = rawCallbacks.get(id);
    if (raw) runGuarded("click " + id, raw, [{ type: "click", id }]);
  };

  // A new value for a hand-written tree, as JSON text: {"id": <callback id>, "value": <boolean, number or text>}.
  dispatch.change = (payload) => {
    let data;
    try {
      data = JSON.parse(payload);
    } catch (error) {
      native.log("[error] change: " + describe(error));
      return;
    }
    const id = Number(data && data.id);
    const raw = rawCallbacks.get(id);
    if (!raw) return;
    const value = data.value;
    runGuarded("change " + id, raw, [{ type: "change", id, value, target: { value, checked: value === true } }]);
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
