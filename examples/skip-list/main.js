// Skips what you never want to hear, like Spicetify's Trash Bin and Auto Skip. "Never play this" in Apple Music's
// context menu lists the song, album, artist or playlist the menu is about; the playback bar button lists the song
// that plays and skips it. A listed name is skipped whenever a track's title, artist or album is exactly that name,
// ignoring case and accents. Three settings also skip live versions, remixes and intros, judged from the title.
// "Skip list" in Apple Music's main menu shows the list and removes entries. It lives in this extension's storage.
const { h, useState, useEffect } = Ampify.React;
const { Button, Text } = Ampify.components;

Ampify.i18n.define({
  en: {
    menu: "Never play this", button: "Skip and never play again", empty: "Nothing is skipped yet. Use \"Never play this\" in a menu.",
    remove: "Remove", more: "Showing the newest {shown} of {count}.", live: "live version", remix: "remix",
    filler: "intro or interlude", listed: "in your list", list: "Skip list",
  },
  fr: {
    menu: "Ne jamais jouer", button: "Passer et ne plus jamais jouer", empty: "Rien n'est ignoré pour l'instant. Utilisez « Ne jamais jouer » dans un menu.",
    remove: "Retirer", more: "Les {shown} plus récents sur {count}.", live: "version live", remix: "remix",
    filler: "intro ou interlude", listed: "dans votre liste", list: "Liste d'exclusion",
  },
  de: {
    menu: "Nie abspielen", button: "Überspringen und nie wieder abspielen", empty: "Noch wird nichts übersprungen. Nutzen Sie „Nie abspielen“ in einem Menü.",
    remove: "Entfernen", more: "Die neuesten {shown} von {count}.", live: "Live-Version", remix: "Remix",
    filler: "Intro oder Interlude", listed: "in Ihrer Liste", list: "Sperrliste",
  },
  es: {
    menu: "No reproducir nunca", button: "Saltar y no reproducir más", empty: "Todavía no se salta nada. Usa «No reproducir nunca» en un menú.",
    remove: "Quitar", more: "Mostrando los {shown} más recientes de {count}.", live: "versión en directo", remix: "remix",
    filler: "intro o interludio", listed: "en tu lista", list: "Lista de exclusión",
  },
  pt: {
    menu: "Nunca tocar", button: "Pular e nunca mais tocar", empty: "Nada é pulado ainda. Use «Nunca tocar» em um menu.",
    remove: "Remover", more: "Mostrando os {shown} mais recentes de {count}.", live: "versão ao vivo", remix: "remix",
    filler: "intro ou interlúdio", listed: "na sua lista", list: "Lista de exclusão",
  },
  ru: {
    menu: "Никогда не играть", button: "Пропустить и больше не играть", empty: "Пока ничего не пропускается. Используйте «Никогда не играть» в меню.",
    remove: "Убрать", more: "Показаны {shown} последних из {count}.", live: "концертная версия", remix: "ремикс",
    filler: "интро или интерлюдия", listed: "в вашем списке", list: "Список исключений",
  },
  ja: {
    menu: "今後再生しない", button: "スキップして今後再生しない", empty: "スキップするものはまだありません。メニューの「今後再生しない」を使ってください。",
    remove: "削除", more: "{count} 件中、新しい {shown} 件を表示しています。", live: "ライブ版", remix: "リミックス",
    filler: "イントロ／インタールード", listed: "リストにあります", list: "除外リスト",
  },
  zh: {
    menu: "永不播放", button: "跳过并不再播放", empty: "还没有跳过任何内容。请在菜单中使用“永不播放”。",
    remove: "移除", more: "显示最新的 {shown} 项，共 {count} 项。", live: "现场版", remix: "混音版",
    filler: "前奏或间奏", listed: "在你的列表中", list: "排除列表",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const MAX_ENTRIES = 500;
const MAX_NAME = 200;
const MAX_SHOWN = 80;  // the interface holds 300 nodes at most, and an entry takes three
const LIVE = /[([]\s*live\b|\s-\s+live\b/i;  // "(Live at X)", "[Live]", "- Live"; not "Live Forever"
const REMIX = /\bremix/i;
const FILLER = /\b(intro|interlude|skit)\b/i;

// Lower case, without accents or punctuation: "Beyoncé" ~ "beyonce". Letters of every script are kept.
function plain(text) {
  return String(text || "")
    .normalize("NFD")
    .replace(/\p{M}/gu, "")
    .toLowerCase()
    .replace(/[^\p{L}\p{N}]+/gu, " ")
    .trim();
}

// Storage: entries = [{ name, artist }], oldest first. With an artist, only that song matches; without, any title,
// artist or album of that name.
let entries = Ampify.storage.get("entries", []);
const watchers = new Set();

function save() {
  Ampify.storage.set("entries", entries);
  watchers.forEach((watcher) => watcher());
}

const sameEntry = (a, b) => plain(a.name) === plain(b.name) && plain(a.artist) === plain(b.artist);

function add(name, artist = "") {
  const entry = { name: String(name || "").trim().slice(0, MAX_NAME), artist: String(artist || "").trim().slice(0, MAX_NAME) };
  if (!plain(entry.name) || entries.some((known) => sameEntry(known, entry))) return false;
  entries = [...entries, entry].slice(-MAX_ENTRIES);
  save();
  return true;
}

function removeAt(index) {
  entries = entries.filter((entry, i) => i !== index);
  save();
}

function listed(track) {
  const title = plain(track.title);
  const artist = plain(track.artist);
  const album = plain(track.album);
  return entries.some((entry) => {
    const name = plain(entry.name);
    if (entry.artist) return name === title && plain(entry.artist) === artist;
    return name === title || name === artist || name === album;
  });
}

const options = {
  live: Ampify.config.get("skip-live", false),
  remix: Ampify.config.get("skip-remixes", false),
  filler: Ampify.config.get("skip-intros", false),
};

// Why the track is skipped, "" when it plays.
function reasonFor(track) {
  const title = String(track.title || "");
  if (listed(track)) return t("listed");
  if (options.live && LIVE.test(title)) return t("live");
  if (options.remix && REMIX.test(title)) return t("remix");
  if (options.filler && FILLER.test(title)) return t("filler");
  return "";
}

// One check per queue entry: if next() does not take, the same track is not asked again in a loop.
let checked = "";
const keyOf = (track) => track.queueItemId || track.id || track.title;

function check() {
  const track = Ampify.player.state().track;
  if (!track || keyOf(track) === checked) return;
  checked = keyOf(track);
  const reason = reasonFor(track);
  if (!reason) return;
  Ampify.log("skipped \"" + track.title + "\" by " + track.artist + " (" + reason + ")");
  Ampify.player.next();
}

Ampify.player.on("trackchange", check);
Ampify.player.on("statechange", check);

Ampify.ui.menu.addItem("context", {
  label: t("menu"),
  onClick: (item) => {
    if (add(item && item.title)) Ampify.log("listed \"" + item.title + "\"");
  },
});

// An icon: the bar has no room for the sentence, which shows as its tooltip.
Ampify.ui.playbar.addButton({
  label: t("button"),
  icon: "",  // Blocked, in Segoe Fluent Icons
  onClick: () => {
    const track = Ampify.player.state().track;
    if (!track || !track.title) return;
    add(track.title, track.artist);
    checked = keyOf(track);
    Ampify.log("listed \"" + track.title + "\" by " + track.artist);
    Ampify.player.next();
  },
});

// The panel, newest entry first.
function Panel() {
  const [, setStamp] = useState(0);
  useEffect(() => {
    const watcher = () => setStamp((n) => n + 1);
    watchers.add(watcher);
    return () => watchers.delete(watcher);
  }, []);

  if (entries.length === 0) return h("stack", null, h(Text, { wrap: true }, t("empty")));
  const shown = entries.map((entry, index) => ({ entry, index })).reverse().slice(0, MAX_SHOWN);
  return h("stack", null,
    ...shown.map(({ entry, index }) => h("row", { key: index + ":" + entry.name },
      h(Text, { grow: true, maxLines: 1 }, entry.artist ? entry.name + " · " + entry.artist : entry.name),
      h(Button, { label: t("remove"), variant: "tertiary", onClick: () => removeAt(index) }))),
    entries.length > MAX_SHOWN ? h(Text, { variant: "caption" }, t("more", { shown: MAX_SHOWN, count: entries.length })) : null);
}

// The list opens from Apple Music's main menu, never on its own: a panel would show at every start.
Ampify.ui.menu.addItem("app", { label: t("list"), onClick: () => Ampify.ReactDOM.render(h(Panel), "popup:skip-list") });
check();
