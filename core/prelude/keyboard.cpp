// Module keyboard: Ampify.keyboard, global shortcuts. Natives: core/natives/keyboard.cpp. The Windows host reads the
// key names that normalizeCombo writes (win/win_host.cpp).

namespace ampify::prelude {

extern const char kKeyboard[];
const char kKeyboard[] = R"AMPIFYJS(
  // Key names are written in any case ("right", "Right"): they are matched in lowercase, then written as here.
  const KEY_NAMES = [
    "Left", "Right", "Up", "Down", "Space", "Enter", "Escape", "Tab", "Backspace", "Delete", "Home", "End",
    "PageUp", "PageDown", "MediaPlayPause", "MediaNextTrack", "MediaPreviousTrack", "MediaStop",
  ];
  const KEY_CANONICAL = new Map(KEY_NAMES.map((name) => [name.toLowerCase(), name]));
  const MODIFIERS = { ctrl: "Ctrl", control: "Ctrl", alt: "Alt", shift: "Shift", win: "Win", meta: "Win" };

  // "ctrl+alt+l" becomes "Ctrl+Alt+L", and "control+right" becomes "Ctrl+Right". The order is fixed, so that the
  // same shortcut always has the same name.
  function normalizeCombo(input) {
    const parts = text(input).split("+").map((part) => part.trim()).filter((part) => part.length > 0);
    const key = parts.pop();
    if (key === undefined) throw new RangeError("empty shortcut");
    const mods = new Set();
    for (const part of parts) {
      const mod = MODIFIERS[part.toLowerCase()];
      if (!mod) throw new RangeError("unknown modifier: " + part);
      mods.add(mod);
    }
    const lower = key.toLowerCase();
    let name;
    if (key.length === 1 && /[A-Za-z0-9]/.test(key)) name = key.toUpperCase();
    else if (/^f([1-9]|1[0-9]|2[0-4])$/.test(lower)) name = key.toUpperCase();
    else if (KEY_CANONICAL.has(lower)) name = KEY_CANONICAL.get(lower);
    else throw new RangeError("unknown key: " + key);
    // Function keys and media keys may stand alone; every other key needs a modifier.
    if (mods.size === 0 && !/^F[0-9]|^Media/.test(name)) throw new RangeError("shortcut without a modifier: " + input);
    return ["Ctrl", "Alt", "Shift", "Win"].filter((mod) => mods.has(mod)).concat([name]).join("+");
  }

  const keyHandlers = new Map();

  Ampify.keyboard = {
    bind(combo, fn) {
      if (typeof fn !== "function") throw new TypeError("keyboard.bind expects a function");
      const normalized = normalizeCombo(combo);
      if (!native.bindKey(normalized)) throw new Error("shortcut not available: " + normalized);
      keyHandlers.set(normalized, fn);
      return normalized;
    },
  };

  dispatch.key = (combo) => {
    const fn = keyHandlers.get(combo);
    if (fn) runGuarded("shortcut " + combo, fn, []);
  };
)AMPIFYJS";

}  // namespace ampify::prelude
