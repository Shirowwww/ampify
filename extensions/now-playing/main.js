// Now Playing: a button of the playback bar that opens Apple Music's own full-screen player, laid over the window
// (Ampify.ui.applePlayer). The label uses Apple Music's own French word for it.

Ampify.i18n.define({
  en: { label: "Now Playing" },
  fr: { label: "À l'écoute" },
});

Ampify.ui.playbar.addButton({
  label: Ampify.i18n.t("label"),
  icon: "",  // FullScreen, in Segoe Fluent Icons
  onClick: () => Ampify.ui.applePlayer.open(),
});
