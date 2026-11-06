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
const releaseFacts = JSON.parse(readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "facts", "release.json"), "utf8"));
const languages = releaseFacts.languages.map((language) => language.code);

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

// ---- Example of the audio API: the sound is read, never changed ---------------------------------------------------

test("audio-meter logs the peak about once a second and reads loudness", () => {
  const { manifest, source } = load("audio-meter");
  const frames = [];
  const fake = fakeAmpify(manifest);
  fake.ampify.audio = {
    onFrame: (fn) => frames.push(fn),
    loudness: () => ({ momentary: -18.5, shortTerm: -17.2 }),
  };
  run(source, fake.ampify);
  assert.equal(fake.logs[0], 'loudness now: {"momentary":-18.5,"shortTerm":-17.2}');

  frames[0]({ bands: [], rms: 0.25, peak: 0.5, time: 1000 });
  frames[0]({ bands: [], rms: 0.1, peak: 0.9, time: 1400 });
  frames[0]({ bands: [], rms: 0.3, peak: 0.75, time: 2100 });
  assert.deepEqual(fake.logs.slice(1), ["peak 0.50, rms 0.25", "peak 0.75, rms 0.30"]);
});

test("audio-meter declares exactly the permission its code uses", () => {
  const { manifest, source } = load("audio-meter");
  assert.deepEqual(manifest.permissions, ["audio.analyze"]);
  assert.ok(/Ampify\.audio\./.test(source));
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
    getHeaders: [],
    getAnswer: () => ({ status: 0, text: "" }),
    forms: [],
    formAnswer: { status: 0, text: "" },
    gifs: [],  // the videos made GIFs, with their options
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
      get: (url, options = {}) => {
        need(`network:${new URL(url).host}`);
        world.gets.push(url);
        world.getHeaders.push(options.headers ?? null);
        // getAnswer may throw, as Ampify rejects a request when too many are in flight.
        return Promise.resolve().then(() => ({ ...world.getAnswer(url) }));
      },
      gifFromVideo: (url, options = {}) => {
        need(`network:${new URL(url).host}`);
        world.gifs.push({ url, options: { ...options } });
        // gifAnswer may throw, as Ampify rejects a GIF that would pass its size limit.
        return Promise.resolve().then(() => (world.gifAnswer ? world.gifAnswer(options) : { type: "image/gif", base64: "R0lGODlh" }));
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

// The searches discord-presence made, without its reads of Apple Music's web player (its page and script).
const searches = (world) => world.gets.filter((url) => !url.startsWith("https://music.apple.com/"));

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
  assert.deepEqual(fake.world.activities.at(-1).buttons.at(-1), { label: "Other platforms", url: "https://song.link/i/6" });
});

test("discord-presence does not take a longer title for the one playing", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest);
  const other = { trackName: "Airbourne", artistName: "Artist", collectionName: "Album", trackId: 5 };
  fake.world.getAnswer = () => ({ status: 200, text: JSON.stringify({ results: [other] }) });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Air", "Artist"), 200_000));
  await settle();
  assert.ok(fake.world.activities.at(-1).buttons.every((button) => button.label !== "Other platforms"));
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
  assert.equal(searches(fake.world).length, 1, "the refused request was the first one, the web player's page");
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

  await settle();
  assert.match(searches(fake.world)[0], /^https:\/\/itunes\.apple\.com\/search\?.*term=Artist%20Title/);
  const shown = fake.world.activities.at(-1);
  assert.equal(shown.largeImage, "https://is1-ssl.mzstatic.com/image/thumb/a/600x600bb.jpg");
  assert.equal(shown.detailsUrl, "https://music.apple.com/us/album/album/1?i=42");
  assert.equal(shown.stateUrl, "https://music.apple.com/us/artist/artist/7");
  assert.equal(shown.largeUrl, "https://music.apple.com/us/album/album/1");
  assert.deepEqual(shown.buttons, [
    { label: "Listen on Apple Music", url: "https://music.apple.com/us/album/album/1?i=42" },
    { label: "Other platforms", url: "https://song.link/i/42" },
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
  assert.equal(searches(first.world).length, 1);

  const second = serviceAmpify(manifest, { storage });
  runIn(source, second.context);
  second.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000));
  assert.equal(second.world.gets.length, 0);
  assert.equal(second.world.activities.at(-1).largeImage, "https://is1-ssl.mzstatic.com/image/thumb/a/600x600bb.jpg");
});

test("discord-presence shows a file of your own with its cover, shared for two months, and search buttons", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { "button-2": "YouTube Music" } });
  fake.world.getAnswer = () => ({ status: 200, text: JSON.stringify({ results: [] }) });
  fake.world.artwork = { type: "image/jpeg", base64: "/9j/AA==" };
  fake.world.formAnswer = { status: 200, text: "https://x0.at/abc123def456ghi7.jpg\n" };
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
  assert.equal(fake.world.forms[0].url, "https://x0.at/");
  assert.deepEqual(fake.world.forms[0].fields, {
    file: { filename: "cover.jpg", type: "image/jpeg", base64: "/9j/AA==" }, id_length: "16",
  });
  const shown = fake.world.activities.at(-1);
  assert.equal(shown.largeImage, "https://x0.at/abc123def456ghi7.jpg");
  assert.equal(shown.detailsUrl, undefined);

  // Two months later it is sent again, well before the host deletes it.
  fake.world.now += 61 * 24 * 3600 * 1000;
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
    ? { status: 200, text: JSON.stringify({ success: true, files: [{ url: "https://d.uguu.se/xyz.png" }] }) }
    : { status: 500, text: "<!doctype html>" });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Demo take", "My Band"), 200_000));
  await settle();
  fake.runTimeouts();
  await settle();
  assert.deepEqual(fake.world.forms.map((form) => new URL(form.url).host), ["x0.at", "uguu.se"]);
  assert.deepEqual(fake.world.forms[1].fields, {
    "files[]": { filename: "cover.png", type: "image/png", base64: "iVBORw==" },
  });
  assert.equal(fake.world.activities.at(-1).largeImage, "https://d.uguu.se/xyz.png");  // any subdomain of the host

  fake.world.now += 3 * 3600 * 1000;  // uguu.se keeps it 3 hours
  fake.emit("statechange", playing(song("1", "Demo take", "My Band"), 200_000, 1000));
  await settle();
  fake.runTimeouts();
  await settle();
  assert.equal(fake.world.forms.length, 4);
});

test("discord-presence finds on Deezer an explicit song Apple's search leaves out, with its cover and song.link", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest);
  const deezer = [
    { id: 1, title: "Mask Off (Karaoke)", artist: { name: "Karaoke Band" }, album: { title: "Hits", cover_xl: "https://cdn-images.dzcdn.net/images/cover/a/1000x1000.jpg" } },
    { id: 142337683, title: "Mask Off", artist: { name: "Future" }, album: { title: "FUTURE", cover_xl: "https://cdn-images.dzcdn.net/images/cover/b/1000x1000.jpg" } },
  ];
  fake.world.getAnswer = (url) => (url.startsWith("https://api.deezer.com/")
    ? { status: 200, text: JSON.stringify({ data: deezer }) }
    : { status: 200, text: JSON.stringify({ results: [] }) });
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Mask Off", "Future", "FUTURE"), 200_000));
  await settle();
  assert.deepEqual(searches(fake.world).map((url) => new URL(url).host), ["itunes.apple.com", "api.deezer.com"]);
  assert.match(searches(fake.world)[1], /\?limit=25&q=Future%20Mask%20Off$/);
  const shown = fake.world.activities.at(-1);
  assert.equal(shown.largeImage, "https://cdn-images.dzcdn.net/images/cover/b/1000x1000.jpg");
  assert.equal(shown.detailsUrl, "https://music.apple.com/search?term=Future%20Mask%20Off");
  assert.deepEqual(shown.buttons, [
    { label: "Listen on Apple Music", url: "https://music.apple.com/search?term=Future%20Mask%20Off" },
    { label: "Other platforms", url: "https://song.link/d/142337683" },
  ]);
  assert.equal(fake.world.forms.length, 0);  // found: nothing is uploaded
});

test("discord-presence asks Deezer again later when it refuses with its quota error", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { "share-covers": false } });
  fake.world.getAnswer = (url) => (url.startsWith("https://api.deezer.com/")
    ? { status: 200, text: JSON.stringify({ error: { type: "Exception", message: "Quota limit exceeded", code: 4 } }) }
    : { status: 200, text: JSON.stringify({ results: [] }) });
  runIn(source, fake.context);
  const track = song("1", "Mask Off", "Future");
  fake.emit("trackchange", playing(track, 200_000));
  await settle();
  fake.world.now += 11 * 60 * 1000;  // a refusal is asked again after a few minutes, not the next day
  fake.emit("statechange", playing(track, 200_000, 1_000));
  await settle();
  assert.equal(searches(fake.world).length, 4);
});

// Apple Music's web player as discord-presence reads it: a page naming its script, the script with the token, and the
// catalog's answer for "Future Mask Off" (an explicit song, which the iTunes search leaves out).
const TOKEN = "eyJhbGciOiJFUzI1NiJ9.eyJpc3MiOiJBTVBXZWJQbGF5In0.c2ln";
const webPlayer = (catalog) => (url) => {
  if (url === "https://music.apple.com/us/search") return { status: 200, text: '<script src="/assets/index~ab12.js"></script>' };
  if (url === "https://music.apple.com/assets/index~ab12.js") return { status: 200, text: `const t="${TOKEN}";` };
  if (url.startsWith("https://amp-api.music.apple.com/")) return catalog();
  return { status: 200, text: JSON.stringify({ results: [] }) };
};
const maskOff = () => ({
  status: 200,
  text: JSON.stringify({ results: { songs: { data: [
    { id: "9", attributes: { name: "Mask Off (Marshmello Remix)", artistName: "Future", albumName: "Mask Off - Single", url: "https://music.apple.com/fr/album/x/1?i=9", artwork: { url: "https://is1-ssl.mzstatic.com/r/{w}x{h}bb.jpg" } } },
    { id: "1253106000", attributes: { name: "Mask Off", artistName: "Future", albumName: "FUTURE", url: "https://music.apple.com/fr/album/mask-off/1253105919?i=1253106000", artwork: { url: "https://is1-ssl.mzstatic.com/a/{w}x{h}bb.jpg" } },
      relationships: { artists: { data: [{ attributes: { url: "https://music.apple.com/fr/artist/future/128050210" } }] } } },
  ] } } }),
});

test("discord-presence finds an explicit song in Apple Music's catalog with the web player's token", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { language: "fr" });
  fake.world.getAnswer = webPlayer(maskOff);
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Mask Off", "Future", "FUTURE"), 200_000));
  await settle();
  const asked = fake.world.gets.findIndex((url) => url.startsWith("https://amp-api.music.apple.com/"));
  assert.match(fake.world.gets[asked], /\/v1\/catalog\/fr\/search\?types=songs&.*term=Future%20Mask%20Off$/);
  assert.deepEqual({ ...fake.world.getHeaders[asked] }, { Authorization: "Bearer " + TOKEN, Origin: "https://music.apple.com" });
  assert.equal(searches(fake.world).length, 1, "neither the iTunes search nor Deezer is asked");
  const shown = fake.world.activities.at(-1);
  assert.equal(shown.largeImage, "https://is1-ssl.mzstatic.com/a/600x600bb.jpg");
  assert.equal(shown.detailsUrl, "https://music.apple.com/fr/album/mask-off/1253105919?i=1253106000");
  assert.equal(shown.stateUrl, "https://music.apple.com/fr/artist/future/128050210");
  assert.equal(shown.largeUrl, "https://music.apple.com/fr/album/mask-off/1253105919");
  assert.equal(shown.buttons.at(-1).url, "https://song.link/i/1253106000");
});

// An album with a motion artwork, as Apple's catalog and video host answer: the master playlist lists H.264 and HEVC
// variants, and each variant's playlist names one fragmented MP4 file, relative to it.
const MOTION = "https://mvod.itunes.apple.com/itunes-assets/HLSMusic116/v4/7d/P1_default.m3u8";
const VARIANT_360 = "https://mvod.itunes.apple.com/itunes-assets/HLSMusic116/v4/7e/P1_video_sdr_360x360.m3u8";
const utopia = () => ({
  status: 200,
  text: JSON.stringify({ results: { songs: { data: [
    { id: "1699712651", attributes: { name: "FE!N", artistName: "Travis Scott", albumName: "UTOPIA", url: "https://music.apple.com/us/album/fe-n/1699712635?i=1699712651", artwork: { url: "https://is1-ssl.mzstatic.com/u/{w}x{h}bb.jpg" } },
      relationships: { albums: { data: [{ id: "1699712635", attributes: { editorialVideo: { motionDetailSquare: { video: MOTION } } } }] } } },
  ] } } }),
});
const motionHost = (url) => {
  if (url === MOTION) {
    return { status: 200, text: [
      "#EXTM3U",
      '#EXT-X-STREAM-INF:BANDWIDTH=1987477,CODECS="avc1.64001f",RESOLUTION=768x768',
      "https://mvod.itunes.apple.com/itunes-assets/HLSMusic126/v4/a9/P1_video_sdr_768x768.m3u8",
      '#EXT-X-STREAM-INF:BANDWIDTH=1455343,CODECS="hvc1.2.20000000.L123.B0",RESOLUTION=180x180',
      "https://mvod.itunes.apple.com/itunes-assets/HLSMusic126/v4/be/P1_video_hevc_180x180.m3u8",
      '#EXT-X-STREAM-INF:BANDWIDTH=273236,CODECS="avc1.64001f",RESOLUTION=360x360',
      VARIANT_360,
    ].join("\n") };
  }
  if (url === VARIANT_360) {
    return { status: 200, text: '#EXTM3U\n#EXT-X-MAP:URI="P1_video_sdr_360x360-.mp4",BYTERANGE="897@0"\n#EXTINF:2.92000,\nP1_video_sdr_360x360-.mp4\n' };
  }
  return webPlayer(utopia)(url);
};

test("discord-presence shows the album's motion artwork as a GIF made on this PC, once per album", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { "animated-covers": true } });
  fake.world.getAnswer = motionHost;
  fake.world.formAnswer = { status: 200, text: "https://x0.at/gif0123456789abcd.gif\n" };
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 6; i++) await settle();
  const asked = fake.world.gets.find((url) => url.startsWith("https://amp-api.music.apple.com/"));
  assert.match(asked, /include\[songs\]=artists,albums&fields\[albums\]=editorialVideo&extend\[albums\]=editorialVideo/);
  // The smallest H.264 variant of at least 200 pixels, its file read whole.
  assert.deepEqual(fake.world.gifs, [{
    url: "https://mvod.itunes.apple.com/itunes-assets/HLSMusic116/v4/7e/P1_video_sdr_360x360-.mp4",
    options: { side: 200, fps: 10, seconds: 30 },
  }]);
  assert.deepEqual(fake.world.forms[0].fields, {
    file: { filename: "cover.gif", type: "image/gif", base64: "R0lGODlh" }, id_length: "16",
  });
  assert.equal(fake.world.activities.at(-1).largeImage, "https://x0.at/gif0123456789abcd.gif");

  // Another song of the album: the same GIF, not made again.
  fake.emit("trackchange", playing(song("2", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 6; i++) await settle();
  assert.equal(fake.world.gifs.length, 1);
  assert.equal(fake.world.activities.at(-1).largeImage, "https://x0.at/gif0123456789abcd.gif");
});

test("discord-presence makes a lighter GIF when a busy video's would be too big, and stops at another failure", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { "animated-covers": true } });
  fake.world.getAnswer = motionHost;
  fake.world.gifAnswer = (options) => {
    if (options.side > 160) throw new Error("http.gifFromVideo: the GIF would be over 3 MB: a smaller side, fewer frames a second or fewer seconds");
    return { type: "image/gif", base64: "R0lGODlh" };
  };
  fake.world.formAnswer = { status: 200, text: "https://x0.at/gif0123456789abcd.gif\n" };
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 10; i++) await settle();
  assert.deepEqual(fake.world.gifs.map((gif) => gif.options), [{ side: 200, fps: 10, seconds: 30 }, { side: 160, fps: 8, seconds: 20 }]);
  assert.equal(fake.world.activities.at(-1).largeImage, "https://x0.at/gif0123456789abcd.gif");

  const broken = serviceAmpify(manifest, { config: { "animated-covers": true } });
  broken.world.getAnswer = motionHost;
  broken.world.gifAnswer = () => { throw new Error("http.gifFromVideo: the video could not be read"); };
  runIn(source, broken.context);
  broken.emit("trackchange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 10; i++) await settle();
  assert.equal(broken.world.gifs.length, 1);
  assert.equal(broken.world.activities.at(-1).largeImage, "https://is1-ssl.mzstatic.com/u/600x600bb.jpg");
});

test("discord-presence asks again for a track found before animated covers were on, and shows its album's", async () => {
  const { manifest, source } = load("discord-presence");
  // Found by an Ampify of before (or by the iTunes search): no motion artwork field, kept with no end.
  const storage = new Map([["lookups.1", { "1": {
    cover: "https://is1-ssl.mzstatic.com/u/600x600bb.jpg", song: "https://music.apple.com/us/album/fe-n/1699712635?i=1699712651",
    artist: "", album: "https://music.apple.com/us/album/fe-n/1699712635", songId: "1699712651",
  } }]]);
  const off = serviceAmpify(manifest, { storage });
  off.world.getAnswer = motionHost;
  runIn(source, off.context);
  off.emit("trackchange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 6; i++) await settle();
  assert.equal(off.world.gets.length, 0, "animated covers off: the kept entry is enough");

  const on = serviceAmpify(manifest, { storage, config: { "animated-covers": true } });
  on.world.getAnswer = motionHost;
  on.world.formAnswer = { status: 200, text: "https://x0.at/gif0123456789abcd.gif\n" };
  runIn(source, on.context);
  on.emit("trackchange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 8; i++) await settle();
  assert.equal(on.world.gets.filter((url) => url.startsWith("https://amp-api.music.apple.com/")).length, 1);
  assert.equal(on.world.activities.at(-1).largeImage, "https://x0.at/gif0123456789abcd.gif");

  // Looked up again once only: the new entry has the field.
  on.emit("trackchange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 4; i++) await settle();
  assert.equal(on.world.gets.filter((url) => url.startsWith("https://amp-api.music.apple.com/")).length, 1);
});

test("discord-presence asks the iTunes search's finds again the next day, for the catalog's motion artwork", async () => {
  const { manifest, source } = load("discord-presence");
  const storage = new Map();
  const fake = serviceAmpify(manifest, { storage, config: { "animated-covers": true } });
  fake.world.getAnswer = () => itunes();
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000));
  for (let i = 0; i < 4; i++) await settle();
  const kept = storage.get("lookups.1")["1"];
  assert.equal(kept.songId, 42);
  assert.equal(kept.keptUntil, fake.world.now + 24 * 3600 * 1000);  // the fake clock stands still
  fake.emit("trackchange", playing(song("1", "Title", "Artist"), 200_000));
  for (let i = 0; i < 4; i++) await settle();
  assert.equal(searches(fake.world).length, 1, "not asked again within the day");
});

test("discord-presence reads a variant listed with its sound first, and files named from the host's root", async () => {
  const { manifest, source } = load("discord-presence");
  const fake = serviceAmpify(manifest, { config: { "animated-covers": true } });
  const variant = "https://mvod.itunes.apple.com/itunes-assets/HLSMusic126/v4/c3/P1_video_sdr_240x240.m3u8";
  fake.world.getAnswer = (url) => {
    if (url === MOTION) {
      return { status: 200, text: '#EXTM3U\n#EXT-X-STREAM-INF:CODECS="mp4a.40.2,avc1.64001f",RESOLUTION=240x240\n/itunes-assets/HLSMusic126/v4/c3/P1_video_sdr_240x240.m3u8\n' };
    }
    if (url === variant) return { status: 200, text: '#EXTM3U\n#EXT-X-MAP:URI="/itunes-assets/HLSMusic126/v4/c3/P1-.mp4"\n' };
    return motionHost(url);
  };
  fake.world.formAnswer = { status: 200, text: "https://x0.at/gif0123456789abcd.gif\n" };
  runIn(source, fake.context);
  fake.emit("trackchange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 6; i++) await settle();
  assert.deepEqual(fake.world.gifs.map((gif) => gif.url), ["https://mvod.itunes.apple.com/itunes-assets/HLSMusic126/v4/c3/P1-.mp4"]);
});

test("discord-presence keeps the still cover when animated covers are off, or the GIF cannot be made", async () => {
  const { manifest, source } = load("discord-presence");
  const off = serviceAmpify(manifest);
  off.world.getAnswer = motionHost;
  runIn(source, off.context);
  off.emit("trackchange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 6; i++) await settle();
  assert.equal(off.world.gets.filter((url) => url.startsWith("https://mvod.itunes.apple.com/")).length, 0);
  assert.equal(off.world.activities.at(-1).largeImage, "https://is1-ssl.mzstatic.com/u/600x600bb.jpg");

  const failing = serviceAmpify(manifest, { config: { "animated-covers": true } });
  failing.world.getAnswer = (url) => (url === VARIANT_360 ? { status: 404, text: "" } : motionHost(url));
  runIn(source, failing.context);
  failing.emit("trackchange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000));
  for (let i = 0; i < 6; i++) await settle();
  assert.equal(failing.world.gifs.length, 0);
  assert.equal(failing.world.activities.at(-1).largeImage, "https://is1-ssl.mzstatic.com/u/600x600bb.jpg");
  failing.emit("statechange", playing(song("1", "FE!N", "Travis Scott", "UTOPIA"), 200_000, 1_000));
  for (let i = 0; i < 6; i++) await settle();
  assert.equal(failing.world.gets.filter((url) => url === MOTION).length, 1, "not asked again at once");
});

test("discord-presence keeps the web player's token, and reads it again once the catalog refuses it", async () => {
  const { manifest, source } = load("discord-presence");
  const storage = new Map();
  const fake = serviceAmpify(manifest, { storage });
  let refused = false;
  fake.world.getAnswer = webPlayer(() => (refused ? { status: 401, text: "" } : maskOff()));
  runIn(source, fake.context);
  const pageReads = () => fake.world.gets.filter((url) => url === "https://music.apple.com/us/search").length;
  fake.emit("trackchange", playing(song("1", "Mask Off", "Future"), 200_000));
  await settle();
  fake.emit("trackchange", playing(song("2", "Mask Off", "Future"), 200_000));
  await settle();
  assert.equal(pageReads(), 1, "the token is kept");

  refused = true;  // a week old, or revoked: refused, dropped, and read again for the next song
  fake.emit("trackchange", playing(song("3", "Mask Off", "Future"), 200_000));
  await settle();
  fake.emit("trackchange", playing(song("4", "Mask Off", "Future"), 200_000));
  await settle();
  assert.equal(pageReads(), 2);
  // The fresh token refused too (an Ampify that cannot send headers): not read again for every song.
  fake.emit("trackchange", playing(song("5", "Mask Off", "Future"), 200_000));
  await settle();
  assert.equal(pageReads(), 2);
  assert.equal(searches(fake.world).filter((url) => url.startsWith("https://itunes.apple.com/")).length, 3);
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
  assert.deepEqual(shown.buttons, [{ label: "Other platforms", url: "https://song.link/i/42" }]);

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
    "discord-presence": ["network:music.apple.com", "network:amp-api.music.apple.com", "network:mvod.itunes.apple.com",
      "network:itunes.apple.com",
      "network:api.deezer.com", "network:x0.at", "network:uguu.se", "network:litterbox.catbox.moe"],
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
    tables,
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

test("volume-percentage shows the slider's volume in the playback bar and follows it", () => {
  const { manifest, source } = load("volume-percentage");
  const granted = new Set(manifest.permissions ?? []);
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  let volume = null;
  const buttons = [];
  const labels = [];
  const intervals = [];
  const stops = [];
  const Ampify = {
    audio: { volume: () => (need("player.read"), volume) },
    ui: {
      playbar: { addButton: (button) => (need("ui.playbar"), buttons.push(button), 7) },
      setLabel: (id, text) => labels.push([id, text]),
    },
    onStop: (fn) => stops.push(fn),
  };
  let cleared = null;
  runIn(source, { Ampify, setInterval: (fn, ms) => intervals.push({ fn, ms }) - 1, clearInterval: (id) => (cleared = id) });
  assert.equal(buttons[0].label, "– %", "unknown until Apple Music sets it");
  volume = 0.1778;  // -15 dB: Apple Music's slider is half way
  intervals[0].fn();
  assert.deepEqual(labels, [[7, "50 %"]]);
  intervals[0].fn();
  assert.equal(labels.length, 1, "the same volume is not written again");
  assert.equal(buttons[0].onClick, undefined, "a text of the bar, not a button");
  volume = 1;
  intervals[0].fn();
  assert.deepEqual(labels.at(-1), [7, "100 %"]);
  volume = 0;
  intervals[0].fn();
  assert.deepEqual(labels.at(-1), [7, "0 %"], "muted");
  stops[0]();
  assert.equal(cleared, 0);
});

test("volume-percentage still loads on an Ampify that wants an onClick", () => {
  const { source } = load("volume-percentage");
  const buttons = [];
  const Ampify = {
    audio: { volume: () => 1 },
    ui: {
      playbar: {
        addButton: (button) => {
          if (typeof button.onClick !== "function") throw new TypeError("ui: onClick expects a function");
          return buttons.push(button);
        },
      },
      setLabel: () => {},
    },
    onStop: () => {},
  };
  runIn(source, { Ampify, setInterval: () => 0, clearInterval: () => {} });
  assert.equal(buttons.length, 1);
  assert.equal(buttons[0].label, "100 %");
});

test("listening-stats counts a play after 30 seconds of listening, once, and the time listened", () => {
  const { manifest, source } = load("listening-stats");
  const granted = new Set(manifest.permissions ?? []);
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const stored = new Map();
  const intervals = [];
  const listeners = {};
  const entries = [];
  let state = { playing: true, track: { id: "t-1", title: "Lucid Dreams", artist: "Juice WRLD" } };
  const Ampify = {
    // The page opens on Apple Music's own counts, read once it shows: these counts are checked through "all".
    React: { h: (type, props, ...children) => ({ type, props: props ?? {}, children }), useState: (v) => [v === "library" ? "all" : v, () => {}], useEffect: () => {} },
    ReactDOM: { render: () => {} },
    components: { Button: "Button", Card: "Card", Dropdown: "Dropdown", Text: "Text" },
    i18n: fakeI18n("en"),
    storage: {
      get: (key, fallback) => (need("storage"), stored.has(key) ? structuredClone(stored.get(key)) : fallback),
      set: (key, value) => (need("storage"), stored.set(key, structuredClone(value))),
    },
    player: { state: () => (need("player.read"), state), on: (name, fn) => (listeners[name] = fn) },
    ui: { sidebar: { addItem: (item) => (need("ui.sidebar"), entries.push(item)) }, navigate: () => {} },
    onStop: () => {},
  };
  const context = { Ampify, setInterval: (fn, ms) => intervals.push({ fn, ms }) - 1, clearInterval: () => {} };
  runIn(source, context);
  assert.equal(entries[0].label, "Listening stats");
  assert.equal(typeof context.render, "function", "the page renders through render()");
  const tick = intervals.find((interval) => interval.ms === 5000).fn;
  for (let i = 0; i < 5; i++) tick();
  assert.equal(stored.get("all").songs["t-1"].plays, 0, "25 seconds are not a play yet");
  for (let i = 0; i < 5; i++) tick();
  const song = stored.get("all").songs["t-1"];
  assert.equal(song.plays, 1);
  assert.equal(song.ms, 50000);
  state = { playing: false, track: state.track };
  tick();
  assert.equal(stored.get("all").songs["t-1"].ms, 50000, "a pause adds nothing");
  listeners.trackchange();
  state = { playing: true, track: state.track };
  for (let i = 0; i < 6; i++) tick();
  assert.equal(stored.get("all").songs["t-1"].plays, 2, "played again, counted again");
  // Repeat one: Apple starts the song again without a track change; its position falls back to the start.
  state = { playing: true, positionMs: 150000, track: state.track };
  tick();
  state = { playing: true, positionMs: 2000, track: state.track };
  for (let i = 0; i < 6; i++) tick();
  assert.equal(stored.get("all").songs["t-1"].plays, 3, "repeated from its start, counted again");

  // The page with its counts: function components expanded, and the interface's boolean props given booleans.
  const expand = (node) => {
    if (!node || typeof node !== "object") return node;
    if (typeof node.type === "function") return expand(node.type({ ...node.props, children: node.children }));
    return { ...node, children: node.children.flat().map(expand) };
  };
  const page = expand(context.render());
  const texts = [];
  const walk = (node) => {
    if (!node || typeof node !== "object") return void (typeof node === "string" && texts.push(node));
    if ("grow" in node.props) assert.equal(typeof node.props.grow, "boolean", "grow takes true or false");
    node.children.forEach(walk);
  };
  walk(page);
  assert.ok(texts.includes("Lucid Dreams") && texts.includes("Juice WRLD"), "the top songs list the song");
  const hours = stored.get("all").hours;
  assert.equal(hours.length, 24);
  assert.equal(hours.reduce((sum, ms) => sum + ms, 0), stored.get("all").songs["t-1"].ms, "each 5 s goes to its hour");
  const count = (node) => (!node || typeof node !== "object" ? 0 : 1 + node.children.reduce((sum, child) => sum + count(child), 0));
  assert.ok(count(page) < 300, "the interface takes 300 nodes at most");
});

test("listening-stats keeps the album, and stays under 300 nodes with a full month", () => {
  const { source } = load("listening-stats");
  const stored = new Map();
  const day = (offset) => {
    const date = new Date(Date.now() - offset * 86400000);
    const two = (n) => String(n).padStart(2, "0");
    return `${date.getFullYear()}-${two(date.getMonth() + 1)}-${two(date.getDate())}`;
  };
  const songs = {};
  for (let i = 0; i < 40; i++) songs["t-" + i] = { title: "Song " + i, artist: "Artist " + (i % 12), album: "Album " + (i % 9), plays: i + 1, ms: 60000 * (i + 1) };
  const days = {};
  for (let offset = 0; offset < 28; offset++) days[day(offset)] = { songs, hours: Array.from({ length: 24 }, () => 60000) };
  stored.set("days", days);
  stored.set("all", { songs, hours: Array.from({ length: 24 }, () => 60000) });
  let state = { playing: true, track: { id: "t-x", title: "New", artist: "Someone", album: "Debut" } };
  const intervals = [];
  const Ampify = {
    React: { h: (type, props, ...children) => ({ type, props: props ?? {}, children }), useState: (v) => [v, () => {}], useEffect: () => {} },
    ReactDOM: { render: () => {} },
    components: { Button: "Button", Card: "Card", Dropdown: "Dropdown", Text: "Text" },
    i18n: fakeI18n("en"),
    storage: { get: (key, fallback) => (stored.has(key) ? structuredClone(stored.get(key)) : fallback), set: (key, value) => stored.set(key, structuredClone(value)) },
    player: { state: () => state, on: () => {} },
    ui: { sidebar: { addItem: () => {} }, navigate: () => {} },
    onStop: () => {},
  };
  const context = { Ampify, setInterval: (fn, ms) => intervals.push({ fn, ms }) - 1, clearInterval: () => {} };
  runIn(source, context);
  intervals.find((interval) => interval.ms === 5000).fn();
  assert.equal(stored.get("all").songs["t-x"].album, "Debut");
  const expand = (node) => {
    if (!node || typeof node !== "object") return node;
    if (typeof node.type === "function") return expand(node.type({ ...node.props, children: node.children }));
    return { ...node, children: node.children.flat().map(expand) };
  };
  const count = (node) => (!node || typeof node !== "object" ? 0 : 1 + node.children.reduce((sum, child) => sum + count(child), 0));
  // useState gives "week": the month and all-time views are counted through a page whose state starts there.
  for (const period of ["week", "month", "all"]) {
    Ampify.React.useState = (v) => [typeof v === "string" ? period : v, () => {}];
    assert.ok(count(expand(context.render())) < 300, period + ": the interface takes 300 nodes at most");
  }
  state = null;
});

test("listening-stats says when Apple Music's library cannot be read, and reads it once while pages open", async () => {
  const { source } = load("listening-stats");
  let reads = 0;
  const Ampify = {
    React: { h: (type, props, ...children) => ({ type, props: props ?? {}, children }), useState: (v) => [v, () => {}], useEffect: (fn) => void fn() },
    ReactDOM: { render: () => {} },
    components: { Button: "Button", Card: "Card", Dropdown: "Dropdown", Text: "Text" },
    i18n: fakeI18n("en"),
    storage: { get: (key, fallback) => fallback, set: () => {} },
    library: { tracks: () => (reads++, Promise.reject(new Error("no library"))) },
    log: () => {},
    player: { state: () => null, on: () => {} },
    ui: { sidebar: { addItem: () => {} }, navigate: () => {} },
    onStop: () => {},
  };
  const context = { Ampify, setInterval: () => 0, clearInterval: () => {} };
  runIn(source, context);
  const expand = (node) => {
    if (!node || typeof node !== "object") return node;
    if (typeof node.type === "function") return expand(node.type({ ...node.props, children: node.children }));
    return { ...node, children: node.children.flat().map(expand) };
  };
  const texts = (node, out = []) => {
    if (!node || typeof node !== "object") return (typeof node === "string" && out.push(node), out);
    node.children.forEach((child) => texts(child, out));
    return out;
  };
  expand(context.render());
  expand(context.render());
  assert.equal(reads, 1, "a page opened again while the library is read does not read it again");
  await new Promise((resolve) => setImmediate(resolve));
  const shown = texts(expand(context.render()));
  assert.ok(shown.includes("Apple Music's library could not be read. The other periods show what plays on this device."));
  assert.ok(!shown.includes("Nothing yet: songs count after 30 seconds of listening."), "not the empty page of a new install");
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

test("sleep-timer says when it will pause, pauses after its delay, then says so", () => {
  const world = commandAmpify("sleep-timer", { config: { minutes: 1 } });
  assert.equal(world.timers.length, 1);
  assert.equal(world.timers[0].ms, 60_000);
  assert.deepEqual(world.commands, [], "nothing pauses before the delay");
  assert.deepEqual(world.notes, [
    { title: "Sleep timer", text: "The music pauses in 1 min. Change the delay in Settings, under Ampify." },
  ]);
  world.timers[0].fn();
  assert.deepEqual(world.commands, ["pause"]);
  assert.deepEqual(world.notes.at(-1), { title: "Sleep timer", text: "Playback is paused." });
});

test("sleep-timer speaks French on a French Windows", () => {
  const world = commandAmpify("sleep-timer", { language: "fr" });
  assert.equal(world.timers[0].ms, 30 * 60_000, "30 minutes by default");
  world.timers[0].fn();
  assert.deepEqual(world.notes, [
    { title: "Minuterie de veille",
      text: "La musique se met en pause dans 30 min. Le délai se règle dans les Réglages, sous Ampify." },
    { title: "Minuterie de veille", text: "La lecture est en pause." },
  ]);
});

test("keyboard-skip changes track with its two shortcuts", () => {
  const world = commandAmpify("keyboard-skip");
  assert.deepEqual([...world.bindings.keys()].sort(), ["Ctrl+Alt+Left", "Ctrl+Alt+Right"]);
  world.bindings.get("Ctrl+Alt+Right")();
  world.bindings.get("Ctrl+Alt+Left")();
  assert.deepEqual(world.commands, ["next", "previous"]);
});

// copy-link: a context entry kept to songs, albums and playlists, and a shortcut, both copying to the clipboard.
function copyLinkAmpify({ state = { track: null, positionMs: 0 }, language = "en" } = {}) {
  const { manifest, source } = load("copy-link");
  const granted = new Set(manifest.permissions ?? []);
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const world = { copied: [], entries: [], bindings: new Map() };
  const Ampify = {
    i18n: fakeI18n(language),
    ui: { menu: { addItem: (where, entry) => (need("ui.menu"), world.entries.push({ where, ...entry })) } },
    keyboard: { bind: (keys, fn) => (need("ui.keyboard"), world.bindings.set(keys, fn)) },
    clipboard: { copy: (text) => (need("clipboard.write"), world.copied.push(text), true) },
    uri: { build: (kind, id) => `https://music.apple.com/us/${kind}/_/${id}` },
    player: {
      state: () => (need("player.read"), state),
      formatTime: (ms) => `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, "0")}`,
    },
  };
  runIn(source, { Ampify });
  return world;
}

test("copy-link copies an item's link from its menu, or its name without a catalog id", () => {
  const world = copyLinkAmpify({ language: "fr" });
  assert.equal(world.entries.length, 1);
  const [entry] = world.entries;
  assert.equal(entry.where, "context");
  assert.equal(entry.label, "Copier le lien");
  assert.deepEqual([...entry.kinds], ["song", "album", "playlist"]);
  entry.onClick({ title: "Album", item: { kind: "album", id: "123" } });
  entry.onClick({ title: "My song", item: { kind: "song" } });
  entry.onClick({ title: "Untold" });
  assert.deepEqual(world.copied, ["https://music.apple.com/us/album/_/123", "My song", "Untold"]);
});

test("copy-link copies what plays now with its time, and nothing without a track", () => {
  const playing = copyLinkAmpify({ state: { track: { title: "Song", artist: "Artist" }, positionMs: 83_000 } });
  playing.bindings.get("Ctrl+Alt+C")();
  assert.deepEqual(playing.copied, ["Song - Artist (1:23)"]);
  const idle = copyLinkAmpify();
  idle.bindings.get("Ctrl+Alt+C")();
  assert.deepEqual(idle.copied, []);
});

// ---- The Marketplace page ----------------------------------------------------------------------------------------

// A small stand-in for Ampify.React: function components render depth-first with their hooks kept by their place in
// the tree, a state change renders the whole page again, and host elements come out as plain nodes. An effect's
// cleanup runs when its component is no longer drawn.
function fakeReact() {
  const slots = new Map();
  const mounted = new Set();
  const cleanups = new Map();
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
    for (const name of Array.from(slots.keys())) if (!seen.has(name)) slots.delete(name);
    for (const name of Array.from(mounted)) {
      if (seen.has(name)) continue;
      mounted.delete(name);
      const cleanup = cleanups.get(name);
      cleanups.delete(name);
      if (typeof cleanup === "function") cleanup();
    }
    effects.forEach(({ name, effect }) => cleanups.set(name, effect()));
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
    current.effects.push({ name, effect });
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
// Its pages open as the kernel opens them: navigating mounts the page anew, and back() is the arrow of the page header.
function fakeMarketplace({ catalog = [], installed = [], theme = "", failures = {}, language = "en", readmes = {} } = {}) {
  const react = fakeReact();
  const world = { state: { catalogOk: true, catalog, installed, theme, scheme: "" }, commands: [], reads: [],
                  marketError: "", route: "/marketplace", routes: [] };
  const pages = {};
  const history = [];
  const show = (path) => {
    world.route = path;
    react.mount(pages[path]);
  };
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
      Slider: (props) => h("slider", props),
      Text: (props) => h("text", { ...props, text: props.children.join("") }),
    },
    kit: {
      run: (args) => {
        const command = args.join(" ");
        if (args[0] === "market" && args[1] === "readme") {
          world.reads.push(command);
          if (world.offline) return Promise.resolve({ code: 1, out: "", err: "cannot reach the catalog" });
          return Promise.resolve({ code: 0, out: readmes[args[2]] || "", err: "" });
        }
        if (args[0] === "market") return Promise.resolve({ code: 0, out: JSON.stringify(world.state), err: world.marketError });
        world.commands.push(command);
        if (failures[command]) return Promise.resolve({ code: 1, out: "", err: `ampify: ${failures[command]}\n` });
        apply[args[0]](args.slice(1));
        return Promise.resolve({ code: 0, out: "", err: "" });
      },
    },
    ReactDOM: { render: (tree, target) => { pages[target.page] = () => tree; } },
    ui: {
      title: { set: () => {} },
      sidebar: { addItem: () => {} },
      // Ampify's settings page is the kernel's: the route is kept, nothing of the app is drawn.
      navigate: (path) => {
        world.routes.push(path);
        if (!pages[path]) return;
        history.push(world.route);
        show(path);
      },
    },
    i18n: fakeI18n(language),  // English unless a test asks for French
    icons: { get: (name) => `<svg data-icon="${name}"/>` },
  };
  const context = { Ampify: ampify };
  runIn(readFileSync(join(examples, "..", "apps", "marketplace", "main.js"), "utf-8"), context);
  pages["/marketplace"] = context.render;
  show("/marketplace");
  return { world, page: react.page, back: () => show(history.pop()) };
}

const allNodes = (node) => (typeof node === "object" ? [node, ...node.children.flatMap(allNodes)] : []);
const textsOf = (node) => allNodes(node).filter((child) => child.type === "text").map((child) => child.text);
const cardOf = (page, name) =>
  allNodes(page.tree).find((node) => node.type === "card" && textsOf(node).includes(name));
const cardTitled = (page, title) => allNodes(page.tree).find((node) => node.type === "card" && node.title === title);
const abilitiesOf = (page) => textsOf(cardTitled(page, "What it can do"));
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

test("the Marketplace labels a number setting on its slider, like the menu of a choice setting", async () => {
  const settings = [
    { key: "side", label: "Sidebar side", type: "choice", options: ["Right", "Left"], value: "Right" },
    { key: "radius", label: "Panel corners", type: "number", min: 0, max: 24, value: "12" },
  ];
  const installed = [marketCopy("demo", "theme", [], { settings })];
  const { page } = fakeMarketplace({ catalog: [marketEntry("demo", "theme")], installed });
  await settle();
  buttonIn(page.tree, "Themes").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  const panel = cardTitled(page, "Settings");
  const slider = allNodes(panel).find((node) => node.type === "slider");
  assert.equal(slider.label, "Panel corners", "the slider's own header carries the label");
  assert.equal(allNodes(panel).find((node) => node.type === "dropdown").label, "Sidebar side");
  assert.ok(!textsOf(panel).includes("Panel corners"), "no separate caption in the dim secondary text");
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
  assert.deepEqual(abilitiesOf(page), ["• keep its settings"], "the detail view lists the permissions");
  assert.ok(textsOf(page.tree).includes("Demo installed. Read what it can do, then turn it on."));

  buttonIn(page.tree, "Turn on").onClick();
  await settle();
  assert.deepEqual(world.commands, ["install demo --yes", "extension enable demo"]);
  assert.equal(world.state.installed[0].enabled, true);
});

test("installing from a card opens the detail view even after that element's page was left", async () => {
  const { world, page, back } = fakeMarketplace({ catalog: [marketEntry("demo", "extension", ["storage"])] });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  back();
  await settle();
  buttonIn(cardOf(page, "Demo"), "Install").onClick();
  await settle();
  assert.deepEqual(world.commands, ["install demo --yes"]);
  assert.deepEqual(abilitiesOf(page), ["• keep its settings"], "the detail view lists the permissions");
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
  const { world, page, back } = fakeMarketplace({
    catalog: [marketEntry("demo", "extension", ["storage"]), marketEntry("glow", "theme", ["player.read"])],
    installed: [marketCopy("demo", "extension", ["storage"]), marketCopy("glow", "theme", ["player.read"])],
  });
  await settle();
  buttonIn(cardOf(page, "Glow"), "Use…").onClick();
  assert.deepEqual(abilitiesOf(page), ["• see the track that is playing"]);
  back();
  await settle();

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
  assert.deepEqual(abilitiesOf(page),
                   ["• keep its settings", "• send requests to api.example.com (new in version 1.1.0)"]);
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
  assert.equal(world.route, "/marketplace/item", "the detail page stays open");
});

test("each catalog tab says in one line what its kind is, and Installed does not", async () => {
  const { page } = fakeMarketplace({ catalog: [marketEntry("demo", "extension")] });
  await settle();
  assert.ok(textsOf(page.tree).some((text) => text.startsWith("A theme changes how Apple Music looks")));
  buttonIn(page.tree, "Snippets").onClick();
  assert.ok(textsOf(page.tree).some((text) => text.startsWith("A snippet is a small tweak")));
  assert.ok(!textsOf(page.tree).some((text) => text.includes("ampify new")), "no command for someone without a terminal");
  buttonIn(page.tree, "Installed").onClick();
  assert.ok(!textsOf(page.tree).some((text) => text.startsWith("A snippet is")));
});

test("an element that cannot load says why on its detail page, not to run ampify doctor", async () => {
  const broken = marketCopy("demo", "extension", [], { valid: false, errors: ["main.js is missing"] });
  const { page } = fakeMarketplace({ installed: [broken] });
  await settle();
  buttonIn(page.tree, "Installed").onClick();
  assert.ok(textsOf(page.tree).includes("Cannot load: open Details to see why"));
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  assert.ok(textsOf(page.tree).includes("• main.js is missing"));
  assert.ok(!textsOf(page.tree).some((text) => text.includes("doctor")));
});

test("an uninstall goes back to the list, which says the element is gone", async () => {
  const { world, page } = fakeMarketplace({ installed: [marketCopy("demo", "extension")] });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  buttonIn(page.tree, "Uninstall").onClick();
  buttonIn(page.tree, "Confirm uninstall").onClick();
  await settle();
  await settle();
  assert.equal(world.route, "/marketplace");
  assert.ok(textsOf(page.tree).includes("Demo uninstalled"), "the list keeps what the action said");
  assert.ok(!cardOf(page, "Demo"), "and no longer shows the element");
});

test("the arrow of a detail page goes back to the list as it was left: tab and search", async () => {
  const catalog = ["alpha", "beta", "gamma"].map((id) => marketEntry(id, "extension"));
  const { world, page, back } = fakeMarketplace({ catalog });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  allNodes(page.tree).find((node) => node.type === "textfield").onChange({ value: "beta" });
  buttonIn(cardOf(page, "Beta"), "Details").onClick();
  assert.equal(world.route, "/marketplace/item");
  assert.ok(textsOf(page.tree).includes("Test beta."));

  back();
  await settle();
  assert.equal(world.route, "/marketplace");
  assert.equal(allNodes(page.tree).find((node) => node.type === "textfield").value, "beta", "the search is kept");
  assert.ok(cardOf(page, "Beta"), "the Extensions tab is still open");
  assert.ok(!cardOf(page, "Alpha"), "and still filtered");
});

test("a detail page without picture nor description still shows what the element is and does", async () => {
  const bare = { ...marketEntry("plain", "extension"), description: "" };
  const { world, page } = fakeMarketplace({ catalog: [bare], installed: [marketCopy("plain", "extension", [], bare)] });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Plain"), "Details").onClick();
  assert.ok(allNodes(page.tree).some((node) => node.type === "icon"), "a tile with the icon of its kind stands in for the picture");
  assert.ok(textsOf(page.tree).includes("No description yet."));
  assert.deepEqual(abilitiesOf(page), ["Asks for no permission."]);
  const about = textsOf(cardTitled(page, "About"));
  assert.ok(about.includes("Made by") && about.includes("Shirowwww"), "the About card says who made it");
  buttonIn(cardTitled(page, "About"), "Open Ampify's settings").onClick();
  assert.deepEqual(world.routes.slice(-1), ["/ampify/settings"]);
});

test("the detail page shows the element's README below the facts, read once", async () => {
  const readme = [
    "# Demo", "", "Shows **what is playing** on the [lock screen](https://example.com).", "",
    "![shot](shot.png)", "", "## How to use it", "", "1. Turn it on.", "2. Play a song,", "   then look.", "",
    "- `Ctrl+L` locks", "", "| Key | What |", "|---|---|", "| a | b |", "", "```", "const x = 1;", "```", "",
  ].join("\n");
  const { world, page, back } = fakeMarketplace({ catalog: [marketEntry("demo", "extension")], readmes: { demo: readme } });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  assert.ok(textsOf(page.tree).includes("Reading its description…"), "a line says it is being read");
  await settle();
  const shown = textsOf(cardOf(page, "How to use it"));
  assert.deepEqual(shown, ["Demo", "Shows what is playing on the lock screen.", "How to use it", "1. Turn it on.",
                           "2. Play a song, then look.", "• Ctrl+L locks", "Key  ·  What", "a  ·  b", "const x = 1;"]);
  assert.ok(!textsOf(page.tree).includes("Reading its description…"));

  back();
  await settle();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  assert.ok(cardOf(page, "How to use it"), "shown at once the second time");
  assert.deepEqual(world.reads, ["market readme demo"]);
});

test("a README that could not be read is read again the next time its page opens", async () => {
  const { world, page, back } = fakeMarketplace({ catalog: [marketEntry("demo", "extension")], readmes: { demo: "# Usage" } });
  world.offline = true;
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  await settle();
  assert.ok(!textsOf(page.tree).includes("Usage"));

  world.offline = false;
  back();
  await settle();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  await settle();
  assert.ok(textsOf(page.tree).includes("Usage"), "shown once the catalog can be reached");
});

test("a long README stays within the kernel's 300 elements, so the detail page and its buttons still show", async () => {
  const readme = Array.from({ length: 400 }, (_, index) => "- line " + index).join("\n");
  const { page } = fakeMarketplace({ catalog: [marketEntry("demo", "extension")], readmes: { demo: readme } });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  await settle();
  assert.ok(textsOf(page.tree).includes("• line 0"));
  assert.ok(allNodes(page.tree).length <= 300, `${allNodes(page.tree).length} elements`);
  buttonIn(page.tree, "Install");
});

test("no text of a detail page goes over the kernel's 500 bytes, and none of its words is lost", async () => {
  const latin = Array.from({ length: 200 }, (_, index) => "word" + index).join(" ");
  const cyrillic = "Длинное описание ".repeat(40).trim();
  const entry = { ...marketEntry("demo", "extension"), description: cyrillic };
  const { page } = fakeMarketplace({ catalog: [entry], readmes: { demo: latin + "\n\n```\n" + "x".repeat(700) + "\n```\n" } });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  assert.ok(textsOf(page.tree).every((text) => Buffer.byteLength(text) <= 500), "the cards fit");
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  await settle();
  const texts = textsOf(page.tree);
  const long = texts.filter((text) => Buffer.byteLength(text) > 500);
  assert.deepEqual(long, [], "every text fits");
  assert.ok(texts.join(" ").includes("word0") && texts.join(" ").includes("word199"));
  assert.equal(texts.filter((text) => text.startsWith("x")).join("").length, 700, "the code is all there");
  assert.equal(texts.filter((text) => text.includes("Длинное")).join(" "), cyrillic);
});

test("a README of long paragraphs also stays within the kernel's 300 elements", async () => {
  const paragraph = Array.from({ length: 250 }, (_, index) => "w" + index).join(" ");
  const readme = Array.from({ length: 90 }, () => paragraph).join("\n\n");
  const { page } = fakeMarketplace({ catalog: [marketEntry("demo", "extension")], readmes: { demo: readme } });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  await settle();
  assert.ok(textsOf(page.tree).some((text) => text.startsWith("w0 w1")));
  assert.ok(allNodes(page.tree).length <= 300, `${allNodes(page.tree).length} elements`);
});

test("a word cut at its only space still leaves every piece within 500 bytes", async () => {
  const readme = "a " + "b".repeat(498) + "中" + "c".repeat(10);
  const { page } = fakeMarketplace({ catalog: [marketEntry("demo", "extension")], readmes: { demo: readme } });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  await settle();
  const pieces = textsOf(page.tree).filter((text) => text.startsWith("b") || text.endsWith("c"));
  assert.ok(pieces.length > 0);
  assert.deepEqual(pieces.filter((text) => Buffer.byteLength(text) > 500), []);
});

test("a detail page without README shows no README card", async () => {
  const { page } = fakeMarketplace({ catalog: [marketEntry("demo", "extension")] });
  await settle();
  buttonIn(page.tree, "Extensions").onClick();
  buttonIn(cardOf(page, "Demo"), "Details").onClick();
  await settle();
  const cards = allNodes(page.tree).filter((node) => node.type === "card");
  assert.deepEqual(cards.map((card) => card.title), [undefined, "What it can do", "About"]);
});

test("a card shows the whole preview, never cut", async () => {
  const { page } = fakeMarketplace({ catalog: [{ ...marketEntry("night", "theme"), preview: "https://example.com/n.jpg" }] });
  await settle();
  const image = allNodes(cardOf(page, "Night")).find((node) => node.type === "image");
  assert.equal(image.fit, "contain");
});

test("the Marketplace's Settings button opens Ampify's settings page", async () => {
  const { world, page } = fakeMarketplace({ catalog: [marketEntry("night", "theme")] });
  await settle();
  buttonIn(page.tree, "Settings").onClick();
  assert.deepEqual(world.routes, ["/ampify/settings"]);
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
  assert.ok(textsOf(page.tree).includes("Clics : 0"));
  assert.ok(!textsOf(page.tree).includes("Panneau React"), "the panel's frame shows the name, the view does not");
  buttonIn(page.tree, "Ajouter un").onClick();
  assert.ok(textsOf(page.tree).includes("Clics : 1"));
});

// The UI palette example, with host stand-ins for Grid, Tabs and Menu (their own logic is in the C++ prelude tests).
function uiPalette(language) {
  const react = fakeReact();
  const h = react.React.h;
  const host = (type) => (props) => h(type, { ...props, text: type === "text" ? props.children.join("") : undefined }, props.children);
  const Tabs = (props) => h("tabs", {}, props.options.map((option) =>
    h("tab", { label: option.label, value: option.value, selected: option.value === props.value,
               onClick: () => props.onChange(option.value) })));
  const Menu = (props) => h("menu", { label: props.label }, props.items.map((item) =>
    h("item", { label: item.label, disabled: item.disabled === true, onClick: item.onClick })));
  const components = { Card: host("card"), Button: host("button"), Text: host("text"), Grid: host("grid"), Tabs, Menu };
  const Ampify = {
    React: react.React,
    components,
    ReactDOM: { render: (tree, target) => (assert.equal(target, "panel:ui-palette"), react.mount(() => tree)) },
    i18n: fakeI18n(language),
    log: () => {},
    ready: Promise.resolve(),
  };
  runIn(load("ui-palette").source, { Ampify, setTimeout, clearTimeout });
  return react.page;
}

test("ui-palette counts a click on each cell of its grid", () => {
  const page = uiPalette("fr");
  assert.ok(buttonIn(page.tree, "Case 1 : 0"), "the grid starts with four cells at zero");
  buttonIn(page.tree, "Case 1 : 0").onClick();
  assert.ok(buttonIn(page.tree, "Case 1 : 1"));
  assert.ok(buttonIn(page.tree, "Case 4 : 0"));
});

test("ui-palette switches to its menu tab and resets the cells from the More flyout", () => {
  const page = uiPalette("en");
  buttonIn(page.tree, "Cell 1: 0").onClick();
  allNodes(page.tree).find((node) => node.type === "tab" && node.label === "Menu").onClick();
  const menu = allNodes(page.tree).find((node) => node.type === "menu");
  assert.equal(menu.label, "More");
  assert.ok(allNodes(page.tree).some((node) => node.type === "item" && node.label === "Not available" && node.disabled));
  allNodes(page.tree).find((node) => node.type === "item" && node.label === "Reset the cells").onClick();
  allNodes(page.tree).find((node) => node.type === "tab" && node.label === "Grid").onClick();
  assert.ok(buttonIn(page.tree, "Cell 1: 0"), "the reset put the count back to zero");
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
      for (const timer of Array.from(world.timers)) {
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

test("playback-guard restarts a song frozen while playing, then sends it back where it froze", () => {
  const { world, run } = fakeGuard();
  world.state = rollout();
  run(14);
  assert.deepEqual(world.commands, [], "a song that plays is left alone");

  run(5, { frozen: true });
  assert.deepEqual(world.commands, ["previous"], "past 3 s, previous restarts the same song");
  assert.match(world.logs.join("\n"), /Rollout.*14 s/);

  world.state = rollout(0);  // restarted, from its start
  run(2);
  assert.deepEqual(world.commands, ["previous", "seek 14000"]);
  assert.match(world.logs.join("\n"), /back to 14 s \(sent\)/);
});

test("playback-guard reloads the song through the next one when the restart does not take", () => {
  const { world, run } = fakeGuard();
  world.state = rollout(14_000);
  run(10, { frozen: true });
  assert.deepEqual(world.commands, ["previous", "next"]);

  world.state = { ...other(), playing: true, positionMs: 0 };  // the next song, still loading
  run(1, { frozen: true });
  assert.deepEqual(world.commands, ["previous", "next"], "no previous while the next song loads: Apple ignores it then");
  run(1);
  assert.deepEqual(world.commands, ["previous", "next", "previous"]);
  world.state = { ...rollout(0), track: { ...rollout().track, queueItemId: "q7" } };  // back, with a new queue entry
  run(2);
  assert.deepEqual(world.commands, ["previous", "next", "previous", "seek 14000"]);
});

test("playback-guard sends previous again when Apple ignored it, never past the next song's first seconds", () => {
  const { world, run } = fakeGuard();
  world.state = rollout(14_000);
  run(10, { frozen: true });
  world.state = other();
  run(25);
  assert.deepEqual(world.commands, ["previous", "next", "previous", "previous"]);
  assert.match(world.logs.join("\n"), /did not play again after the reload: Apple did not go back/);
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
  assert.equal(world.commands.filter((name) => name === "next").length, 2, "two repairs, then the song is left alone");
});

// ---- skip-list and new-releases ---------------------------------------------------------------------------------

// One stand-in for the player, the playback bar, menus, the sidebar, the panel, http and timers. `used` collects each
// permission the script really asked for, to compare with the manifest.
function featureAmpify(name, { config = {}, storage = new Map(), language = "en", state = null, locale = "en-US" } = {}) {
  const { manifest, source } = load(name);
  const granted = new Set(manifest.permissions ?? []);
  const used = new Set();
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
    used.add(permission);
  };
  const react = fakeReact();
  const h = react.React.h;
  const host = (type) => (props) => h(type, { ...props, text: type === "text" ? props.children.join("") : undefined }, props.children);
  const world = {
    logs: [], commands: [], buttons: [], menus: [], labels: [], sidebar: [], navigated: [], opened: [], gets: [],
    intervals: [], stops: [], handlers: { trackchange: [], statechange: [] }, target: "", inFlight: 0, maxInFlight: 0,
    active: new Map(),
    getAnswer: () => ({ status: 0, text: "" }),
    queue: { available: true, complete: true, items: [] },  // null: queue.list() throws
    state: state ?? { playing: false, positionMs: 0, durationMs: 0, track: null },
  };
  const command = (label) => (...args) => (need("player.control"), world.commands.push([label, ...args].join(" ")), true);
  const Ampify = {
    log: (message) => world.logs.push(String(message)),
    onStop: (fn) => world.stops.push(fn),
    locale,
    config: { get: (key, fallback) => (key in config ? config[key] : fallback) },
    storage: {
      get: (key, fallback) => (need("storage"), storage.has(key) ? structuredClone(storage.get(key)) : fallback),
      set: (key, value) => (need("storage"), storage.set(key, structuredClone(value))),
    },
    player: {
      state: () => (need("player.read"), structuredClone(world.state)),
      on: (event, fn) => (need("player.read"), world.handlers[event].push(fn)),
      next: command("next"),
      seek: command("seek"),
      queue: {
        list: () => {
          need("player.read");
          if (!world.queue) throw new Error("queue unavailable");
          return structuredClone(world.queue);
        },
      },
    },
    uri: {
      build: (kind, id, options = {}) => `https://music.apple.com/${options.storefront || "us"}/${kind}/_/${id}`,
    },
    ui: {
      menu: { addItem: (kind, item) => (need("ui.menu"), world.menus.push({ kind, ...item })) },
      playbar: { addButton: (button) => (need("ui.playbar"), world.buttons.push(button), world.buttons.length) },
      setLabel: (id, label) => (need("ui.playbar"), world.labels.push([id, label])),
      setActive: (id, on) => (need("ui.playbar"), world.active.set(id, on)),
      sidebar: { addItem: (item) => (need("ui.sidebar"), world.sidebar.push(item)) },
      navigate: (path) => (need("ui.page"), world.navigated.push(path)),
    },
    React: react.React,
    components: Object.fromEntries(["Card", "Button", "Toggle", "Dropdown", "Text", "TextField"].map((n) => [n, host(n.toLowerCase())])),
    ReactDOM: { render: (tree, target) => (need("ui." + target.split(":")[0]), (world.target = target), react.mount(() => tree)) },
    http: {
      get: (url) => {
        need(`network:${new URL(url).host}`);
        world.gets.push(url);
        world.maxInFlight = Math.max(world.maxInFlight, ++world.inFlight);
        return settle().then(() => ({ ...world.getAnswer(url) })).finally(() => world.inFlight--);
      },
      open: (url) => (need(`network:${new URL(url).host}`), world.opened.push(url), true),
    },
    i18n: fakeI18n(language),
  };
  const context = {
    Ampify,
    setInterval: (fn, ms) => world.intervals.push({ fn, ms, cleared: false }) - 1,
    clearInterval: (id) => { if (world.intervals[id]) world.intervals[id].cleared = true; },
    setTimeout: (fn) => setImmediate(fn),
  };
  runIn(source, context);
  const emit = (event, next) => {
    world.state = next;
    for (const fn of world.handlers[event]) fn(structuredClone(next));
  };
  const tick = () => world.intervals.filter((interval) => !interval.cleared).forEach((interval) => interval.fn());
  return { world, context, manifest, used, storage, emit, tick, react };
}

const declared = (manifest) => [...(manifest.permissions ?? [])].sort();

test("skip-list skips a name from the context menu, whatever its case and accents, once per track", () => {
  const fake = featureAmpify("skip-list");
  assert.equal(fake.world.menus[0].kind, "context");
  assert.equal(fake.world.menus[0].label, "Never play this");
  fake.world.menus[0].onClick({ title: "BEYONCÉ" });
  fake.world.menus[0].onClick({ title: "beyonce" });
  assert.deepEqual(fake.storage.get("entries"), [{ name: "BEYONCÉ", artist: "" }], "the same name is listed once");

  fake.emit("trackchange", playing(song("1", "Halo", "Beyoncé"), 200_000));
  fake.emit("statechange", playing(song("1", "Halo", "Beyoncé"), 200_000, 5_000));
  assert.deepEqual(fake.world.commands, ["next"]);
  assert.match(fake.world.logs.at(-1), /skipped "Halo" by Beyoncé/);

  fake.emit("trackchange", playing(song("2", "Halo", "Someone"), 200_000));
  assert.deepEqual(fake.world.commands, ["next"], "another artist plays");
});

test("skip-list matches a whole name, on the title, the artist or the album", () => {
  const entries = [{ name: "Air", artist: "" }, { name: "Greatest Hits", artist: "" }];
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", entries]]) });
  fake.emit("trackchange", playing(song("1", "Airbourne", "Airbourne Band", "Other"), 200_000));
  assert.deepEqual(fake.world.commands, [], "Air is not Airbourne");
  fake.emit("trackchange", playing(song("2", "Song", "Someone", "greatest HITS"), 200_000));
  assert.deepEqual(fake.world.commands, ["next"]);
  fake.emit("trackchange", playing(song("3", "Air", "Someone", "Other"), 200_000));
  assert.deepEqual(fake.world.commands, ["next", "next"]);
});

test("skip-list's button lists the playing song with its artist and skips it", () => {
  const fake = featureAmpify("skip-list");
  assert.equal(fake.world.buttons[0].label, "Skip and never play again");
  fake.emit("trackchange", playing(song("1", "Halo", "Beyoncé"), 200_000));
  fake.world.buttons[0].onClick();
  assert.deepEqual(fake.storage.get("entries"), [{ name: "Halo", artist: "Beyoncé" }]);
  assert.deepEqual(fake.world.commands, ["next"], "skipped once, not again by the check");

  fake.emit("trackchange", playing(song("2", "Halo", "Someone"), 200_000));
  assert.deepEqual(fake.world.commands, ["next"], "the same title by another artist plays");
  fake.emit("trackchange", playing(song("3", "halo", "BEYONCE"), 200_000));
  assert.deepEqual(fake.world.commands, ["next", "next"]);
});

test("skip-list leaves live versions, remixes and intros alone until its settings are on", () => {
  const titles = ["Song (Live)", "Song - Live at Wembley", "Song [Live]", "Song (Club Remix)", "Intro", "Interlude", "Funny Skit"];
  const fine = ["Live Forever", "Introduction", "Alive"];
  const off = featureAmpify("skip-list");
  titles.forEach((title, i) => off.emit("trackchange", playing(song(String(i), title, "A"), 100_000)));
  assert.deepEqual(off.world.commands, []);

  const on = featureAmpify("skip-list", { config: { "skip-live": true, "skip-remixes": true, "skip-intros": true } });
  titles.forEach((title, i) => on.emit("trackchange", playing(song("t" + i, title, "A"), 100_000)));
  assert.equal(on.world.commands.length, titles.length);
  fine.forEach((title, i) => on.emit("trackchange", playing(song("f" + i, title, "A"), 100_000)));
  assert.equal(on.world.commands.length, titles.length, "plain titles play");

  const liveOnly = featureAmpify("skip-list", { config: { "skip-live": true } });
  liveOnly.emit("trackchange", playing(song("1", "Song (Club Remix)", "A"), 100_000));
  liveOnly.emit("trackchange", playing(song("2", "Song (Live)", "A"), 100_000));
  assert.deepEqual(liveOnly.world.commands, ["next"], "each setting acts alone");
});

test("skip-list's list opens from Apple Music's menu only, newest first, and removes an entry", () => {
  const entries = [{ name: "Old", artist: "" }, { name: "Halo", artist: "Beyoncé" }];
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", entries]]) });
  assert.equal(fake.world.target, "", "nothing opens when Apple Music starts");
  const open = fake.world.menus.find((item) => item.kind === "app");
  assert.equal(open.label, "Skip list");
  open.onClick({});
  assert.equal(fake.world.target, "popup:skip-list");
  assert.deepEqual(textsOf(fake.react.page.tree), ["Halo · Beyoncé", "Old"]);

  fake.world.menus[0].onClick({ title: "New one" });
  assert.equal(textsOf(fake.react.page.tree)[0], "New one", "the list follows the context menu");
  buttonIn(fake.react.page.tree, "Remove").onClick();
  assert.deepEqual(fake.storage.get("entries").map((entry) => entry.name), ["Old", "Halo"]);

  const empty = featureAmpify("skip-list");
  empty.world.menus.find((item) => item.kind === "app").onClick({});
  assert.match(textsOf(empty.react.page.tree)[0], /Nothing is skipped yet/);
});

test("skip-list speaks French on a French Windows and declares exactly the permissions it uses", () => {
  const fake = featureAmpify("skip-list", { language: "fr" });
  assert.equal(fake.world.menus[0].label, "Ne jamais jouer");
  fake.world.menus[0].onClick({ title: "X" });
  fake.emit("trackchange", playing(song("1", "X", "A"), 100_000));
  fake.world.buttons[0].onClick();
  const open = fake.world.menus.find((item) => item.kind === "app");
  assert.equal(open.label, "Liste d'exclusion");
  open.onClick({});
  assert.deepEqual([...fake.used].sort(), declared(fake.manifest));
});

const catalogQueue = (title, catalogId) => ({
  available: true, complete: true,
  items: [
    { id: "q0", title: "Earlier", artist: "A", section: "history", catalogId: "1" },
    { id: "q1", title, artist: "A", section: "current", catalogId },
  ],
});
const fieldsOf = (tree) => allNodes(tree).filter((node) => node.type === "textfield");
const openAppMenu = (fake) => fake.world.menus.find((item) => item.kind === "app").onClick({});
const buttonLabels = (tree) => allNodes(tree).filter((node) => node.type === "button").map((node) => node.label);
const pasteAndMerge = (fake, text) => {
  buttonIn(fake.react.page.tree, "Import").onClick();
  fieldsOf(fake.react.page.tree)[0].onChange(text);
  buttonIn(fake.react.page.tree, "Add to the list").onClick();
  return textsOf(fake.react.page.tree);
};

test("skip-list keeps the kind and catalog id of a menu item, and drops what is odd", () => {
  const fake = featureAmpify("skip-list");
  const click = fake.world.menus[0].onClick;
  click({ title: "Halo", item: { kind: "song", id: "123", canonicalId: "x" } });
  click({ title: "Air", item: { kind: "artist", id: "45" } });
  click({ title: "Odd", item: { kind: "bogus", id: "12a" } });
  click({ title: "Local", item: { kind: "song" } });
  click({ title: "Long", item: { kind: "album", id: "1".repeat(20) } });
  click({ title: "Halo", item: { kind: "song", id: "123" } });
  assert.deepEqual(fake.storage.get("entries"), [
    { name: "Halo", artist: "", kind: "song", id: "123" },
    { name: "Air", artist: "", kind: "artist", id: "45" },
    { name: "Odd", artist: "" },
    { name: "Local", artist: "", kind: "song" },
    { name: "Long", artist: "", kind: "album" },
  ]);
});

test("skip-list matches an album entry on the album only and an artist entry on the artist only", () => {
  const entries = [{ name: "Greatest Hits", artist: "", kind: "album" }, { name: "Air", artist: "", kind: "artist" }];
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", entries]]) });
  fake.emit("trackchange", playing(song("1", "Greatest Hits", "Someone", "Other"), 100_000));
  fake.emit("trackchange", playing(song("2", "Air", "Someone", "Air"), 100_000));
  assert.deepEqual(fake.world.commands, [], "a song or an album of that name is not the artist or the album");
  fake.emit("trackchange", playing(song("3", "Song", "Someone", "greatest HITS"), 100_000));
  fake.emit("trackchange", playing(song("4", "Song", "AIR", "Other"), 100_000));
  assert.deepEqual(fake.world.commands, ["next", "next"]);
});

test("skip-list matches a song entry on its catalog id, else on its name", () => {
  const entries = [{ name: "Old Title", artist: "", kind: "song", id: "123" }, { name: "Halo", artist: "Beyoncé", kind: "song" }];
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", entries]]) });
  fake.world.queue = catalogQueue("Renamed", "123");
  fake.emit("trackchange", playing(song("1", "Renamed", "Someone"), 100_000));
  assert.deepEqual(fake.world.commands, ["next"], "the catalog id of the current queue item matches");

  fake.world.queue = catalogQueue("Other", "999");
  fake.emit("trackchange", playing(song("2", "Other", "Someone"), 100_000));
  fake.world.queue = catalogQueue("Elsewhere", "123");
  fake.emit("trackchange", playing(song("3", "Not that one", "Someone"), 100_000));
  assert.deepEqual(fake.world.commands, ["next"], "another id, or a queue item that is not the playing song, does not match");

  fake.emit("trackchange", playing(song("4", "halo", "BEYONCE", "Air"), 100_000));
  fake.emit("trackchange", playing(song("5", "Halo", "Someone"), 100_000));
  fake.emit("trackchange", playing(song("6", "Song", "Halo", "Halo"), 100_000));
  assert.deepEqual(fake.world.commands, ["next", "next"], "the name falls back on the title and the artist, not on an album");
});

test("skip-list falls back on the name when the queue is missing, unavailable or throws", () => {
  const entry = { name: "Halo", artist: "", kind: "song", id: "123" };
  for (const queue of [null, { available: false, items: [] }, {}, { items: [null, { section: "current" }] }]) {
    const fake = featureAmpify("skip-list", { storage: new Map([["entries", [entry]]]) });
    fake.world.queue = queue;
    fake.emit("trackchange", playing(song("1", "Halo", "Someone"), 100_000));
    fake.emit("trackchange", playing(song("2", "Other", "Someone"), 100_000));
    assert.deepEqual(fake.world.commands, ["next"], JSON.stringify(queue));
  }
});

test("skip-list does not match a song entry with a known id on a same-titled song with another id", () => {
  const entries = [{ name: "Halo", artist: "", kind: "song", id: "123" }];
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", entries]]) });
  fake.world.queue = catalogQueue("Halo", "999");
  fake.emit("trackchange", playing(song("1", "Halo", "Someone"), 100_000));
  assert.deepEqual(fake.world.commands, [], "another catalog id is another song");
  fake.world.queue = catalogQueue("Halo", "123");
  fake.emit("trackchange", playing(song("2", "Halo", "Someone"), 100_000));
  assert.deepEqual(fake.world.commands, ["next"]);
});

test("skip-list matches a kind it cannot read from a track on nothing, and a legacy entry on any name", () => {
  const kinds = ["playlist", "composer", "genre", "station", "video", "concert", "container"];
  const entries = kinds.map((kind) => ({ name: "Match", artist: "", kind }));
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", entries]]) });
  fake.emit("trackchange", playing(song("1", "Match", "Match", "Match"), 100_000));
  assert.deepEqual(fake.world.commands, [], "a title, artist or album of that name is not a playlist or a genre");

  const legacy = featureAmpify("skip-list", { storage: new Map([["entries", [{ name: "Match", artist: "" }]]]) });
  legacy.emit("trackchange", playing(song("2", "Song", "Someone", "match"), 100_000));
  assert.deepEqual(legacy.world.commands, ["next"]);
});

test("skip-list cuts an export by UTF-8 bytes, so Japanese, Chinese and Russian names fit a text field", () => {
  const long = (char) => char.repeat(190);
  const list = [
    ...Array.from({ length: 12 }, (_, i) => ({ name: "日本語の曲名" + i, artist: "アーティスト" + i })),
    { name: long("中"), artist: long("Я") },
    { name: "Ж".repeat(60), artist: "😀".repeat(60) },
  ];
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", list]]) });
  openAppMenu(fake);
  buttonIn(fake.react.page.tree, "Export").onClick();
  const parts = [];
  for (;;) {
    const [field] = fieldsOf(fake.react.page.tree);
    assert.ok(Buffer.byteLength(field.value, "utf8") <= 500, "a text holds 500 bytes at most");
    parts.push(field.value);
    const next = allNodes(fake.react.page.tree).find((node) => node.type === "button" && node.label === "Next");
    if (!next) break;
    next.onClick();
  }
  const exported = parts.flatMap((part) => JSON.parse(part));
  assert.equal(exported.length, list.length);
  assert.deepEqual(exported.slice(0, 12), list.slice(0, 12));
  assert.ok(Buffer.byteLength(exported[12].name, "utf8") <= 80, "a long name is cut by bytes");
  assert.ok(exported[12].name.length > 0 && exported[13].artist.length > 0);
  assert.ok(!exported.some((entry) => /�|[\ud800-\udbff](?![\udc00-\udfff])/.test(entry.name + entry.artist)), "no character cut in half");
});

test("skip-list exports its list as compact JSON, in parts a text field can hold", () => {
  const list = Array.from({ length: 40 }, (_, i) => ({ name: "Song " + i + "x".repeat(20), artist: "Artist " + i }));
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", list]]) });
  openAppMenu(fake);
  buttonIn(fake.react.page.tree, "Export").onClick();
  const parts = [];
  for (;;) {
    const [field] = fieldsOf(fake.react.page.tree);
    assert.ok(field.value.length <= 500, "a text holds 500 characters at most");
    parts.push(field.value);
    const next = allNodes(fake.react.page.tree).find((node) => node.type === "button" && node.label === "Next");
    if (!next) break;
    assert.match(textsOf(fake.react.page.tree).join("|"), new RegExp(`Part ${parts.length} of \\d+`));
    next.onClick();
  }
  assert.ok(parts.length > 1);
  assert.deepEqual(parts.flatMap((part) => JSON.parse(part)), list);
  assert.ok(parts.every((part) => !part.includes("\n") && !part.includes(": ")), "compact");

  const empty = featureAmpify("skip-list", { storage: new Map([["entries", list.slice(0, 2)]]) });
  openAppMenu(empty);
  buttonIn(empty.react.page.tree, "Export").onClick();
  assert.equal(fieldsOf(empty.react.page.tree)[0].value, JSON.stringify(list.slice(0, 2)));
  assert.ok(!buttonLabels(empty.react.page.tree).includes("Next"), "one part has no Next");
  buttonIn(empty.react.page.tree, "Done").onClick();
  assert.equal(fieldsOf(empty.react.page.tree).length, 0);
});

test("skip-list imports what it exported, part by part", () => {
  const list = [
    { name: "Halo", artist: "Beyoncé", kind: "song", id: "123" }, { name: "Air", artist: "", kind: "artist" },
    { name: "Greatest Hits", artist: "" }, { name: "He said \"no\"", artist: "A\\B" },
  ];
  const source = featureAmpify("skip-list", { storage: new Map([["entries", list]]) });
  openAppMenu(source);
  buttonIn(source.react.page.tree, "Export").onClick();
  const exported = fieldsOf(source.react.page.tree)[0].value;

  const target = featureAmpify("skip-list");
  openAppMenu(target);
  assert.ok(pasteAndMerge(target, exported).includes("Added 4, skipped 0."));
  assert.deepEqual(target.storage.get("entries"), list);
  assert.ok(pasteAndMerge(target, exported).includes("Added 0, skipped 4."), "a second import adds nothing");
  assert.deepEqual(target.storage.get("entries"), list);
});

test("skip-list's import validates strictly, drops unknown fields and skips duplicates", () => {
  const known = [{ name: "Known", artist: "" }];
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", known]]) });
  openAppMenu(fake);
  const long = "n".repeat(300);
  const texts = pasteAndMerge(fake, JSON.stringify([
    { name: "Halo", artist: "Beyoncé", kind: "song", id: "123", extra: "x", count: 5 },
    { name: "halo", artist: "BEYONCÉ", kind: "song", id: "123" },
    { name: "KNOWN" },
    { name: "" }, { name: "  " }, { name: 5 }, "text", null, [1], { artist: "No name" },
    { name: "Air", kind: "nope", id: "x1" },
    { name: "Album", artist: 7 },
    { name: long },
  ]));
  assert.ok(texts.includes("Added 3, skipped 10."), texts.join("|"));
  assert.deepEqual(fake.storage.get("entries"), [
    ...known,
    { name: "Halo", artist: "Beyoncé", kind: "song", id: "123" },
    { name: "Air", artist: "" },
    { name: "n".repeat(200), artist: "" },
  ]);
});

test("skip-list's import reports invalid text and never throws", () => {
  const fake = featureAmpify("skip-list");
  openAppMenu(fake);
  for (const [text, message] of [
    ["", "This is not valid JSON."], ["{nope", "This is not valid JSON."], ["[{\"name\":\"A\"}", "This is not valid JSON."],
    ["{\"name\":\"A\"}", "The text must be a JSON list of entries."], ["\"text\"", "The text must be a JSON list of entries."],
    ["null", "The text must be a JSON list of entries."], ["x".repeat(200_001), "This text is too long to import."],
  ]) {
    assert.ok(pasteAndMerge(fake, text).includes(message), `${text.slice(0, 20)}: ${message}`);
  }
  assert.equal(fake.storage.get("entries"), undefined, "nothing was stored");
});

test("skip-list's import stops at 500 entries and keeps the ones it has", () => {
  const known = Array.from({ length: 499 }, (_, i) => ({ name: "Known " + i, artist: "" }));
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", known]]) });
  openAppMenu(fake);
  const texts = pasteAndMerge(fake, JSON.stringify([{ name: "New 1" }, { name: "New 2" }, { name: "New 3" }]));
  assert.ok(texts.includes("Added 1, skipped 2."), texts.join("|"));
  const entries = fake.storage.get("entries");
  assert.equal(entries.length, 500);
  assert.deepEqual(entries.slice(0, 499), known);
  assert.equal(entries[499].name, "New 1");
});

test("skip-list's panel stays under the interface's 300 nodes with a full list, in every view", () => {
  const full = Array.from({ length: 500 }, (_, i) => ({ name: "Entry " + i, artist: "Artist", kind: "song", id: String(i + 1) }));
  const fake = featureAmpify("skip-list", { storage: new Map([["entries", full]]) });
  openAppMenu(fake);
  assert.ok(allNodes(fake.react.page.tree).length <= 300, "list");
  buttonIn(fake.react.page.tree, "Export").onClick();
  assert.ok(allNodes(fake.react.page.tree).length <= 300, "export");
  buttonIn(fake.react.page.tree, "Done").onClick();
  buttonIn(fake.react.page.tree, "Import").onClick();
  assert.ok(allNodes(fake.react.page.tree).length <= 300, "import");
  for (const text of textsOf(fake.react.page.tree)) assert.ok(text.length <= 500);
});

test("skip-list has every new string in all eight languages, and a new version", () => {
  const fake = featureAmpify("skip-list");
  const { tables } = fake.context.Ampify.i18n;
  const english = Object.keys(tables.en).sort();
  for (const code of languages) {
    assert.deepEqual(Object.keys(tables[code]).sort(), english, code);
    for (const key of ["export", "import", "merge", "added", "invalid"]) assert.notEqual(tables[code][key], tables.en[key], `${code}.${key}`);
  }
  assert.equal(fake.manifest.version, "1.1.0");
  for (const code of languages) assert.ok(fake.manifest.translations[code].description.length > 20);
});

// ---- bookmarks ---------------------------------------------------------------------------------------------------

const withoutDate = (list) => list.map(({ added, ...rest }) => (assert.equal(typeof added, "number"), rest));
const rowWith = (tree, text) =>
  allNodes(tree).find((node) => node.type === "row" && textsOf(node).some((shown) => shown.startsWith(text)));
const bookmarked = (list, options = {}) => featureAmpify("bookmarks", { storage: new Map([["bookmarks", list]]), ...options });

test("bookmarks stores a menu item with its kind and id, only the ones Apple gives", () => {
  const fake = featureAmpify("bookmarks");
  assert.equal(fake.world.menus[0].kind, "context");
  assert.equal(fake.world.menus[0].label, "Bookmark");
  const click = fake.world.menus[0].onClick;
  click({ title: "Halo", item: { kind: "song", id: "123", canonicalId: "x" } });
  click({ title: "Halo", item: { kind: "song", id: "123" } });
  click({ title: "Chill", item: { kind: "playlist", id: "pl.u-abc" } });
  click({ title: "Local song" });
  click({ title: "Odd", item: { kind: "bogus", id: "../x" } });
  click({ title: "" });
  assert.deepEqual(withoutDate(fake.storage.get("bookmarks")), [
    { title: "Halo", kind: "song", id: "123" }, { title: "Chill", kind: "playlist", id: "pl.u-abc" },
    { title: "Local song" }, { title: "Odd" },
  ]);
});

test("bookmarks' button saves the playing song with its position and catalog id when known", () => {
  const fake = featureAmpify("bookmarks");
  assert.equal(fake.world.buttons[0].label, "Bookmark this moment");
  assert.equal(fake.world.buttons[0].icon, "");
  fake.world.buttons[0].onClick();
  assert.equal(fake.storage.get("bookmarks"), undefined, "nothing plays");

  fake.emit("statechange", playing(song("1", "Halo", "Beyoncé"), 200_000, 83_000));
  fake.world.queue = catalogQueue("Halo", "555");
  fake.world.buttons[0].onClick();
  fake.world.queue = null;
  fake.emit("statechange", playing(song("1", "Halo", "Beyoncé"), 200_000, 90_000));
  fake.world.buttons[0].onClick();
  assert.deepEqual(withoutDate(fake.storage.get("bookmarks")), [
    { title: "Halo", artist: "Beyoncé", positionMs: 83_000, catalogId: "555" },
    { title: "Halo", artist: "Beyoncé", positionMs: 90_000 },
  ]);
});

test("bookmarks keeps 200 at most and cuts long titles", () => {
  const old = Array.from({ length: 200 }, (_, i) => ({ title: "Old " + i, added: i }));
  const fake = bookmarked(old);
  fake.world.menus[0].onClick({ title: "t".repeat(300) });
  const stored = fake.storage.get("bookmarks");
  assert.equal(stored.length, 200);
  assert.equal(stored[0].title, "Old 1", "the oldest goes");
  assert.equal(stored.at(-1).title.length, 200);
});

test("bookmarks' list opens from Apple Music's menu only, newest first, with moments as m:ss", () => {
  const list = [
    { title: "Old", kind: "album", id: "1", added: 1 },
    { title: "Halo", artist: "Beyoncé", positionMs: 83_000, added: 2 },
    { title: "Long", artist: "A", positionMs: 3_725_000, added: 3 },
  ];
  const fake = bookmarked(list);
  assert.equal(fake.world.target, "");
  const open = fake.world.menus.find((item) => item.kind === "app");
  assert.equal(open.label, "Bookmarks");
  open.onClick({});
  assert.equal(fake.world.target, "popup:bookmarks");
  assert.deepEqual(textsOf(fake.react.page.tree), ["Long · A · 62:05", "Halo · Beyoncé · 1:23", "Old"]);
  fake.world.menus[0].onClick({ title: "New", item: { kind: "artist", id: "9" } });
  assert.equal(textsOf(fake.react.page.tree)[0], "New", "the list follows the menu");

  const empty = featureAmpify("bookmarks");
  empty.world.menus.find((item) => item.kind === "app").onClick({});
  assert.match(textsOf(empty.react.page.tree)[0], /Nothing is bookmarked yet/);
});

test("bookmarks opens an item in Apple Music with the link built from its kind and id", () => {
  const list = [{ title: "Chill", kind: "playlist", id: "pl.u-abc", added: 1 }, { title: "Album", kind: "album", id: "77", added: 2 }];
  const fake = bookmarked(list, { locale: "fr-FR" });
  openAppMenu(fake);
  buttonIn(rowWith(fake.react.page.tree, "Album"), "Open").onClick();
  buttonIn(rowWith(fake.react.page.tree, "Chill"), "Open").onClick();
  assert.deepEqual(fake.world.opened, ["https://music.apple.com/fr/album/_/77", "https://music.apple.com/fr/playlist/_/pl.u-abc"]);
  const plain = bookmarked(list);
  openAppMenu(plain);
  buttonIn(rowWith(plain.react.page.tree, "Album"), "Open").onClick();
  assert.equal(plain.world.opened[0], "https://music.apple.com/us/album/_/77");
});

test("bookmarks seeks inside the song playing now, else opens the song by its catalog id", () => {
  const moment = { title: "Halo", artist: "Beyoncé", positionMs: 83_000, catalogId: "555", added: 1 };
  const fake = bookmarked([moment]);
  openAppMenu(fake);
  fake.emit("trackchange", playing(song("9", "Other", "Someone"), 100_000));
  buttonIn(rowWith(fake.react.page.tree, "Halo"), "Open").onClick();
  assert.deepEqual(fake.world.opened, ["https://music.apple.com/us/song/_/555"]);
  assert.deepEqual(fake.world.commands, []);

  fake.world.queue = catalogQueue("Halo", "555");
  fake.emit("trackchange", playing(song("1", "Halo", "Beyoncé feat. Someone"), 200_000));
  buttonIn(rowWith(fake.react.page.tree, "Halo"), "Open").onClick();
  assert.deepEqual(fake.world.commands, ["seek 83000"], "same catalog id");
  assert.equal(fake.world.opened.length, 1);

  const named = bookmarked([{ title: "Halo", artist: "Beyoncé", positionMs: 5_000, added: 1 }]);
  openAppMenu(named);
  named.emit("trackchange", playing(song("1", "HALO", "beyonce"), 200_000));
  buttonIn(rowWith(named.react.page.tree, "Halo"), "Open").onClick();
  assert.deepEqual(named.world.commands, ["seek 5000"], "same name and artist when no id is known");
  named.emit("trackchange", playing(song("2", "Else", "Someone"), 200_000));
  const row = rowWith(named.react.page.tree, "Halo");
  assert.equal(buttonIn(row, "Open").disabled, true, "no id and not playing: nothing to open");
  buttonIn(row, "Open").onClick();
  assert.equal(named.world.opened.length, 0);
});

test("bookmarks removes an entry", () => {
  const fake = bookmarked([{ title: "A", added: 1 }, { title: "B", added: 2 }]);
  openAppMenu(fake);
  buttonIn(rowWith(fake.react.page.tree, "B"), "Remove").onClick();
  assert.deepEqual(withoutDate(fake.storage.get("bookmarks")), [{ title: "A" }]);
  assert.deepEqual(textsOf(fake.react.page.tree), ["A"]);
});

test("bookmarks' panel stays under 300 nodes, speaks French, and declares exactly the permissions it uses", () => {
  const full = Array.from({ length: 200 }, (_, i) => ({ title: "Song " + i, kind: "song", id: String(i + 1), added: i }));
  const big = bookmarked(full);
  openAppMenu(big);
  assert.ok(allNodes(big.react.page.tree).length <= 300);

  const fake = bookmarked([{ title: "Halo", kind: "song", id: "1", added: 1 }], { language: "fr" });
  assert.equal(fake.world.menus[0].label, "Ajouter un signet");
  fake.world.queue = catalogQueue("Halo", "1");
  fake.emit("statechange", playing(song("1", "Halo", "A"), 100_000, 1_000));
  fake.world.buttons[0].onClick();
  openAppMenu(fake);
  allNodes(fake.react.page.tree).filter((node) => node.label === "Ouvrir").forEach((node) => node.onClick());
  buttonIn(fake.react.page.tree, "Retirer").onClick();
  assert.deepEqual(fake.world.commands, ["seek 1000"]);
  assert.equal(fake.world.opened.length, 1);
  assert.deepEqual([...fake.used].sort(), declared(fake.manifest));
  assert.equal(fake.manifest.version, "1.0.0");
});

test("bookmarks has every string in all eight languages", () => {
  const fake = featureAmpify("bookmarks");
  const { tables } = fake.context.Ampify.i18n;
  for (const code of languages) {
    assert.deepEqual(Object.keys(tables[code]).sort(), Object.keys(tables.en).sort(), code);
    assert.ok(fake.manifest.translations[code].name && fake.manifest.translations[code].description, code);
  }
});

const daysAgo = (days) => new Date(Date.now() - days * 86400000).toISOString().replace(/\.\d+Z$/, "Z");
const album = (id, name, days, tracks, extra = {}) => ({
  wrapperType: "collection", collectionType: "Album", collectionId: id, collectionName: name, artistName: "Beyoncé",
  releaseDate: daysAgo(days), trackCount: tracks,
  artworkUrl100: `https://is1-ssl.mzstatic.com/image/thumb/a${id}/100x100bb.jpg`,
  collectionViewUrl: `https://music.apple.com/us/album/x/${id}?uo=4`, ...extra,
});
const catalog = (artistName = "Beyoncé") => (url) => {
  if (url.includes("/search?")) return { status: 200, text: JSON.stringify({ results: [{ wrapperType: "artist", artistName, artistId: 123 }] }) };
  return {
    status: 200,
    text: JSON.stringify({
      results: [
        { wrapperType: "artist", artistName: "Beyoncé", artistId: 123 },
        album(1, "Big Album", 20, 12), album(2, "Cuff It - Single", 5, 1), album(3, "Short Thing - EP", 40, 3),
        album(4, "Old Album", 200, 10),
      ],
    }),
  };
};
const knownArtist = () => new Map([["artists", { beyonce: { name: "Beyoncé", plays: 3, last: 1 } }]]);
const flush = async () => { for (let i = 0; i < 30; i++) await settle(); };

test("new-releases counts the artists you play and keeps the 50 most listened", () => {
  const fake = featureAmpify("new-releases");
  for (let i = 0; i < 55; i++) fake.emit("trackchange", playing(song("t" + i, "Song", "Artist " + i), 100_000));
  for (let i = 0; i < 3; i++) fake.emit("trackchange", playing(song("x", "Song", "Favorite"), 100_000));
  fake.emit("trackchange", playing(song("n", "Song", ""), 100_000));
  const artists = fake.storage.get("artists");
  assert.equal(Object.keys(artists).length, 50);
  assert.equal(artists.favorite.plays, 3);
  assert.ok(artists["artist 54"], "among equals, the most recent stay");
  assert.equal(artists["artist 0"], undefined);
});

test("new-releases asks the catalog for the artist, then their albums, and lists the recent ones newest first", async () => {
  const fake = featureAmpify("new-releases", { storage: knownArtist() });
  fake.world.getAnswer = catalog();
  await flush();
  assert.deepEqual(fake.world.gets, [
    "https://itunes.apple.com/search?term=Beyonc%C3%A9&entity=musicArtist&limit=1&country=us",
    "https://itunes.apple.com/lookup?id=123&entity=album&sort=recent&limit=10&country=us",
  ]);
  assert.equal(fake.storage.get("ids").beyonce.id, 123, "the artist id is cached");
  assert.ok(fake.storage.get("checkedAt") > 0);

  fake.react.mount(fake.context.render);
  const cards = allNodes(fake.react.page.tree).filter((node) => node.type === "card");
  assert.deepEqual(cards.map((card) => textsOf(card)[0]), ["Cuff It", "Big Album", "Short Thing"], "newest first, the 60-day window");
  assert.deepEqual(textsOf(cards[0])[1].split(" · ").slice(0, 2), ["Beyoncé", "Single"]);
  assert.match(textsOf(cards[1])[1], /^Beyoncé · Album · \d{4}-\d{2}-\d{2}$/);
  assert.match(textsOf(cards[2])[1], /^Beyoncé · EP ·/);
  assert.equal(allNodes(cards[0]).find((node) => node.type === "image").src, "https://is1-ssl.mzstatic.com/image/thumb/a2/300x300bb.jpg");

  cards[0].onClick();
  assert.deepEqual(fake.world.opened, ["https://music.apple.com/us/album/x/2"]);
});

test("new-releases looks up each name of a shared credit the catalog does not know as one artist", async () => {
  const shared = new Map([["artists", { "ninho freeze corleone": { name: "Ninho & Freeze Corleone", plays: 3, last: 1 } }]]);
  const fake = featureAmpify("new-releases", { storage: shared });
  const known = { Ninho: 11, "Freeze Corleone": 22 };
  fake.world.getAnswer = (url) => {
    const term = decodeURIComponent((url.match(/term=([^&]+)/) || [])[1] || "");
    if (url.includes("/search?")) {
      const results = known[term] ? [{ wrapperType: "artist", artistName: term, artistId: known[term] }] : [];
      return { status: 200, text: JSON.stringify({ results }) };
    }
    const id = Number(url.match(/id=(\d+)/)[1]);
    return { status: 200, text: JSON.stringify({ results: [album(id, "Joint Album", 5, 12)] }) };
  };
  await flush();
  const searched = fake.world.gets.filter((url) => url.includes("/search?")).map((url) => decodeURIComponent(url.match(/term=([^&]+)/)[1]));
  assert.deepEqual(searched, ["Ninho & Freeze Corleone", "Ninho", "Freeze Corleone"]);
  assert.equal(fake.storage.get("ids").ninho.id, 11);
  assert.equal(fake.storage.get("releases").length, 2);
});

test("new-releases follows the period and singles choices on its page", async () => {
  const fake = featureAmpify("new-releases", { storage: knownArtist() });
  fake.world.getAnswer = catalog();
  await flush();
  fake.react.mount(fake.context.render);
  const names = () => allNodes(fake.react.page.tree).filter((node) => node.type === "card").map((card) => textsOf(card)[0]);

  allNodes(fake.react.page.tree).find((node) => node.type === "dropdown").onChange("30");
  assert.deepEqual(names(), ["Cuff It", "Big Album"]);
  allNodes(fake.react.page.tree).find((node) => node.type === "toggle").onChange(false);
  assert.deepEqual(names(), ["Big Album"]);
  allNodes(fake.react.page.tree).find((node) => node.type === "dropdown").onChange("90");
  assert.deepEqual(names(), ["Big Album", "Short Thing"]);
  assert.deepEqual(fake.storage.get("prefs"), { days: 90, singles: false });
});

test("new-releases uses the storefront of the language, and says so when it has no artist or nothing new", async () => {
  const french = featureAmpify("new-releases", { storage: knownArtist(), language: "fr" });
  french.world.getAnswer = catalog();
  await flush();
  assert.match(french.world.gets[0], /country=fr$/);
  const portuguese = featureAmpify("new-releases", { storage: knownArtist(), language: "pt" });
  portuguese.world.getAnswer = catalog();
  await flush();
  assert.match(portuguese.world.gets[0], /country=br$/);

  const empty = featureAmpify("new-releases");
  empty.react.mount(empty.context.render);
  assert.ok(textsOf(empty.react.page.tree).some((text) => /No artist yet/.test(text)));

  const quiet = featureAmpify("new-releases", { storage: knownArtist() });
  quiet.world.getAnswer = () => ({ status: 200, text: JSON.stringify({ results: [{ artistName: "Beyoncé", artistId: 123 }] }) });
  await flush();
  quiet.react.mount(quiet.context.render);
  assert.ok(textsOf(quiet.react.page.tree).includes("Nothing new from your artists in the last 60 days."));
});

test("new-releases ignores a search hit that is another artist, and retries after a failure", async () => {
  const fake = featureAmpify("new-releases", { storage: knownArtist() });
  fake.world.getAnswer = catalog("Someone Else");
  await flush();
  assert.equal(fake.world.gets.length, 1, "no album request for a stranger");
  assert.equal(fake.storage.get("ids").beyonce.id, 0);

  const down = featureAmpify("new-releases", { storage: knownArtist() });
  await flush();
  assert.equal(down.storage.get("checkedAt"), undefined, "a check where nothing answered does not count");
  assert.equal(down.storage.get("ids"), undefined);
  down.react.mount(down.context.render);
  await flush();  // opening the page asks again
  assert.equal(down.world.gets.length, 2);
  assert.ok(textsOf(down.react.page.tree).includes("Apple's catalog did not answer. Try again later."));
});

test("new-releases checks once a day, or when the page opens on data older than 12 hours", async () => {
  const recent = new Map([...knownArtist(), ["checkedAt", Date.now() - 3600_000], ["releases", []]]);
  const fresh = featureAmpify("new-releases", { storage: recent });
  fresh.world.getAnswer = catalog();
  await flush();
  fresh.react.mount(fresh.context.render);
  await flush();
  assert.equal(fresh.world.gets.length, 0, "checked an hour ago: nothing to ask");

  const aged = new Map([...knownArtist(), ["checkedAt", Date.now() - 13 * 3600_000], ["releases", []]]);
  const opened = featureAmpify("new-releases", { storage: aged });
  opened.world.getAnswer = catalog();
  await flush();
  assert.equal(opened.world.gets.length, 0, "13 hours is not a day");
  opened.react.mount(opened.context.render);
  await flush();
  assert.equal(opened.world.gets.length, 2, "opening the page asks");

  const daily = new Map([...knownArtist(), ["checkedAt", Date.now() - 25 * 3600_000]]);
  const start = featureAmpify("new-releases", { storage: daily });
  start.world.getAnswer = catalog();
  await flush();
  assert.equal(start.world.gets.length, 2, "a day old: asked at start");
  assert.equal(start.world.intervals[0].ms, 3600_000);
  start.world.stops.forEach((stop) => stop());
  assert.equal(start.world.intervals[0].cleared, true);
});

test("new-releases keeps a few requests in flight and declares exactly the permissions it uses", async () => {
  const names = Array.from({ length: 12 }, (_, i) => ["a" + i, { name: "A" + i, plays: 1, last: i }]);
  const fake = featureAmpify("new-releases", { storage: new Map([["artists", Object.fromEntries(names)]]) });
  fake.world.getAnswer = catalog("A0");
  await flush();
  assert.ok(fake.world.maxInFlight >= 2 && fake.world.maxInFlight <= 3, `in flight: ${fake.world.maxInFlight}`);
  assert.ok(fake.world.gets.length >= 12);

  const single = featureAmpify("new-releases", { storage: knownArtist() });
  single.world.getAnswer = catalog();
  await flush();
  single.react.mount(single.context.render);
  allNodes(single.react.page.tree).find((node) => node.type === "card").onClick();
  single.world.sidebar[0].onClick();
  assert.deepEqual(single.world.navigated, ["/new-releases"]);
  assert.deepEqual([...single.used].sort(), declared(single.manifest));
});

// ---- ab-loop -----------------------------------------------------------------------------------------------------

const at = (positionMs, track = song("1", "Riff", "Band"), isPlaying = true) =>
  ({ ...playing(track, 200_000, positionMs), playing: isPlaying });

test("ab-loop marks a start, then an end, and goes back to the start at the end", () => {
  const fake = featureAmpify("ab-loop");
  const button = fake.world.buttons[0];
  assert.equal(button.label, "Loop: mark the start");
  button.onClick();
  assert.equal(fake.world.labels.length, 0, "nothing plays: nothing marked");

  fake.emit("statechange", at(30_000));
  button.onClick();
  assert.deepEqual(fake.world.labels.at(-1), [1, "Loop from 0:30: mark the end"]);
  assert.equal(fake.world.active.get(1), true);
  fake.emit("statechange", at(30_400));
  button.onClick();
  assert.equal(fake.world.intervals.length, 0, "under a second: the end is not taken");

  fake.emit("statechange", at(45_000));
  button.onClick();
  assert.deepEqual(fake.world.labels.at(-1), [1, "Looping 0:30 to 0:45: stop"]);
  fake.emit("statechange", at(44_000));
  fake.tick();
  assert.deepEqual(fake.world.commands, []);
  fake.emit("statechange", at(45_100));
  fake.tick();
  assert.deepEqual(fake.world.commands, ["seek 30000"]);
  fake.emit("statechange", at(46_000, song("1", "Riff", "Band"), false));
  fake.tick();
  assert.deepEqual(fake.world.commands, ["seek 30000"], "paused: it waits");

  button.onClick();
  assert.deepEqual(fake.world.labels.at(-1), [1, "Loop: mark the start"]);
  assert.equal(fake.world.active.get(1), false);
  assert.equal(fake.world.intervals[0].cleared, true);
});

test("ab-loop swaps points marked backwards and stops with another song", () => {
  const fake = featureAmpify("ab-loop", { language: "fr" });
  const button = fake.world.buttons[0];
  fake.emit("statechange", at(90_000));
  button.onClick();
  fake.emit("statechange", at(60_000));
  button.onClick();
  assert.deepEqual(fake.world.labels.at(-1), [1, "Boucle de 1:00 à 1:30 : arrêter"]);
  fake.emit("statechange", at(91_000));
  fake.tick();
  assert.deepEqual(fake.world.commands, ["seek 60000"]);

  fake.emit("trackchange", at(0, song("2", "Other", "Band")));
  assert.deepEqual(fake.world.labels.at(-1), [1, "Boucle : marquer le début"]);
  assert.equal(fake.world.intervals[0].cleared, true);
  fake.world.stops.forEach((stop) => stop());
  assert.deepEqual([...fake.used].sort(), declared(fake.manifest));
});
