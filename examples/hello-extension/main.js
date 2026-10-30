// Exemple minimal d'extension. L'objet AMK est le seul accès au kit : voir docs/extensions/API.md.
const launches = AMK.storage.get("launches", 0) + 1;
AMK.storage.set("launches", launches);
AMK.log("Bonjour, lancement n° " + launches);
AMK.onStop(() => AMK.log("Au revoir"));
