// "Copy link" in the menu of a song, an album or a playlist, and Ctrl+Alt+C to copy what plays now with its time, such
// as "Song - Artist (1:23)". An item of the library without a catalog id gets its name copied instead of a link.
Ampify.i18n.define({
  en: { copy: "Copy link" },
  fr: { copy: "Copier le lien" },
  de: { copy: "Link kopieren" },
  es: { copy: "Copiar enlace" },
  pt: { copy: "Copiar link" },
  ru: { copy: "Копировать ссылку" },
  ja: { copy: "リンクをコピー" },
  zh: { copy: "复制链接" },
  it: { copy: "Copia link" },
  ko: { copy: "링크 복사" },
});

Ampify.ui.menu.addItem("context", {
  label: Ampify.i18n.t("copy"),
  kinds: ["song", "album", "playlist"],
  onClick: ({ title, item }) => {
    const link = item && item.id ? Ampify.uri.build(item.kind, item.id) : null;
    Ampify.clipboard.copy(link || title || "");
  },
});

Ampify.keyboard.bind("Ctrl+Alt+C", () => {
  const { track, positionMs } = Ampify.player.state();
  if (!track) return;
  Ampify.clipboard.copy(track.title + " - " + track.artist + " (" + Ampify.player.formatTime(positionMs) + ")");
});
