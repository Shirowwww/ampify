// Now Playing: a button of the playback bar that opens Apple Music's own full-screen player, laid over the window
// (Ampify.ui.applePlayer). The label uses Apple Music's own word for it where there is one.

Ampify.i18n.define({
  en: { label: "Now Playing" },
  fr: { label: "À l'écoute" },
  de: { label: "Jetzt läuft" },
  es: { label: "Reproduciendo" },
  pt: { label: "Tocando agora" },
  ru: { label: "Сейчас играет" },
  ja: { label: "再生中" },
  zh: { label: "正在播放" },
  it: { label: "In riproduzione" },
  ko: { label: "지금 재생 중" },
});

Ampify.ui.playbar.addButton({
  label: Ampify.i18n.t("label"),
  icon: "",  // FullScreen, in Segoe Fluent Icons
  onClick: () => Ampify.ui.applePlayer.open(),
});
