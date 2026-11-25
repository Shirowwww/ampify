// Mini player: Apple's own mini player, easier to reach and to live with. A shortcut opens and closes it from any
// program, its window can be see-through, and a star marks the song playing as a favourite. Ampify's tray icon has its
// own entry too. The album's animated cover in it is the Animated cover extension's.

const SHORTCUTS = ["Ctrl+Shift+Alt+M", "Ctrl+Alt+M"];  // the setting's choices, "None" aside

const options = {
  opacity: parseInt(String(Ampify.config.get("opacity")), 10) / 100 || 1,  // "80 %"
  favorite: Ampify.config.get("favorite") === true,
};

const shortcut = String(Ampify.config.get("shortcut"));
if (SHORTCUTS.includes(shortcut)) {
  try {
    Ampify.keyboard.bind(shortcut, () => Ampify.ui.miniPlayer.toggle());
  } catch (error) {
    Ampify.log("shortcut " + shortcut + " not available: " + error);
  }
}
Ampify.ui.miniPlayer.set(options);
