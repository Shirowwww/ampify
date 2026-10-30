// The JavaScript side of the AMK object. It runs in every extension's runtime, before main.js, and it is part
// of the kit: its closure holds the only reference to `native`, the bridge to the C++ kernel, so an extension
// can use the API but cannot reach the bridge itself. Each permission check happens in C++ (see
// script_extension.cpp), so this file can be read without trusting it. API contract: docs/extensions/API.md.
#include "amk_prelude.hpp"

namespace amk {

const char* const kAmkPrelude = R"AMKJS((function (native) {
  "use strict";

  const text = (value) => (typeof value === "string" ? value : String(value));

  function describe(value) {
    if (value instanceof Error) return value.name + ": " + value.message;
    if (typeof value !== "object" || value === null) return String(value);
    try {
      return JSON.stringify(value);
    } catch (error) {
      return String(value);
    }
  }

  function logLine(prefix, args) {
    native.log(prefix + args.map(describe).join(" "));
  }

  // A callback that throws is logged and does not stop the others.
  function runGuarded(where, fn, args) {
    try {
      return fn(...args);
    } catch (error) {
      native.log("[erreur] " + where + " : " + describe(error));
    }
    return undefined;
  }

  // ---- Base -------------------------------------------------------------------------------------------------

  const stopHandlers = [];

  const AMK = {
    version: native.version,
    kitApi: native.kitApi,
    process: native.process,
    locale: native.locale,
    log: (message) => native.log(text(message)),
    onStop(fn) {
      if (typeof fn !== "function") throw new TypeError("onStop attend une fonction");
      stopHandlers.push(fn);
    },
  };

  // ---- Settings and storage ---------------------------------------------------------------------------------

  const TRUE_WORDS = ["true", "on", "yes", "1"];

  AMK.config = {
    get(key, fallback) {
      const raw = native.configGet(text(key));
      if (raw === undefined) return fallback;
      if (typeof fallback === "boolean") return TRUE_WORDS.includes(raw.toLowerCase());
      if (typeof fallback === "number") {
        const number = Number(raw);
        return Number.isFinite(number) ? number : fallback;
      }
      return raw;
    },
    set(key, value) {
      let raw;
      if (typeof value === "boolean") raw = value ? "true" : "false";
      else if (typeof value === "number" && Number.isFinite(value)) raw = String(value);
      else if (typeof value === "string") raw = value;
      else throw new TypeError("config.set : valeur booléenne, numérique ou texte seulement");
      native.configSet(text(key), raw);
    },
  };

  AMK.storage = {
    get(key, fallback) {
      const raw = native.storageGet(text(key));
      return raw === undefined ? fallback : JSON.parse(raw);
    },
    set(key, value) {
      const raw = JSON.stringify(value);
      if (raw === undefined) throw new TypeError("storage.set : la valeur n'est pas du JSON");
      native.storageSet(text(key), raw);
    },
    remove(key) {
      native.storageRemove(text(key));
    },
  };

  // ---- Player -----------------------------------------------------------------------------------------------

  const trackListeners = { trackchange: [], statechange: [] };

  AMK.player = {
    state() {
      return JSON.parse(native.playerState());
    },
    on(event, fn) {
      const listeners = trackListeners[event];
      if (!listeners) throw new RangeError("événement inconnu : " + event + " (trackchange ou statechange)");
      if (typeof fn !== "function") throw new TypeError("player.on attend une fonction");
      native.require("player.read");
      listeners.push(fn);
    },
    play: () => native.playerCommand("play", 0),
    pause: () => native.playerCommand("pause", 0),
    next: () => native.playerCommand("next", 0),
    previous: () => native.playerCommand("previous", 0),
    seek(ms) {
      const position = Number(ms);
      if (!Number.isFinite(position) || position < 0) throw new RangeError("seek attend un nombre de millisecondes positif");
      return native.playerCommand("seek", Math.floor(position));
    },
  };

  AMK.audio = {
    setGainDb(db) {
      const value = Number(db);
      if (!Number.isFinite(value)) throw new RangeError("setGainDb attend un nombre de décibels");
      return native.setGain(value);
    },
  };

  // ---- Shortcuts, notifications, network --------------------------------------------------------------------

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
    if (key === undefined) throw new RangeError("raccourci vide");
    const mods = new Set();
    for (const part of parts) {
      const mod = MODIFIERS[part.toLowerCase()];
      if (!mod) throw new RangeError("modificateur inconnu : " + part);
      mods.add(mod);
    }
    const lower = key.toLowerCase();
    let name;
    if (key.length === 1 && /[A-Za-z0-9]/.test(key)) name = key.toUpperCase();
    else if (/^f([1-9]|1[0-9]|2[0-4])$/.test(lower)) name = key.toUpperCase();
    else if (KEY_CANONICAL.has(lower)) name = KEY_CANONICAL.get(lower);
    else throw new RangeError("touche inconnue : " + key);
    // Function keys and media keys may stand alone; every other key needs a modifier.
    if (mods.size === 0 && !/^F[0-9]|^Media/.test(name)) throw new RangeError("raccourci sans modificateur : " + input);
    return ["Ctrl", "Alt", "Shift", "Win"].filter((mod) => mods.has(mod)).concat([name]).join("+");
  }

  const keyHandlers = new Map();

  AMK.keyboard = {
    bind(combo, fn) {
      if (typeof fn !== "function") throw new TypeError("keyboard.bind attend une fonction");
      const normalized = normalizeCombo(combo);
      if (!native.bindKey(normalized)) throw new Error("raccourci indisponible : " + normalized);
      keyHandlers.set(normalized, fn);
      return normalized;
    },
  };

  AMK.notify = (options) => {
    const note = options || {};
    native.notify(note.title === undefined ? "" : text(note.title), note.text === undefined ? "" : text(note.text));
  };

  AMK.http = {
    get(url) {
      return new Promise((resolve) => resolve(native.httpGet(text(url))));
    },
  };

  // ---- Icons and links --------------------------------------------------------------------------------------

  const ICONS = {
    play: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>',
    pause: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M6 5h4v14H6zM14 5h4v14h-4z"/></svg>',
    next: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M6 5l10 7-10 7zM16 5h2v14h-2z"/></svg>',
    previous: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M18 5L8 12l10 7zM6 5h2v14H6z"/></svg>',
    info: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M11 7h2v2h-2zm0 4h2v6h-2zM12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20z"/></svg>',
    heart: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M12 21l-8-8a5 5 0 0 1 8-6 5 5 0 0 1 8 6z"/></svg>',
    bell: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M12 22a2 2 0 0 0 2-2h-4a2 2 0 0 0 2 2zm6-6V11a6 6 0 0 0-5-5.9V4a1 1 0 0 0-2 0v1.1A6 6 0 0 0 6 11v5l-2 2v1h16v-1z"/></svg>',
    timer: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M9 2h6v2H9zm3 6a8 8 0 1 0 0 16 8 8 0 0 0 0-16zm1 8h-2V8h2z"/></svg>',
  };

  AMK.icons = {
    get(name) {
      if (!Object.prototype.hasOwnProperty.call(ICONS, name)) {
        throw new RangeError("icône inconnue : " + name + " (" + Object.keys(ICONS).join(", ") + ")");
      }
      return ICONS[name];
    },
    names: () => Object.keys(ICONS),
  };

  const SECTIONS = ["album", "playlist", "artist", "song", "station"];
  const LINK = /^(?:https:\/\/|music:\/\/)music\.apple\.com\/([^?#]*)(?:\?([^#]*))?/i;
  const ID = /^[A-Za-z0-9.-]+$/;

  AMK.uri = {
    parse(input) {
      const match = LINK.exec(text(input).trim());
      if (!match) return null;
      const segments = match[1].split("/").filter((segment) => segment.length > 0);
      const storefront = /^[a-z]{2}$/i.test(segments[0] || "") ? segments[0].toLowerCase() : null;
      const kind = segments[storefront ? 1 : 0];
      if (!SECTIONS.includes(kind)) return null;
      const id = segments[segments.length - 1];
      if (!id || id === kind || !ID.test(id)) return null;
      let trackId = null;
      for (const pair of (match[2] || "").split("&")) {
        const [name, value] = pair.split("=");
        if (name === "i" && /^[0-9]+$/.test(value || "")) trackId = value;
      }
      return { kind, storefront, id, trackId };
    },
    build(kind, id, options) {
      if (!SECTIONS.includes(kind)) throw new RangeError("type inconnu : " + kind);
      if (!ID.test(text(id))) throw new RangeError("identifiant invalide : " + id);
      const storefront = (options && options.storefront) || "us";
      if (!/^[a-z]{2}$/.test(storefront)) throw new RangeError("storefront invalide : " + storefront);
      let link = "https://music.apple.com/" + storefront + "/" + kind + "/_/" + text(id);
      if (options && options.trackId !== undefined && options.trackId !== null) {
        if (!/^[0-9]+$/.test(String(options.trackId))) throw new RangeError("trackId invalide");
        link += "?i=" + String(options.trackId);
      }
      return link;
    },
  };

  // ---- Timers -----------------------------------------------------------------------------------------------

  const timers = new Map();
  let nextTimerId = 1;

  function schedule(fn, ms, args, repeat) {
    if (typeof fn !== "function") throw new TypeError("fonction attendue");
    const delay = Math.max(0, Math.min(Number(ms) || 0, 86400000));
    const id = nextTimerId++;
    timers.set(id, { fn, args, delay, repeat });
    native.schedule(id, delay);
    return id;
  }

  function cancel(id) {
    timers.delete(id);
    native.cancel(id);
  }

  // ---- Console ----------------------------------------------------------------------------------------------

  const console = {
    log: (...args) => logLine("", args),
    info: (...args) => logLine("", args),
    warn: (...args) => logLine("[avertissement] ", args),
    error: (...args) => logLine("[erreur] ", args),
  };

  // ---- Freeze the API, then publish it as globals ------------------------------------------------------------

  for (const part of [AMK, AMK.config, AMK.storage, AMK.player, AMK.audio, AMK.keyboard, AMK.http, AMK.icons, AMK.uri]) {
    Object.freeze(part);
  }
  Object.freeze(AMK);

  globalThis.AMK = AMK;
  globalThis.console = console;
  globalThis.setTimeout = (fn, ms, ...args) => schedule(fn, ms, args, false);
  globalThis.setInterval = (fn, ms, ...args) => schedule(fn, ms, args, true);
  globalThis.clearTimeout = cancel;
  globalThis.clearInterval = cancel;
  globalThis.queueMicrotask = (fn) => {
    Promise.resolve().then(() => runGuarded("microtâche", fn, []));
  };

  // Called by the kernel. Each entry point runs under the runtime's time budget.
  return {
    stop() {
      for (let i = stopHandlers.length - 1; i >= 0; i--) runGuarded("arrêt", stopHandlers[i], []);
    },
    timer(id) {
      const entry = timers.get(id);
      if (!entry) return;
      if (entry.repeat) {
        native.schedule(id, entry.delay);  // before the call, so that clearInterval inside it cancels it
      } else {
        timers.delete(id);
      }
      runGuarded("minuterie", entry.fn, entry.args);
    },
    key(combo) {
      const fn = keyHandlers.get(combo);
      if (fn) runGuarded("raccourci " + combo, fn, []);
    },
    player(event, stateJson) {
      for (const fn of trackListeners[event]) runGuarded(event, fn, [JSON.parse(stateJson)]);
    },
  };
}))AMKJS";

}  // namespace amk
