// Runs each example extension against a fake Ampify object, to check that the script does what its
// comments say and only uses the permissions its manifest declares. Node only, no dependency:
//
//     node --test tools/test_examples.mjs

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { createHash } from "node:crypto";

const examples = join(dirname(fileURLToPath(import.meta.url)), "..", "examples");

// A stand-in for the kernel's Ampify object, following docs/extensions/api.md. Permission checks
// mirror the kernel: a function protected by a permission the manifest did not declare throws.
function fakeAmpify(manifest, storage = new Map()) {
  const granted = new Set(manifest.permissions ?? []);
  const logs = [];
  const stopHandlers = [];
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const ampify = {
    version: "0.1.0",
    kitApi: "1.0",
    log: (message) => logs.push(String(message)),
    onStop: (fn) => stopHandlers.push(fn),
    storage: {
      get: (key, fallback) => {
        need("storage");
        return storage.has(key) ? structuredClone(storage.get(key)) : fallback;
      },
      set: (key, value) => {
        need("storage");
        storage.set(key, structuredClone(value));
      },
      remove: (key) => {
        need("storage");
        storage.delete(key);
      },
    },
  };
  return { ampify, logs, stopHandlers };
}

function load(name) {
  const dir = join(examples, name);
  return {
    manifest: JSON.parse(readFileSync(join(dir, "manifest.json"), "utf-8")),
    source: readFileSync(join(dir, "main.js"), "utf-8"),
  };
}

function run(source, ampify) {
  vm.runInNewContext(source, { Ampify: ampify }, { filename: "main.js" });
}

test("hello-extension counts launches in storage and logs them", () => {
  const { manifest, source } = load("hello-extension");
  const storage = new Map();

  const first = fakeAmpify(manifest, storage);
  run(source, first.ampify);
  assert.deepEqual(first.logs, ["Hello, launch #1"]);
  assert.equal(storage.get("launches"), 1);

  const second = fakeAmpify(manifest, storage);
  run(source, second.ampify);
  assert.deepEqual(second.logs, ["Hello, launch #2"]);
  assert.equal(storage.get("launches"), 2);
});

test("hello-extension says goodbye when it stops", () => {
  const { manifest, source } = load("hello-extension");
  const fake = fakeAmpify(manifest);
  run(source, fake.ampify);

  assert.equal(fake.stopHandlers.length, 1);
  fake.stopHandlers[0]();
  assert.equal(fake.logs.at(-1), "Goodbye");
});

test("hello-extension fails without the storage permission it needs", () => {
  const { source } = load("hello-extension");
  const withoutPermissions = fakeAmpify({ permissions: [] });
  assert.throws(() => run(source, withoutPermissions.ampify), /PermissionDenied: storage/);
});

// The namespaces of Ampify that need a permission, as API.md section 4 lists them.
const permissionOf = { storage: "storage" };

test("hello-extension declares exactly the permissions its code uses", () => {
  const { manifest, source } = load("hello-extension");
  const needed = [...source.matchAll(/Ampify\.(\w+)/g)].map((match) => permissionOf[match[1]]).filter(Boolean);
  const declared = manifest.permissions ?? [];

  for (const permission of needed) assert.ok(declared.includes(permission), `permission manquante : ${permission}`);
  for (const permission of declared) assert.ok(needed.includes(permission), `permission inutile : ${permission}`);
});

// ---- Examples of the kit services: Discord and Last.fm -----------------------------------------------------------

// A fuller stand-in: player events, a clock, timers, config, POST, MD5, output files, a panel and the i18n texts.
// Every service checks the permission the kernel checks.
function serviceAmpify(manifest, { config = {}, storage = new Map(), state = null, language = "en" } = {}) {
  const granted = new Set(manifest.permissions ?? []);
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const world = {
    now: 1_700_000_000_000,
    logs: [],
    posts: [],
    postStatus: 200,
    activities: [],
    cleared: 0,
    files: new Map(),
    writes: 0,
    panels: new Map(),
    handlers: { trackchange: [], statechange: [] },
    intervals: [],
    stops: [],
    state: state ?? { playing: false, positionMs: 0, durationMs: 0, track: null },
  };
  const texts = { fr: new Map(), en: new Map() };
  class FakeDate extends Date {
    static now() {
      return world.now;
    }
  }
  const ampify = {
    version: "0.1.0",
    kitApi: "1.0",
    log: (message) => world.logs.push(String(message)),
    onStop: (fn) => world.stops.push(fn),
    config: { get: (key, fallback) => (key in config ? config[key] : fallback), set: () => {} },
    storage: {
      get: (key, fallback) => {
        need("storage");
        return storage.has(key) ? structuredClone(storage.get(key)) : fallback;
      },
      set: (key, value) => {
        need("storage");
        storage.set(key, structuredClone(value));
      },
      remove: (key) => {
        need("storage");
        storage.delete(key);
      },
    },
    player: {
      state: () => {
        need("player.read");
        return structuredClone(world.state);
      },
      on: (event, fn) => {
        need("player.read");
        world.handlers[event].push(fn);
      },
    },
    discord: {
      setActivity: (activity) => {
        need("ipc.discord");
        world.activities.push(activity);
        return true;
      },
      clear: () => {
        need("ipc.discord");
        world.cleared++;
      },
    },
    http: {
      post: (url, body) => {
        need(`network:${new URL(url).host}`);
        world.posts.push({ url, body });
        return Promise.resolve({ status: world.postStatus, text: "{}" });
      },
    },
    crypto: { md5: (value) => createHash("md5").update(String(value), "utf8").digest("hex") },
    fs: {
      writeText: (name, value) => {
        need("fs.output");
        world.files.set(name, String(value));
        world.writes++;
        return true;
      },
    },
    ui: {
      panel: {
        register: (id, view) => {
          need("ui.panel");
          world.panels.set(id, structuredClone(view));
        },
      },
    },
    i18n: {
      language,
      define: (tables) => {
        for (const lang of ["fr", "en"]) {
          for (const [key, value] of Object.entries(tables[lang] ?? {})) texts[lang].set(key, value);
        }
      },
      t: (key, vars = {}) => {
        const pattern = texts[language].get(key) ?? texts.en.get(key) ?? key;
        return pattern.replace(/\{(\w+)\}/g, (whole, name) => (name in vars ? String(vars[name]) : whole));
      },
    },
  };
  const context = {
    Ampify: ampify,
    Date: FakeDate,
    setInterval: (fn, ms) => world.intervals.push({ fn, ms }),
    setTimeout: () => {},
  };
  const emit = (event, next) => {
    world.state = next;
    for (const fn of world.handlers[event]) fn(structuredClone(next));
  };
  const tick = (ms) => {
    world.now += ms;
    for (const { fn } of world.intervals) fn();
  };
  return { world, context, emit, tick, storage };
}

function runIn(source, context) {
  vm.runInNewContext(source, context, { filename: "main.js" });
}

const settle = () => new Promise((resolve) => setImmediate(resolve));
const song = (id, title, artist, album = "Album") => ({ id, title, artist, album, variantId: "", atmos: false, lossless: false });
const playing = (track, durationMs, positionMs = 0) => ({ playing: true, positionMs, durationMs, track });

test("discord-presence shows the track with a countdown, and clears on pause", () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { clientId: "123456789012345678" } });
  runIn(source, fake.context);
  assert.equal(fake.world.cleared, 1);  // nothing playing at start

  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000, 20_000));
  const shown = fake.world.activities.at(-1);
  assert.equal(shown.clientId, "123456789012345678");
  assert.equal(shown.details, "Title");
  assert.equal(shown.state, "by Artist");
  assert.equal(shown.startMs, fake.world.now - 20_000);
  assert.equal(shown.endMs - shown.startMs, 200_000);

  fake.emit("statechange", { ...fake.world.state, playing: false });
  assert.equal(fake.world.cleared, 2);
  fake.world.stops.forEach((fn) => fn());
  assert.equal(fake.world.cleared, 3);
});

test("discord-presence without a client id only explains what to set", () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { language: "en" });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000));
  assert.equal(fake.world.activities.length, 0);
  assert.match(fake.world.logs[0], /Set the id of your Discord application/);
});

test("scrobbler signs its Last.fm calls and scrobbles after half the track", async () => {
  const { manifest, source } = load("scrobbler");
  const fake = serviceAmpify(manifest, { config: { apiKey: "cle", apiSecret: "secret", sessionKey: "session" } });
  runIn(source, fake.context);
  assert.equal(fake.world.posts.length, 0);

  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 180_000));
  assert.equal(fake.world.posts.length, 1);
  const nowPlaying = new URLSearchParams(fake.world.posts[0].body);
  assert.equal(nowPlaying.get("method"), "track.updateNowPlaying");
  // api_sig: the sorted parameters as "namevalue", then the secret, in MD5; "format" is left out.
  const names = [...nowPlaying.keys()].filter((name) => name !== "api_sig" && name !== "format").sort();
  const expected = createHash("md5").update(names.map((n) => n + nowPlaying.get(n)).join("") + "secret").digest("hex");
  assert.equal(nowPlaying.get("api_sig"), expected);
  assert.equal(nowPlaying.get("format"), "json");

  fake.tick(60_000);
  assert.equal(fake.storage.get("pending"), undefined);  // a third of the track: not yet
  fake.tick(30_000);
  await settle();
  const scrobble = new URLSearchParams(fake.world.posts.at(-1).body);
  assert.equal(scrobble.get("method"), "track.scrobble");
  assert.equal(scrobble.get("track"), "Title");
  assert.equal(scrobble.get("timestamp"), String(Math.floor(1_700_000_000_000 / 1000)));
  assert.deepEqual(fake.storage.get("pending"), []);
  assert.equal(fake.storage.get("total"), 1);

  // A track of 30 seconds or less never counts.
  fake.emit("trackchange", playing(song("2", "Jingle", "Radio"), 20_000));
  fake.tick(20_000);
  fake.emit("trackchange", playing(song("3", "Sequel", "Artist"), 300_000));
  assert.equal(fake.storage.get("total"), 1);
});

test("scrobbler keeps a refused listen for later", async () => {
  const { manifest, source } = load("scrobbler");
  const fake = serviceAmpify(manifest, { config: { apiKey: "k", apiSecret: "s", sessionKey: "x" } });
  fake.world.postStatus = 503;
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 600_000));
  fake.tick(240_000);  // four minutes are enough for a long track
  await settle();
  assert.equal(fake.storage.get("pending").length, 1);
  assert.match(fake.world.logs.at(-1), /trying again/);
});

test("the service examples declare exactly the permissions they use", () => {
  const permissionsOf = {
    storage: ["storage"],
    player: ["player.read"],
    discord: ["ipc.discord"],
    fs: ["fs.output"],
    ui: ["ui.panel"],
    http: ["network:ws.audioscrobbler.com"],
  };
  for (const name of ["discord-presence", "scrobbler"]) {
    const { manifest, source } = load(name);
    const needed = new Set([...source.matchAll(/Ampify\.(\w+)/g)].flatMap((match) => permissionsOf[match[1]] ?? []));
    assert.deepEqual([...(manifest.permissions ?? [])].sort(), [...needed].sort(), name);
  }
});

test("now-playing opens Apple's full-screen player from the playback bar", () => {
  const dir = join(examples, "..", "extensions", "now-playing");  // ships with Ampify, in extensions/
  const manifest = JSON.parse(readFileSync(join(dir, "manifest.json"), "utf-8"));
  const granted = new Set(manifest.permissions ?? []);
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const buttons = [];
  let opened = 0;
  const ui = {
    playbar: { addButton: (button) => (need("ui.playbar"), buttons.push(button)) },
    applePlayer: { open: () => (need("ui.fullscreen"), opened++) },
  };
  runIn(readFileSync(join(dir, "main.js"), "utf-8"), { Ampify: { ui } });
  assert.equal(buttons.length, 1);
  assert.equal(buttons[0].icon, "");
  assert.equal(opened, 0, "nothing opens before the button is clicked");
  buttons[0].onClick();
  assert.equal(opened, 1);
});

// ---- The Marketplace page ----------------------------------------------------------------------------------------

// A small stand-in for Ampify.React: function components render depth-first with their hooks kept by their place in
// the tree, a state change renders the whole page again, and host elements come out as plain nodes.
function fakeReact() {
  const slots = new Map();
  const mounted = new Set();
  const page = { tree: null, render: null };
  let current = null;
  const flat = (children) => children.flat(Infinity).filter((child) => child !== null && child !== undefined && child !== false);
  const h = (type, props, ...children) => ({ type, props: props || {}, children: flat(children) });
  const slot = () => {
    const name = current.place + "#" + current.index++;
    current.seen.add(name);
    return name;
  };
  const draw = () => {
    const effects = [];
    const seen = new Set();
    const expand = (node, place) => {
      if (node === null || node === undefined || node === false) return [];
      if (typeof node !== "object") return [String(node)];
      if (Array.isArray(node)) return node.flatMap((child, index) => expand(child, place + "." + index));
      const here = place + "/" + (node.props.key ?? (typeof node.type === "function" ? node.type.name : node.type));
      if (typeof node.type === "function") {
        current = { place: here, index: 0, effects, seen };
        return expand(node.type({ ...node.props, children: node.children }), here);
      }
      const children = node.children.flatMap((child, index) => expand(child, here + "." + index));
      return [{ type: node.type, ...node.props, children }];
    };
    page.tree = expand(page.render(), "")[0];
    // What did not render this time is unmounted: its state starts afresh next time.
    for (const name of [...slots.keys()]) if (!seen.has(name)) slots.delete(name);
    for (const name of [...mounted]) if (!seen.has(name)) mounted.delete(name);
    effects.forEach((effect) => effect());
  };
  const useState = (initial) => {
    const name = slot();
    if (!slots.has(name)) slots.set(name, typeof initial === "function" ? initial() : initial);
    return [slots.get(name), (value) => {
      slots.set(name, value);
      draw();
    }];
  };
  const useEffect = (effect) => {
    const name = slot();
    if (mounted.has(name)) return;
    mounted.add(name);
    current.effects.push(effect);
  };
  const mount = (render) => {
    page.render = render;
    draw();
  };
  return { React: { h, useState, useEffect }, mount, page };
}

const marketEntry = (id, kind, permissions = [], version = "1.0.0") => ({
  id, kind, version, permissions, name: id[0].toUpperCase() + id.slice(1), author: "Shirowwww",
  description: `Test ${id}.`, tags: [],
});
const marketCopy = (id, kind, permissions = [], extra = {}) =>
  ({ ...marketEntry(id, kind, permissions), valid: true, enabled: false, ...extra });

// The Marketplace against a fake ampify: each command changes the state `market --json` gives back, as the real one.
function fakeMarketplace({ catalog = [], installed = [], theme = "", failures = {} } = {}) {
  const react = fakeReact();
  const world = { state: { catalogOk: true, catalog, installed, theme, scheme: "" }, commands: [], marketError: "" };
  const copyOf = (id) => world.state.installed.find((item) => item.id === id);
  const apply = {
    install: ([id]) => world.state.installed.push(marketCopy(id, "", [], catalog.find((entry) => entry.id === id))),
    update: ([id]) => Object.assign(copyOf(id), catalog.find((entry) => entry.id === id)),
    remove: ([id]) => { world.state.installed = world.state.installed.filter((item) => item.id !== id); },
    extension: ([action, id]) => { copyOf(id).enabled = action === "enable"; },
    theme: ([action, id, , scheme]) => Object.assign(world.state, { theme: action === "set" ? id : "", scheme: scheme || "" }),
  };
  const h = react.React.h;
  const ampify = {
    React: react.React,
    components: {
      Card: (props) => h("card", props, props.children),
      Button: (props) => h("button", props),
      ButtonPrimary: (props) => h("button", { ...props, variant: "accent" }),
      ButtonTertiary: (props) => h("button", { ...props, variant: "tertiary" }),
      Dropdown: (props) => h("dropdown", props),
      Text: (props) => h("text", { ...props, text: props.children.join("") }),
    },
    kit: {
      run: (args) => {
        const command = args.join(" ");
        if (args[0] === "market") return Promise.resolve({ code: 0, out: JSON.stringify(world.state), err: world.marketError });
        world.commands.push(command);
        if (failures[command]) return Promise.resolve({ code: 1, out: "", err: `ampify: ${failures[command]}\n` });
        apply[args[0]](args.slice(1));
        return Promise.resolve({ code: 0, out: "", err: "" });
      },
    },
    ui: { title: { set: () => {} }, sidebar: { addItem: () => {} }, navigate: () => {} },
  };
  const context = { Ampify: ampify };
  runIn(readFileSync(join(examples, "..", "apps", "marketplace", "main.js"), "utf-8"), context);
  react.mount(context.render);
  return { world, page: react.page };
}

const allNodes = (node) => (typeof node === "object" ? [node, ...node.children.flatMap(allNodes)] : []);
const textsOf = (node) => allNodes(node).filter((child) => child.type === "text").map((child) => child.text);
const cardOf = (page, name) =>
  allNodes(page.tree).find((node) => node.type === "card" && textsOf(node).includes(name));
function buttonIn(node, label) {
  const found = allNodes(node).find((child) => child.type === "button" && child.label === label);
  assert.ok(found, `a button "${label}" is shown`);
  return found;
}

test("the Marketplace installs an element with permissions off, then turns it on from its detail view", async () => {
  const { world, page } = fakeMarketplace({ catalog: [marketEntry("demo", "extension", ["storage"])] });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Install").onClick();
  await settle();
  assert.deepEqual(world.commands, ["install demo --yes"], "installed, not turned on");
  assert.ok(textsOf(page.tree).includes("It can:\n• keep its settings"), "the detail view lists the permissions");
  assert.ok(textsOf(page.tree).includes("Demo installed. Read what it can do, then turn it on."));

  buttonIn(page.tree, "Turn on").onClick();
  await settle();
  assert.deepEqual(world.commands, ["install demo --yes", "extension enable demo"]);
  assert.equal(world.state.installed[0].enabled, true);
});

test("the Marketplace installs and uses a theme without permissions in one click", async () => {
  const { world, page } = fakeMarketplace({ catalog: [marketEntry("night", "theme")] });
  await settle();
  buttonIn(cardOf(page, "Night"), "Install").onClick();
  await settle();
  assert.deepEqual(world.commands, ["install night --yes", "theme set night"]);
  assert.ok(textsOf(page.tree).includes("Night installed and in use"));
  buttonIn(cardOf(page, "Night"), "Turn off");
});

test("on a card, an action that grants permissions opens the detail view instead of acting", async () => {
  const { world, page } = fakeMarketplace({
    catalog: [marketEntry("demo", "extension", ["storage"]), marketEntry("glow", "theme", ["player.read"])],
    installed: [marketCopy("demo", "extension", ["storage"]), marketCopy("glow", "theme", ["player.read"])],
  });
  await settle();
  buttonIn(cardOf(page, "Glow"), "Use…").onClick();
  assert.ok(textsOf(page.tree).includes("It can:\n• see the track that is playing"));
  buttonIn(page.tree, "‹ Themes").onClick();

  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Turn on…").onClick();
  assert.deepEqual(world.commands, [], "nothing runs before the detail view");
  buttonIn(page.tree, "Turn on").onClick();
  await settle();
  assert.deepEqual(world.commands, ["extension enable demo"]);
});

test("an update that adds a permission to an element that is on is confirmed on the detail view", async () => {
  const { world, page } = fakeMarketplace({
    catalog: [marketEntry("demo", "extension", ["storage", "network:api.example.com"], "1.1.0")],
    installed: [marketCopy("demo", "extension", ["storage"], { enabled: true })],
  });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Update…").onClick();
  assert.deepEqual(world.commands, []);
  assert.ok(textsOf(page.tree).includes(
    "It can:\n• keep its settings\n• send requests to api.example.com (new in version 1.1.0)"));
  buttonIn(page.tree, "Update").onClick();
  await settle();
  assert.deepEqual(world.commands, ["update demo --yes"]);
  assert.equal(world.state.installed[0].version, "1.1.0");
});

test("a failed uninstall keeps the detail view open with ampify's error", async () => {
  const { world, page } = fakeMarketplace({
    catalog: [marketEntry("demo", "extension")],
    installed: [marketCopy("demo", "extension")],
    failures: { "remove demo --yes": "cannot remove: a file is in use" },
  });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  buttonIn(page.tree, "Uninstall").onClick();
  buttonIn(page.tree, "Confirm uninstall").onClick();
  await settle();
  assert.deepEqual(world.commands, ["remove demo --yes"]);
  assert.ok(textsOf(page.tree).includes("ampify: cannot remove: a file is in use"));
  buttonIn(page.tree, "‹ Extensions");
});

test("Refresh shows the catalog's warning instead of clearing it", async () => {
  const { world, page } = fakeMarketplace({ catalog: [marketEntry("night", "theme")] });
  await settle();
  assert.ok(!textsOf(page.tree).includes("Reading the catalog…"), "the first reading leaves no status line behind");
  world.marketError = "ampify: catalog unreachable, cached copy used\n";
  buttonIn(page.tree, "Refresh").onClick();
  await settle();
  assert.ok(textsOf(page.tree).includes("ampify: catalog unreachable, cached copy used"));
});
