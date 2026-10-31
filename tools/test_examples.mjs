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

// ---- Examples of the kit services: Discord, Last.fm, the output file for OBS and the listening stats ----------

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

test("now-playing-file writes the line OBS reads, only when it changes", () => {
  const { manifest, source } = load("now-playing-file");
  const fake = serviceAmpify(manifest, { config: { format: "{title} — {artist}", clearOnPause: true } });
  runIn(source, fake.context);
  assert.equal(fake.world.files.get("now-playing.txt"), "");

  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 1000));
  assert.equal(fake.world.files.get("now-playing.txt"), "Title — Artist");
  assert.deepEqual(JSON.parse(fake.world.files.get("now-playing.json")),
                   { playing: true, title: "Title", artist: "Artist", album: "Album" });
  const writes = fake.world.writes;
  fake.emit("statechange", fake.world.state);
  assert.equal(fake.world.writes, writes);  // same text: no write

  fake.emit("statechange", { ...fake.world.state, playing: false });
  assert.equal(fake.world.files.get("now-playing.txt"), "");
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

test("listen-stats counts plays from 30 seconds and shows them in a panel", () => {
  const { manifest, source } = load("listen-stats");
  const storage = new Map();
  const fake = serviceAmpify(manifest, { storage });
  runIn(source, fake.context);
  assert.match(JSON.stringify(fake.world.panels.get("stats")), /Nothing played yet/);

  fake.emit("trackchange", playing(song("1", "Title A", "Artist 1"), 200_000));
  fake.world.now += 40_000;
  fake.emit("trackchange", playing(song("2", "Title B", "Artist 2"), 200_000));
  fake.world.now += 10_000;  // skipped after 10 s: no play
  fake.emit("trackchange", playing(song("3", "Title C", "Artist 1"), 200_000));
  fake.world.now += 31_000;
  fake.emit("trackchange", { playing: false, positionMs: 0, durationMs: 0, track: null });
  fake.world.stops.forEach((fn) => fn());

  const stats = storage.get("stats");
  assert.equal(stats.plays, 2);
  assert.equal(stats.artists["Artist 1"].plays, 2);
  assert.equal(stats.artists["Artist 2"].plays, 0);
  assert.equal(stats.listenedMs, 81_000);
  const panel = fake.world.panels.get("stats");
  assert.equal(panel.type, "stack");
  assert.match(JSON.stringify(panel), /1\. Artist 1 — 2 play\(s\)/);
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
  for (const name of ["discord-presence", "now-playing-file", "scrobbler", "listen-stats"]) {
    const { manifest, source } = load(name);
    const needed = new Set([...source.matchAll(/Ampify\.(\w+)/g)].flatMap((match) => permissionsOf[match[1]] ?? []));
    assert.deepEqual([...(manifest.permissions ?? [])].sort(), [...needed].sort(), name);
  }
});

test("fullscreen-demo opens a layered view from the top bar, steps its lines and stops when closed", () => {
  const { manifest, source } = load("fullscreen-demo");
  const granted = new Set(manifest.permissions ?? []);
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const world = { buttons: [], views: [], closes: 0, closers: [], stops: [], intervals: new Map(), nextTimer: 1 };
  const ampify = {
    onStop: (fn) => world.stops.push(fn),
    ui: {
      topbar: {
        addButton: (button) => {
          need("ui.topbar");
          world.buttons.push(button);
        },
      },
      fullscreen: {
        open: (view) => {
          need("ui.fullscreen");
          world.views.push(structuredClone(view));
        },
        close: () => world.closes++,
        onClose: (fn) => {
          world.closers.push(fn);
          return () => {};
        },
      },
    },
  };
  const context = {
    Ampify: ampify,
    setInterval: (fn, ms) => {
      const id = world.nextTimer++;
      world.intervals.set(id, { fn, ms });
      return id;
    },
    clearInterval: (id) => world.intervals.delete(id),
  };
  runIn(source, context);
  assert.equal(world.buttons.length, 1);
  assert.equal(world.views.length, 0, "nothing opens before the button is clicked");

  world.buttons[0].onClick();
  const view = world.views[0];
  assert.equal(view.type, "layer");
  const [image, overlay, content] = view.children;
  assert.equal(image.type, "image");
  assert.match(image.src, /^https:\/\//);
  assert.equal(image.fit, "cover");
  assert.match(overlay.background, /^acrylic\(#[0-9A-Fa-f]{8}, 0?\.\d+\)$/);
  assert.equal(content.align, "center");
  const title = content.children[0];
  assert.ok(title.size >= 48 && title.align === "center");
  const lines = (shown) => shown.children[2].children[2].children;
  assert.deepEqual(lines(view).map((line) => line.opacity), [1, 0.35, 0.35, 0.35]);
  assert.ok(lines(view).every((line) => /opacity \d+ms/.test(line.transition)));

  assert.equal(world.intervals.size, 1);
  [...world.intervals.values()][0].fn();
  assert.deepEqual(lines(world.views[1]).map((line) => line.opacity), [0.35, 1, 0.35, 0.35]);
  assert.deepEqual(lines(world.views[1]).map((line) => line.key), lines(view).map((line) => line.key));

  // The user closes it: the timer stops, and the next click opens it again from the first line.
  world.closers.forEach((fn) => fn());
  assert.equal(world.intervals.size, 0);
  world.buttons[0].onClick();
  assert.deepEqual(lines(world.views.at(-1)).map((line) => line.opacity), [1, 0.35, 0.35, 0.35]);
  world.buttons[0].onClick();
  assert.equal(world.closes, 1);
  assert.equal(world.intervals.size, 0);
});

// Now Playing ships with Ampify, in extensions/ rather than examples/.
function fakeNowPlaying({ apple = null, lrclib = [], busyFirst = false } = {}) {
  const dir = join(examples, "..", "extensions", "now-playing");
  const manifest = JSON.parse(readFileSync(join(dir, "manifest.json"), "utf-8"));
  const source = readFileSync(join(dir, "main.js"), "utf-8");
  const granted = new Set(manifest.permissions ?? []);
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const world = { buttons: [], views: [], handlers: {}, closers: [], intervals: new Map(), nextTimer: 1, asked: [] };
  const track = { id: "t-1", title: "Song (feat. Guest)", artist: "Artist", album: "Album", artwork: "", artworkColor: "#112233" };
  let busy = busyFirst;
  const ampify = {
    onStop: () => {},
    player: {
      state: () => ({ playing: false, positionMs: 12500, track }),
      lyrics: () => apple,
      on: (name, fn) => (world.handlers[name] = fn),
    },
    http: {
      get: async (url) => {
        need("network:" + new URL(url).host);
        world.asked.push(url);
        if (url.startsWith("https://itunes.apple.com/")) {
          return { status: 200, text: JSON.stringify({ results: [{ artworkUrl100: "https://is1.example/a/100x100bb.jpg" }] }) };
        }
        if (busy) {
          busy = false;
          return { status: 200, text: JSON.stringify({ name: "ServerOverloaded", statusCode: 503 }) };
        }
        return { status: 200, text: JSON.stringify(lrclib) };
      },
    },
    ui: {
      playbar: { addButton: (button) => (need("ui.playbar"), world.buttons.push(button)) },
      fullscreen: {
        open: (view) => (need("ui.fullscreen"), world.views.push(structuredClone(view))),
        close: () => {},
        onClose: (fn) => world.closers.push(fn),
      },
    },
  };
  const context = {
    Ampify: ampify,
    setInterval: (fn, ms) => {
      const id = world.nextTimer++;
      world.intervals.set(id, { fn, ms });
      return id;
    },
    clearInterval: (id) => world.intervals.delete(id),
    setTimeout: (fn) => setImmediate(fn),  // retries without waiting
    Date,
    URL,
  };
  runIn(source, context);
  return world;
}

const settleNowPlaying = () => new Promise((resolve) => setTimeout(resolve, 20));
const lyricLines = (view) => view.children[2].children[1].children.filter((node) => node.key && node.key.startsWith("line-"));

test("now-playing shows the cover and LRCLIB's synced lyrics, retrying when the service is busy", async () => {
  const lrc = "[00:05.00]first line\n[00:10.00]second line\n[00:15.00]third line";
  const world = fakeNowPlaying({ lrclib: [{ syncedLyrics: lrc }], busyFirst: true });
  assert.equal(world.buttons.length, 1);
  assert.equal(world.views.length, 0, "nothing opens before the button is clicked");

  world.buttons[0].onClick();
  await settleNowPlaying();
  const view = world.views.at(-1);
  assert.equal(view.children[0].src, "https://is1.example/a/1200x1200bb.jpg");
  const lines = lyricLines(view);
  assert.deepEqual(lines.map((line) => line.text), ["first line", "second line", "third line"]);
  assert.deepEqual(lines.map((line) => line.opacity), [0.3, 1, 0.5], "12.5 s is in the second line");
  assert.ok(world.asked.some((url) => url.includes("track_name=Song&")), "the title is searched without its featuring");
});

test("now-playing prefers Apple's lyrics, and stops its timer when closed", async () => {
  const apple = { synced: true, lines: [{ start: 0, end: 20000, text: "apple line" }] };
  const world = fakeNowPlaying({ apple });
  world.buttons[0].onClick();
  await settleNowPlaying();
  assert.deepEqual(lyricLines(world.views.at(-1)).map((line) => line.text), ["apple line"]);
  assert.ok(!world.asked.some((url) => url.includes("lrclib")), "no LRCLIB request when Apple has the lyrics");

  assert.equal(world.intervals.size, 1);
  world.closers.forEach((fn) => fn());
  assert.equal(world.intervals.size, 0);
});
