// Bookmarks, like Spicetify's bookmark.js. "Bookmark" in the context menu of a song, album, artist or playlist saves
// it; the playback bar button saves the song playing at its current position. "Bookmarks" in Apple Music's main menu
// lists them, newest first: Open goes back to a moment (or opens the item in Apple Music), Remove forgets it.
const { h, useState, useEffect } = Ampify.React;
const { Button, Text } = Ampify.components;

Ampify.i18n.define({
  en: {
    menu: "Bookmark", button: "Bookmark this moment", list: "Bookmarks", open: "Open", remove: "Remove",
    empty: "Nothing is bookmarked yet. Use \"Bookmark\" in a menu, or the button in the playback bar.",
    more: "Showing the newest {shown} of {count}.",
  },
  fr: {
    menu: "Ajouter un signet", button: "Marquer cet instant", list: "Signets", open: "Ouvrir", remove: "Retirer",
    empty: "Aucun signet pour l'instant. Utilisez « Ajouter un signet » dans un menu, ou le bouton de la barre de lecture.",
    more: "Les {shown} plus récents sur {count}.",
  },
  de: {
    menu: "Lesezeichen setzen", button: "Diesen Moment merken", list: "Lesezeichen", open: "Öffnen", remove: "Entfernen",
    empty: "Noch keine Lesezeichen. Nutzen Sie „Lesezeichen setzen“ in einem Menü oder die Schaltfläche in der Wiedergabeleiste.",
    more: "Die neuesten {shown} von {count}.",
  },
  es: {
    menu: "Añadir marcador", button: "Marcar este momento", list: "Marcadores", open: "Abrir", remove: "Quitar",
    empty: "Todavía no hay marcadores. Usa «Añadir marcador» en un menú o el botón de la barra de reproducción.",
    more: "Mostrando los {shown} más recientes de {count}.",
  },
  pt: {
    menu: "Adicionar marcador", button: "Marcar este momento", list: "Marcadores", open: "Abrir", remove: "Remover",
    empty: "Ainda não há marcadores. Use «Adicionar marcador» em um menu ou o botão da barra de reprodução.",
    more: "Mostrando os {shown} mais recentes de {count}.",
  },
  ru: {
    menu: "Добавить закладку", button: "Отметить этот момент", list: "Закладки", open: "Открыть", remove: "Убрать",
    empty: "Закладок пока нет. Используйте «Добавить закладку» в меню или кнопку на панели воспроизведения.",
    more: "Показаны {shown} последних из {count}.",
  },
  ja: {
    menu: "ブックマークに追加", button: "この瞬間をブックマーク", list: "ブックマーク", open: "開く", remove: "削除",
    empty: "ブックマークはまだありません。メニューの「ブックマークに追加」か、再生バーのボタンを使ってください。",
    more: "{count} 件中、新しい {shown} 件を表示しています。",
  },
  zh: {
    menu: "添加书签", button: "为此刻添加书签", list: "书签", open: "打开", remove: "移除",
    empty: "还没有书签。请在菜单中使用“添加书签”，或使用播放栏中的按钮。",
    more: "显示最新的 {shown} 项，共 {count} 项。",
  },
  it: {
    menu: "Aggiungi ai segnalibri", button: "Segna questo istante", list: "Segnalibri", open: "Apri", remove: "Rimuovi",
    empty: "Ancora nessun segnalibro. Usa «Aggiungi ai segnalibri» in un menu, o il pulsante nella barra di riproduzione.",
    more: "Ultimi {shown} di {count}.",
  },
  ko: {
    menu: "북마크에 추가", button: "이 시점을 북마크", list: "북마크", open: "열기", remove: "삭제",
    empty: "아직 북마크가 없습니다. 메뉴의 \"북마크에 추가\"나 재생 바의 버튼을 사용하세요.",
    more: "전체 {count}개 중 최신 {shown}개를 표시합니다.",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const MAX_BOOKMARKS = 200;
const MAX_NAME = 200;
const MAX_SHOWN = 60;  // the interface holds 300 nodes at most, and a bookmark takes four
const KINDS = ["song", "album", "artist", "playlist", "station"];  // what Ampify.uri.build can link to
const ITEM_ID = /^[A-Za-z0-9.-]{1,64}$/;  // catalog ids are digits, library playlists look like "pl.u-abc"
const CATALOG_ID = /^\d{1,19}$/;

// Lower case, without accents or punctuation, to tell whether two names are the same song.
const plain = (text) => String(text || "").normalize("NFD").replace(/\p{M}/gu, "").toLowerCase().replace(/[^\p{L}\p{N}]+/gu, " ").trim();
const cut = (text) => String(text || "").trim().slice(0, MAX_NAME);

// Storage: bookmarks = [{ title, added, kind?, id? }] for an item, [{ title, artist, positionMs, added, catalogId? }]
// for a moment, oldest first.
let bookmarks = Ampify.storage.get("bookmarks", []);
const watchers = new Set();

function save(next) {
  bookmarks = next.slice(-MAX_BOOKMARKS);
  Ampify.storage.set("bookmarks", bookmarks);
  watchers.forEach((watcher) => watcher());
}

const isMoment = (bookmark) => typeof bookmark.positionMs === "number";

function addItem(title, item) {
  const name = cut(title);
  if (!name) return false;
  const kind = item && KINDS.includes(item.kind) ? item.kind : "";
  const id = kind && item && typeof item.id === "string" && ITEM_ID.test(item.id) ? item.id : "";
  if (id && bookmarks.some((known) => known.kind === kind && known.id === id)) return false;
  save([...bookmarks, { title: name, ...(id ? { kind, id } : {}), added: Date.now() }]);
  return true;
}

// The playing song's catalog id from the queue's current item, which must be that song. "" when Apple gives none.
function playingCatalogId(track) {
  if (!track) return "";
  try {
    const queue = Ampify.player.queue.list();
    const items = queue && Array.isArray(queue.items) ? queue.items : [];
    const current = items.find((item) => item && item.section === "current");
    if (!current || plain(current.title) !== plain(track.title)) return "";
    return CATALOG_ID.test(String(current.catalogId || "")) ? String(current.catalogId) : "";
  } catch (error) {
    Ampify.log("no catalog id for \"" + track.title + "\": " + (error && error.message));
    return "";
  }
}

function addMoment() {
  const state = Ampify.player.state();
  const track = state.track;
  if (!track || !cut(track.title)) return;
  const catalogId = playingCatalogId(track);
  save([...bookmarks, {
    title: cut(track.title), artist: cut(track.artist), positionMs: Math.max(0, Math.round(state.positionMs || 0)),
    ...(catalogId ? { catalogId } : {}), added: Date.now(),
  }]);
}

// The storefront of the Windows region ("en-US" -> "us"); Apple Music redirects to the user's own.
function storefront() {
  const match = /[-_]([A-Za-z]{2})$/.exec(String(Ampify.locale || ""));
  return match ? match[1].toLowerCase() : "us";
}

const linkOf = (kind, id) => Ampify.uri.build(kind, id, { storefront: storefront() });

// Is this moment on the song playing now? By catalog id when both are known, else by title and artist.
function onNowPlaying(moment, track, catalogId) {
  if (!track) return false;
  if (moment.catalogId && catalogId) return moment.catalogId === catalogId;
  return plain(moment.title) === plain(track.title) && plain(moment.artist) === plain(track.artist);
}

function canOpen(bookmark, track, catalogId) {
  if (!isMoment(bookmark)) return Boolean(bookmark.kind && bookmark.id);
  return Boolean(bookmark.catalogId) || onNowPlaying(bookmark, track, catalogId);
}

function open(bookmark) {
  if (!isMoment(bookmark)) {
    if (bookmark.kind && bookmark.id) Ampify.http.open(linkOf(bookmark.kind, bookmark.id));
    return;
  }
  const track = Ampify.player.state().track;
  if (onNowPlaying(bookmark, track, playingCatalogId(track))) Ampify.player.seek(bookmark.positionMs);
  else if (bookmark.catalogId) Ampify.http.open(linkOf("song", bookmark.catalogId));
}

function removeAt(index) {
  save(bookmarks.filter((bookmark, i) => i !== index));
}

function clock(ms) {
  const seconds = Math.floor(ms / 1000);
  return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
}

function describe(bookmark) {
  const parts = [bookmark.title];
  if (bookmark.artist) parts.push(bookmark.artist);
  if (isMoment(bookmark)) parts.push(clock(bookmark.positionMs));
  return parts.join(" · ");
}

Ampify.ui.menu.addItem("context", {
  label: t("menu"),
  onClick: (click) => {
    if (addItem(click && click.title, click && click.item)) Ampify.log("bookmarked \"" + click.title + "\"");
  },
});

// An icon: the bar has no room for the sentence, which shows as its tooltip.
Ampify.ui.playbar.addButton({ label: t("button"), icon: "", onClick: addMoment });  // Bookmarks, in Segoe Fluent Icons

// The panel, newest first. It follows the song too, because Open means "seek" while the moment's song plays.
function Panel() {
  const [, setStamp] = useState(0);
  useEffect(() => {
    const watcher = () => setStamp((n) => n + 1);
    watchers.add(watcher);
    return () => watchers.delete(watcher);
  }, []);

  if (bookmarks.length === 0) return h("stack", null, h(Text, { wrap: true }, t("empty")));
  const track = Ampify.player.state().track;
  const catalogId = playingCatalogId(track);
  const shown = bookmarks.map((bookmark, index) => ({ bookmark, index })).reverse().slice(0, MAX_SHOWN);
  return h("stack", null,
    ...shown.map(({ bookmark, index }) => h("row", { key: index + ":" + bookmark.title },
      h(Text, { grow: true, maxLines: 1 }, describe(bookmark)),
      h(Button, { label: t("open"), variant: "secondary", disabled: !canOpen(bookmark, track, catalogId), onClick: () => open(bookmark) }),
      h(Button, { label: t("remove"), variant: "tertiary", onClick: () => removeAt(index) }))),
    bookmarks.length > MAX_SHOWN ? h(Text, { variant: "caption" }, t("more", { shown: MAX_SHOWN, count: bookmarks.length })) : null);
}

Ampify.player.on("trackchange", () => watchers.forEach((watcher) => watcher()));

// The list opens from Apple Music's main menu, never on its own: a panel would show at every start. It has the playback
// bar button's icon, as Apple's own entries in that menu have one.
Ampify.ui.menu.addItem("app", { label: t("list"), icon: "", onClick: () => Ampify.ReactDOM.render(h(Panel), "popup:bookmarks") });
