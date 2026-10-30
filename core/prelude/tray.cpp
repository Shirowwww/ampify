// Module tray: AMK.tray, items in the menu of the kit's notification area icon. Natives: core/natives/tray.cpp.
// The host sends the clicks to every element (ExtensionLoader::DeliverEvent), so an item click names its element.

namespace amk::prelude {

extern const char kTray[];
const char kTray[] = R"AMKJS(
  const trayItems = new Map();
  let trayClick = null;

  AMK.tray = {
    // items: [{ label, onClick, checked, enabled }, "-" for a separator]. Replaces the element's previous items.
    // Returns false when the icon is off (setting tray.enabled) or not offered by this host.
    setMenu(items) {
      if (!Array.isArray(items)) throw new TypeError("tray.setMenu attend une liste");
      const entries = [];
      const handlers = new Map();
      items.forEach((item, index) => {
        const id = index + 1;
        if (item === "-" || (item && item.separator === true)) {
          entries.push({ id, separator: true });
          return;
        }
        if (!item || typeof item.onClick !== "function") {
          throw new TypeError("tray.setMenu : chaque entrée attend label et onClick");
        }
        entries.push({
          id,
          label: item.label === undefined ? "" : text(item.label),
          checked: item.checked === true,
          enabled: item.enabled !== false,
        });
        handlers.set(id, item.onClick);
      });
      const shown = native.traySetMenu(JSON.stringify(entries));
      trayItems.clear();
      for (const [id, fn] of handlers) trayItems.set(id, fn);
      return shown;
    },
    // fn() runs on a left click on the icon.
    onClick(fn) {
      if (typeof fn !== "function") throw new TypeError("tray.onClick attend une fonction");
      const shown = native.trayListen();
      trayClick = fn;
      return shown;
    },
  };

  dispatch.tray = (payload) => {
    const event = JSON.parse(payload);
    if (event.click === true) {
      if (trayClick) runGuarded("icône de notification", trayClick, []);
      return;
    }
    if (event.to !== native.trayOwner) return;
    const fn = trayItems.get(event.item);
    if (fn) runGuarded("menu de l'icône " + event.item, fn, []);
  };
)AMKJS";

}  // namespace amk::prelude
