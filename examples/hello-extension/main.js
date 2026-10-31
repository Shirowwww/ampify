// Minimal extension example. The Ampify object is the only way in: see docs/extensions/api.md.
const launches = Ampify.storage.get("launches", 0) + 1;
Ampify.storage.set("launches", launches);
Ampify.log("Hello, launch #" + launches);
Ampify.onStop(() => Ampify.log("Goodbye"));
