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
    answers: {},
    opened: [],
    gets: [],
    getAnswer: () => ({ status: 0, text: "" }),
    forms: [],
    formAnswer: { status: 0, text: "" },
    artwork: null,
    timeouts: [],
    notes: [],
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
      artwork: () => {
        need("player.read");
        return Promise.resolve(world.artwork && { ...world.artwork });
      },
    },
    discord: {
      setActivity: (activity) => {
        need("ipc.discord");
        world.activities.push(structuredClone(activity));
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
        // world.answers: the answers of a Last.fm method, given in turn, the last one kept.
        const queue = world.answers[new URLSearchParams(body).get("method")];
        const answer = queue ? (queue.length > 1 ? queue.shift() : queue[0]) : { status: world.postStatus, text: "{}" };
        return Promise.resolve({ ...answer });
      },
      open: (url) => {
        need(`network:${new URL(url).host}`);
        world.opened.push(url);
        return true;
      },
      get: (url) => {
        need(`network:${new URL(url).host}`);
        world.gets.push(url);
        // getAnswer may throw, as Ampify rejects a request when too many are in flight.
        return Promise.resolve().then(() => ({ ...world.getAnswer(url) }));
      },
      postForm: (url, fields) => {
        need(`network:${new URL(url).host}`);
        world.forms.push({ url, fields: structuredClone(fields) });
        const answer = typeof world.formAnswer === "function" ? world.formAnswer(url) : world.formAnswer;
        return Promise.resolve({ ...answer });
      },
    },
    notify: (note) => {
      need("notifications");
      world.notes.push({ ...note });
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
    setInterval: (fn, ms) => world.intervals.push({ fn, ms }) - 1,
    clearInterval: (id) => {
      if (world.intervals[id]) world.intervals[id].fn = () => {};
    },
    setTimeout: (fn, ms) => world.timeouts.push({ fn, ms }),
  };
  // Runs the timeouts set so far, as if their delay had passed.
  const runTimeouts = () => {
    for (const { fn } of world.timeouts.splice(0)) fn();
  };
  const emit = (event, next) => {
    world.state = next;
    for (const fn of world.handlers[event]) fn(structuredClone(next));
  };
  const tick = (ms) => {
    world.now += ms;
    for (const { fn } of world.intervals) fn();
  };
  return { world, context, emit, tick, storage, runTimeouts };
}

function runIn(source, context) {
  vm.runInNewContext(source, context, { filename: "main.js" });
}

const settle = () => new Promise((resolve) => setImmediate(resolve));
const song = (id, title, artist, album = "Album") => ({ id, title, artist, album, variantId: "", atmos: false, lossless: false });
const playing = (track, durationMs, positionMs = 0) => ({ playing: true, positionMs, durationMs, track });

// What the iTunes Search API answers for "Artist Title": a near miss first, then the track.
const itunes = (overrides = {}) => ({
  status: 200,
  text: JSON.stringify({
    results: [
      { trackName: "Title", artistName: "Someone Else", collectionName: "Album", trackId: 9 },
      {
        trackName: "Title (Remastered)", artistName: "Artist", collectionName: "Album", trackId: 42,
        artworkUrl100: "https://is1-ssl.mzstatic.com/image/thumb/a/100x100bb.jpg",
        trackViewUrl: "https://music.apple.com/us/album/album/1?i=42",
        artistViewUrl: "https://music.apple.com/us/artist/artist/7",
        collectionViewUrl: "https://music.apple.com/us/album/album/1",
        ...overrides,
      },
    ],
  }),
});

test("discord-presence prefers the exact title to a remix of the same album", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest);
  const remix = { trackName: "Flame (Club Remix)", artistName: "Artist", collectionName: "Flame - Single", trackId: 5 };
  const original = { trackName: "Flame", artistName: "Artist", collectionName: "Other", trackId: 6 };
  fake.world.getAnswer = () => ({ status: 200, text: JSON.stringify({ results: [remix, original] }) });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Flame", "Artist", "Flame - Single"), 200_000));
  await settle();
  assert.deepEqual(fake.world.activities.at(-1).buttons.at(-1), { label: "song.link", url: "https://song.link/i/6" });
});

test("discord-presence does not take a longer title for the one playing", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest);
  const other = { trackName: "Airbourne", artistName: "Artist", collectionName: "Album", trackId: 5 };
  fake.world.getAnswer = () => ({ status: 200, text: JSON.stringify({ results: [other] }) });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Air", "Artist"), 200_000));
  await settle();
  assert.ok(fake.world.activities.at(-1).buttons.every((button) => button.label !== "song.link"));
});

test("discord-presence leaves out a button whose link is too long for Discord", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { "button-1": "YouTube Music", "button-2": "Spotify" } });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "é".repeat(120), "Artist"), 200_000));
  assert.deepEqual(fake.world.activities.at(-1).buttons, []);
});

test("discord-presence asks again for a track whose request was refused", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest);
  fake.world.getAnswer = () => {
    throw new RangeError("too many requests in flight");
  };
  runIn(source, fake.context);
  const track = song("1", "Title", "Artist");
  fake.emit("trackchange", playing(track, 200_000));
  await settle();
  fake.world.getAnswer = () => itunes();
  fake.world.now += 11 * 60 * 1000;
  fake.emit("statechange", playing(track, 200_000, 1_000));
  await settle();
  assert.equal(fake.world.gets.length, 2);
  assert.equal(fake.world.activities.at(-1).largeImage, "https://is1-ssl.mzstatic.com/image/thumb/a/600x600bb.jpg");
});

test("discord-presence shows what is playing as Listening, with the catalog's cover, links and buttons", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { clientId: "123456789012345678" } });
  fake.world.getAnswer = () => itunes();
  runIn(source, fake.context);
  assert.equal(fake.world.cleared, 1);  // nothing playing at start

  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000, 20_000));
  const first = fake.world.activities.at(-1);
  assert.equal(first.type, "listening");
  assert.equal(first.statusDisplay, "state");
  assert.equal(first.details, "Title");
  assert.equal(first.state, "Artist");
  assert.equal(first.largeText, "Album");
  assert.equal(first.largeImage, "ampify");  // the cover comes once the catalog answers
  assert.equal(first.startMs, fake.world.now - 20_000);
  assert.equal(first.endMs - first.startMs, 200_000);
  assert.match(fake.world.gets[0], /^https:\/\/itunes\.apple\.com\/search\?.*term=Artist%20Title/);

  await settle();
  const shown = fake.world.activities.at(-1);
  assert.equal(shown.largeImage, "https://is1-ssl.mzstatic.com/image/thumb/a/600x600bb.jpg");
  assert.equal(shown.detailsUrl, "https://music.apple.com/us/album/album/1?i=42");
  assert.equal(shown.stateUrl, "https://music.apple.com/us/artist/artist/7");
  assert.equal(shown.largeUrl, "https://music.apple.com/us/album/album/1");
  assert.deepEqual(shown.buttons, [
    { label: "Listen on Apple Music", url: "https://music.apple.com/us/album/album/1?i=42" },
    { label: "song.link", url: "https://song.link/i/42" },
  ]);

  fake.emit("statechange", { ...fake.world.state, playing: false });
  assert.equal(fake.world.cleared, 2);
  fake.world.stops.forEach((fn) => fn());
  assert.equal(fake.world.cleared, 3);
});

test("discord-presence asks the catalog once per track, even after a restart", async () => {
  const { manifest, source } = load("discord-presence");
  const storage = new Map();
  const first = serviceAmpify(manifest, { storage });
  first.world.getAnswer = () => itunes();
  runIn(source, first.context);
  first.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000));
  first.emit("statechange", playing(song("1", "Title", "Artist"), 200_000, 5_000));
  await settle();
  assert.equal(first.world.gets.length, 1);

  const second = serviceAmpify(manifest, { storage });
  runIn(source, second.context);
  second.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000));
  assert.equal(second.world.gets.length, 0);
  assert.equal(second.world.activities.at(-1).largeImage, "https://is1-ssl.mzstatic.com/image/thumb/a/600x600bb.jpg");
});

test("discord-presence shows a file of your own with its cover, shared for three days, and search buttons", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { "button-2": "YouTube Music" } });
  fake.world.getAnswer = () => ({ status: 200, text: JSON.stringify({ results: [] }) });
  fake.world.artwork = { type: "image/jpeg", base64: "/9j/AA==" };
  fake.world.formAnswer = { status: 200, text: "https://litter.catbox.moe/abc123.jpg" };
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Demo take", "My Band"), 200_000));
  await settle();
  const before = fake.world.activities.at(-1);
  assert.equal(before.largeImage, "ampify");  // until the cover is on the web
  assert.deepEqual(before.buttons, [
    { label: "Listen on Apple Music", url: "https://music.apple.com/search?term=My%20Band%20Demo%20take" },
    { label: "Find on YouTube Music", url: "https://music.youtube.com/search?q=My%20Band%20Demo%20take" },
  ]);

  fake.runTimeouts();
  await settle();
  assert.equal(fake.world.forms.length, 1);
  assert.equal(fake.world.forms[0].url, "https://litterbox.catbox.moe/resources/internals/api.php");
  assert.deepEqual(fake.world.forms[0].fields, {
    reqtype: "fileupload", time: "72h", fileToUpload: { filename: "cover.jpg", type: "image/jpeg", base64: "/9j/AA==" },
  });
  const shown = fake.world.activities.at(-1);
  assert.equal(shown.largeImage, "https://litter.catbox.moe/abc123.jpg");
  assert.equal(shown.detailsUrl, undefined);

  // Three days later the host has deleted it: it is sent again.
  fake.world.now += 71 * 3600 * 1000;
  fake.emit("statechange", playing(song("1", "Demo take", "My Band"), 200_000, 1000));
  await settle();
  fake.runTimeouts();
  await settle();
  assert.equal(fake.world.forms.length, 2);
});

test("discord-presence tries the next host when one is down, and sends again sooner", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest);
  fake.world.getAnswer = () => ({ status: 200, text: JSON.stringify({ results: [] }) });
  fake.world.artwork = { type: "image/png", base64: "iVBORw==" };
  fake.world.formAnswer = (url) => (url.startsWith("https://uguu.se/")
    ? { status: 200, text: JSON.stringify({ success: true, files: [{ url: "https://h.uguu.se/xyz.png" }] }) }
    : { status: 500, text: "<!doctype html>" });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Demo take", "My Band"), 200_000));
  await settle();
  fake.runTimeouts();
  await settle();
  assert.deepEqual(fake.world.forms.map((form) => new URL(form.url).host), ["litterbox.catbox.moe", "uguu.se"]);
  assert.deepEqual(fake.world.forms[1].fields, {
    "files[]": { filename: "cover.png", type: "image/png", base64: "iVBORw==" },
  });
  assert.equal(fake.world.activities.at(-1).largeImage, "https://h.uguu.se/xyz.png");

  fake.world.now += 3 * 3600 * 1000;  // uguu.se keeps it 3 hours
  fake.emit("statechange", playing(song("1", "Demo take", "My Band"), 200_000, 1000));
  await settle();
  fake.runTimeouts();
  await settle();
  assert.equal(fake.world.forms.length, 4);
});

test("discord-presence keeps covers to itself when sharing is off", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { "share-covers": false } });
  fake.world.getAnswer = () => ({ status: 200, text: JSON.stringify({ results: [] }) });
  fake.world.artwork = { type: "image/jpeg", base64: "/9j/AA==" };
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Demo take", "My Band"), 200_000));
  await settle();
  fake.runTimeouts();
  await settle();
  assert.equal(fake.world.forms.length, 0);
  assert.equal(fake.world.activities.at(-1).largeImage, "ampify");
});

test("discord-presence does not share the cover of a track that already changed", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest);
  fake.world.getAnswer = () => ({ status: 200, text: JSON.stringify({ results: [] }) });
  fake.world.artwork = { type: "image/jpeg", base64: "/9j/AA==" };
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Demo take", "My Band"), 200_000));
  await settle();
  fake.world.state = playing(song("2", "Other", "My Band"), 200_000);
  fake.runTimeouts();
  await settle();
  assert.equal(fake.world.forms.length, 0);
});

test("discord-presence follows the card: status line, cover, album, progress, links, buttons, pause", async () => {
  const { manifest, source } = load("discord-presence");
  const config = {
    "status-line": "Title", "show-cover": false, "show-album": false, "show-progress": false,
    "clickable-links": false, "show-paused": true, "button-1": "None", "button-2": "song.link",
  };
  const fake = serviceAmpify(manifest, { config });
  fake.world.getAnswer = () => itunes();
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000));
  await settle();
  const shown = fake.world.activities.at(-1);
  assert.equal(shown.statusDisplay, "details");
  assert.equal(shown.largeImage, "ampify");
  assert.equal(shown.largeText, "Apple Music");
  assert.equal(shown.startMs, undefined);
  assert.equal(shown.detailsUrl, undefined);
  assert.deepEqual(shown.buttons, [{ label: "song.link", url: "https://song.link/i/42" }]);

  fake.emit("statechange", { ...fake.world.state, playing: false });
  assert.equal(fake.world.cleared, 1);  // only the one at start
  assert.equal(fake.world.activities.at(-1).smallText, "Paused");
  assert.equal(fake.world.activities.at(-1).smallImage, "paused");
});

test("discord-presence needs nothing set: it goes through Ampify's Discord application", () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { language: "en" });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000));
  assert.equal(fake.world.activities.at(-1).clientId, "1566762712413904957");
});

test("discord-presence speaks French on a French Windows", () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { language: "fr" });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "", "Artiste"), 200_000));
  assert.equal(fake.world.activities.at(-1).details, "Titre inconnu");
  assert.equal(fake.world.activities.at(-1).buttons[0].label, "Écouter sur Apple Music");
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

const lastfm = (value) => ({ status: 200, text: JSON.stringify(value) });

test("scrobbler connects a Last.fm account from the page it opens", async () => {
  const { manifest, source } = load("scrobbler");
  const fake = serviceAmpify(manifest, { config: { apiKey: "k", apiSecret: "s" } });
  fake.world.answers = {
    "auth.getToken": [lastfm({ token: "tok" })],
    "auth.getSession": [{ status: 403, text: JSON.stringify({ error: 14 }) }, lastfm({ session: { name: "lea", key: "sk1" } })],
  };
  runIn(source, fake.context);
  await settle();
  assert.deepEqual(fake.world.opened, ["https://www.last.fm/api/auth/?api_key=k&token=tok"]);
  assert.match(fake.world.notes[0].text, /Allow Ampify/);

  fake.tick(5_000);  // not allowed yet
  await settle();
  assert.equal(fake.storage.get("session"), undefined);
  fake.tick(5_000);
  await settle();
  assert.deepEqual(fake.storage.get("session"), { name: "lea", key: "sk1" });
  assert.equal(fake.world.notes.at(-1).text, "Connected to Last.fm as lea.");
  const asked = fake.world.posts.length;
  fake.tick(5_000);
  assert.equal(fake.world.posts.length, asked, "it stops asking once connected");

  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 180_000));
  assert.equal(new URLSearchParams(fake.world.posts.at(-1).body).get("sk"), "sk1");
});

test("scrobbler opens Last.fm's page once a day at most", async () => {
  const { manifest, source } = load("scrobbler");
  const storage = new Map([["askedAt", 1_700_000_000_000 - 3_600_000]]);
  const fake = serviceAmpify(manifest, { config: { apiKey: "k", apiSecret: "s" }, storage, language: "fr" });
  runIn(source, fake.context);
  await settle();
  assert.deepEqual(fake.world.opened, []);
  assert.match(fake.world.logs.at(-1), /pas encore connecté/);
});

test("scrobbler signs out when its setting is turned off", async () => {
  const { manifest, source } = load("scrobbler");
  const storage = new Map([["session", { name: "lea", key: "sk1" }], ["askedAt", 1]]);
  const fake = serviceAmpify(manifest, { config: { apiKey: "k", apiSecret: "s", connect: false }, storage });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 180_000));
  await settle();
  assert.equal(storage.has("session"), false);
  assert.equal(storage.has("askedAt"), false, "turning it on again asks at once");
  assert.deepEqual(fake.world.posts, []);
});

test("the service examples declare exactly the permissions they use", () => {
  const permissionsOf = {
    storage: ["storage"],
    player: ["player.read"],
    discord: ["ipc.discord"],
    fs: ["fs.output"],
    ui: ["ui.panel"],
    notify: ["notifications"],
  };
  // The hosts each example reaches with Ampify.http (the other addresses it holds are links shown to people).
  const hostsOf = {
    "discord-presence": ["network:itunes.apple.com", "network:litterbox.catbox.moe", "network:uguu.se"],
    scrobbler: ["network:ws.audioscrobbler.com", "network:www.last.fm"],
  };
  for (const name of ["discord-presence", "scrobbler"]) {
    const { manifest, source } = load(name);
    const needed = new Set([...source.matchAll(/Ampify\.(\w+)/g)].flatMap((match) =>
      match[1] === "http" ? hostsOf[name] : permissionsOf[match[1]] ?? []));
    assert.deepEqual([...(manifest.permissions ?? [])].sort(), [...needed].sort(), name);
  }
});

// Ampify.i18n as the kernel gives it: the texts of `language`, then English, then the key itself.
function fakeI18n(language) {
  const tables = {};
  return {
    language,
    define(messages) {
      for (const [code, table] of Object.entries(messages)) tables[code] = { ...tables[code], ...table };
    },
    t(key, values = {}) {
      const text = tables[language]?.[key] ?? tables.en?.[key] ?? key;
      return String(text).replace(/\{(\w+)\}/g, (all, name) => (name in values ? String(values[name]) : all));
    },
  };
}

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
  runIn(readFileSync(join(dir, "main.js"), "utf-8"), { Ampify: { ui, i18n: fakeI18n("en") } });
  assert.equal(buttons.length, 1);
  assert.equal(buttons[0].label, "Now Playing");
  assert.equal(buttons[0].icon, "");
  assert.equal(opened, 0, "nothing opens before the button is clicked");
  buttons[0].onClick();
  assert.equal(opened, 1);
});

test("now-playing speaks French on a French Windows", () => {
  const dir = join(examples, "..", "extensions", "now-playing");
  const buttons = [];
  const ui = { playbar: { addButton: (button) => buttons.push(button) }, applePlayer: { open: () => {} } };
  runIn(readFileSync(join(dir, "main.js"), "utf-8"), { Ampify: { ui, i18n: fakeI18n("fr") } });
  assert.equal(buttons[0].label, "À l'écoute");
});

// ---- Sleep timer and keyboard shortcuts ---------------------------------------------------------------------------

// Runs an example with a player that records its commands, the timers it sets and its notifications.
function commandAmpify(name, { config = {}, language = "en" } = {}) {
  const { manifest, source } = load(name);
  const granted = new Set(manifest.permissions ?? []);
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const world = { commands: [], timers: [], notes: [], logs: [], bindings: new Map() };
  const Ampify = {
    log: (message) => world.logs.push(String(message)),
    config: { get: (key, fallback) => (key in config ? config[key] : fallback) },
    player: Object.fromEntries(["play", "pause", "next", "previous"].map((command) => [command, () => {
      need("player.control");
      world.commands.push(command);
    }])),
    notify: (note) => (need("notifications"), world.notes.push({ ...note })),
    keyboard: { bind: (keys, fn) => (need("ui.keyboard"), world.bindings.set(keys, fn)) },
    i18n: fakeI18n(language),
  };
  runIn(source, { Ampify, setTimeout: (fn, ms) => world.timers.push({ fn, ms }) });
  return world;
}

test("sleep-timer pauses after its delay, then says so", () => {
  const world = commandAmpify("sleep-timer", { config: { minutes: 1 } });
  assert.equal(world.timers.length, 1);
  assert.equal(world.timers[0].ms, 60_000);
  assert.deepEqual(world.commands, [], "nothing pauses before the delay");
  world.timers[0].fn();
  assert.deepEqual(world.commands, ["pause"]);
  assert.deepEqual(world.notes, [{ title: "Sleep timer", text: "Playback is paused." }]);
});

test("sleep-timer speaks French on a French Windows", () => {
  const world = commandAmpify("sleep-timer", { language: "fr" });
  assert.equal(world.timers[0].ms, 30 * 60_000, "30 minutes by default");
  world.timers[0].fn();
  assert.deepEqual(world.notes, [{ title: "Minuterie de veille", text: "La lecture est en pause." }]);
});

test("keyboard-skip changes track with its two shortcuts", () => {
  const world = commandAmpify("keyboard-skip");
  assert.deepEqual([...world.bindings.keys()].sort(), ["Ctrl+Alt+Left", "Ctrl+Alt+Right"]);
  world.bindings.get("Ctrl+Alt+Right")();
  world.bindings.get("Ctrl+Alt+Left")();
  assert.deepEqual(world.commands, ["next", "previous"]);
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
      slots.set(name, typeof value === "function" ? value(slots.get(name)) : value);
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
  // Contexts keep their default value: the pages tested here give no other one.
  const createContext = (value) => ({ value, Provider: (props) => props.children });
  const useContext = (context) => context.value;
  return { React: { h, useState, useEffect, createContext, useContext }, mount, page };
}

const marketEntry = (id, kind, permissions = [], version = "1.0.0") => ({
  id, kind, version, permissions, name: id[0].toUpperCase() + id.slice(1), author: "Shirowwww",
  description: `Test ${id}.`, tags: [],
});
const marketCopy = (id, kind, permissions = [], extra = {}) =>
  ({ ...marketEntry(id, kind, permissions), valid: true, enabled: false, ...extra });

// The Marketplace against a fake ampify: each command changes the state `market --json` gives back, as the real one.
function fakeMarketplace({ catalog = [], installed = [], theme = "", failures = {}, language = "en" } = {}) {
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
    i18n: fakeI18n(language),  // English unless a test asks for French
    icons: { get: (name) => `<svg data-icon="${name}"/>` },
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

const frenchEntry = () => ({
  ...marketEntry("demo", "extension"),
  translations: { fr: { name: "Démo", description: "Un test en français." } },
});

test("the Marketplace shows the French name and description on a French Windows, card and detail view", async () => {
  const { page } = fakeMarketplace({ catalog: [frenchEntry()], language: "fr" });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  const card = cardOf(page, "Démo");
  assert.ok(card, "the card carries the French name");
  assert.ok(textsOf(card).includes("Un test en français."));
  assert.ok(!textsOf(card).includes("Demo"), "the English name is not shown");
  buttonIn(card, "Détails").onClick();
  assert.ok(textsOf(page.tree).includes("Démo"));
  assert.ok(textsOf(page.tree).includes("Un test en français."));
});

test("the Marketplace keeps the English text when the element has no translation for the language", async () => {
  const entry = { ...marketEntry("demo", "extension"), translations: { fr: { name: "Démo", description: "Un test." } } };
  const english = fakeMarketplace({ catalog: [entry] });
  await settle();
  buttonIn(english.page.tree, "Extensions").onClick();
  assert.ok(cardOf(english.page, "Demo"), "English Windows keeps the English name");

  const bare = fakeMarketplace({ catalog: [marketEntry("demo", "extension")], language: "fr" });
  await settle();
  buttonIn(bare.page.tree, "Extensions").onClick();
  assert.ok(cardOf(bare.page, "Demo"), "no translation: the English name");
  assert.ok(textsOf(cardOf(bare.page, "Demo")).includes("Test demo."));
});

test("the Marketplace takes the installed copy's translation when the catalog has none", async () => {
  const installed = [marketCopy("demo", "extension", [], { translations: frenchEntry().translations })];
  const { page } = fakeMarketplace({ catalog: [marketEntry("demo", "extension")], installed, language: "fr" });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  const card = cardOf(page, "Démo");
  assert.ok(card, "the card carries the installed copy's French name");
  assert.ok(textsOf(card).includes("Un test en français."));
});

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

// ---- The React panel example -------------------------------------------------------------------------------------

function reactPanel(language) {
  const react = fakeReact();
  const h = react.React.h;
  const host = (type) => (props) => h(type, { ...props, text: type === "text" ? props.children.join("") : undefined }, props.children);
  const components = Object.fromEntries(["Card", "Button", "Toggle", "Slider", "Dropdown", "Tooltip", "Text"]
    .map((name) => [name, host(name.toLowerCase())]));
  const Ampify = {
    React: react.React,
    components,
    ReactDOM: { render: (tree, target) => (assert.equal(target, "panel:react-panel"), react.mount(() => tree)) },
    i18n: fakeI18n(language),
    log: () => {},
    ready: Promise.resolve(),
  };
  runIn(load("react-panel").source, { Ampify, setTimeout, clearTimeout });
  return react.page;
}

test("react-panel counts clicks and opens its timer in the full view", () => {
  const page = reactPanel("en");
  buttonIn(page.tree, "Add one").onClick();
  assert.ok(textsOf(page.tree).includes("Clicks: 1"));
  assert.ok(!allNodes(page.tree).some((node) => node.type === "slider"), "the simple view has no timer");
  allNodes(page.tree).find((node) => node.type === "dropdown").onChange("full");
  const slider = allNodes(page.tree).find((node) => node.type === "slider");
  assert.equal(slider.label, "Length (min)");
  assert.equal(slider.value, 30);
});

test("react-panel speaks French on a French Windows", () => {
  const page = reactPanel("fr");
  assert.ok(textsOf(page.tree).includes("Panneau React"));
  buttonIn(page.tree, "Ajouter un").onClick();
  assert.ok(textsOf(page.tree).includes("Clics : 1"));
});

// A player whose position the test moves, with a clock the test advances: setInterval and setTimeout both run on it.
function fakeGuard() {
  const dir = join(examples, "..", "extensions", "playback-guard");  // ships with Ampify, in extensions/
  const manifest = JSON.parse(readFileSync(join(dir, "manifest.json"), "utf-8"));
  const granted = new Set(manifest.permissions ?? []);
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const world = { now: 0, timers: [], commands: [], logs: [], state: { playing: false, positionMs: 0, durationMs: 0, track: null } };
  const command = (name) => (...args) => (need("player.control"), world.commands.push([name, ...args].join(" ")), true);
  const ampify = {
    log: (message) => world.logs.push(String(message)),
    player: {
      state: () => (need("player.read"), structuredClone(world.state)),
      next: command("next"),
      previous: command("previous"),
      seek: command("seek"),
    },
  };
  const later = (repeat) => (fn, ms) => world.timers.push({ fn, ms, due: world.now + ms, repeat });
  runIn(readFileSync(join(dir, "main.js"), "utf-8"), { Ampify: ampify, setInterval: later(true), setTimeout: later(false) });
  // Moves time forward one second at a time; a playing song advances unless the test froze it.
  const run = (seconds, { frozen = false } = {}) => {
    for (let i = 0; i < seconds; i++) {
      world.now += 1000;
      if (world.state.playing && !frozen) world.state.positionMs += 1000;
      for (const timer of [...world.timers]) {
        if (timer.due > world.now) continue;
        timer.fn();
        if (timer.repeat) timer.due += timer.ms;
        else world.timers.splice(world.timers.indexOf(timer), 1);
      }
    }
  };
  return { world, run };
}

const rollout = (positionMs = 0) => playing({ ...song("t-rollout", "Rollout", "Artist"), queueItemId: "q1" }, 180_000, positionMs);
const other = () => playing({ ...song("t-other", "Other", "Artist"), queueItemId: "q2" }, 180_000, 0);

test("playback-guard reloads a song frozen while playing, then sends that song back where it froze", () => {
  const { world, run } = fakeGuard();
  world.state = rollout();
  run(14);
  assert.deepEqual(world.commands, [], "a song that plays is left alone");

  run(5, { frozen: true });
  assert.deepEqual(world.commands, ["next", "previous"]);
  assert.match(world.logs.join("\n"), /Rollout.*14 s/);

  world.state = other();  // the next song, for a moment
  run(1);
  world.state = { ...rollout(0), track: { ...rollout().track, queueItemId: "q7" } };  // back, with a new queue entry
  run(2);
  assert.deepEqual(world.commands, ["next", "previous", "seek 14000"]);
  assert.match(world.logs.join("\n"), /back to 14 s \(sent\)/);
});

test("playback-guard never sends another song to where the frozen one stopped", () => {
  const { world, run } = fakeGuard();
  world.state = rollout(14_000);
  run(5, { frozen: true });
  world.state = other();
  run(25);
  assert.deepEqual(world.commands, ["next", "previous"]);
  assert.match(world.logs.join("\n"), /did not play again/);
});

test("playback-guard leaves a paused song, a loading song and a song it already repaired twice", () => {
  const { world, run } = fakeGuard();
  world.state = { ...rollout(30_000), playing: false };
  run(20, { frozen: true });
  world.state = rollout(0);  // still loading: position 0
  run(20, { frozen: true });
  assert.deepEqual(world.commands, []);

  world.state = rollout(14_000);
  run(120, { frozen: true });
  assert.equal(world.commands.filter((name) => name === "next").length, 2);
});
