// Runs each example extension against a fake AMK object, to check that the script does what its
// comments say and only uses the permissions its manifest declares. Node only, no dependency:
//
//     node --test tools/test_examples.mjs

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const examples = join(dirname(fileURLToPath(import.meta.url)), "..", "examples");

// A stand-in for the kernel's AMK object, following docs/extensions/API.md. Permission checks
// mirror the kernel: a function protected by a permission the manifest did not declare throws.
function fakeAmk(manifest, storage = new Map()) {
  const granted = new Set(manifest.permissions ?? []);
  const logs = [];
  const stopHandlers = [];
  const need = (permission) => {
    if (!granted.has(permission)) throw new Error(`PermissionDenied: ${permission}`);
  };
  const amk = {
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
  return { amk, logs, stopHandlers };
}

function load(name) {
  const dir = join(examples, name);
  return {
    manifest: JSON.parse(readFileSync(join(dir, "manifest.json"), "utf-8")),
    source: readFileSync(join(dir, "main.js"), "utf-8"),
  };
}

function run(source, amk) {
  vm.runInNewContext(source, { AMK: amk }, { filename: "main.js" });
}

test("hello-extension counts launches in storage and logs them", () => {
  const { manifest, source } = load("hello-extension");
  const storage = new Map();

  const first = fakeAmk(manifest, storage);
  run(source, first.amk);
  assert.deepEqual(first.logs, ["Bonjour, lancement n° 1"]);
  assert.equal(storage.get("launches"), 1);

  const second = fakeAmk(manifest, storage);
  run(source, second.amk);
  assert.deepEqual(second.logs, ["Bonjour, lancement n° 2"]);
  assert.equal(storage.get("launches"), 2);
});

test("hello-extension says goodbye when it stops", () => {
  const { manifest, source } = load("hello-extension");
  const fake = fakeAmk(manifest);
  run(source, fake.amk);

  assert.equal(fake.stopHandlers.length, 1);
  fake.stopHandlers[0]();
  assert.equal(fake.logs.at(-1), "Au revoir");
});

test("hello-extension fails without the storage permission it needs", () => {
  const { source } = load("hello-extension");
  const withoutPermissions = fakeAmk({ permissions: [] });
  assert.throws(() => run(source, withoutPermissions.amk), /PermissionDenied: storage/);
});

// The namespaces of AMK that need a permission, as API.md section 4 lists them.
const permissionOf = { storage: "storage" };

test("hello-extension declares exactly the permissions its code uses", () => {
  const { manifest, source } = load("hello-extension");
  const needed = [...source.matchAll(/AMK\.(\w+)/g)].map((match) => permissionOf[match[1]]).filter(Boolean);
  const declared = manifest.permissions ?? [];

  for (const permission of needed) assert.ok(declared.includes(permission), `permission manquante : ${permission}`);
  for (const permission of declared) assert.ok(needed.includes(permission), `permission inutile : ${permission}`);
});
