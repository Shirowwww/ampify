// Skips what you never want to hear, like Spicetify's Trash Bin and Auto Skip. "Never play this" in Apple Music's
// context menu lists the song, album, artist or playlist the menu is about; the playback bar button lists the song
// that plays and skips it. A listed name is skipped whenever a track's title, artist or album is exactly that name,
// ignoring case and accents. Three settings also skip live versions, remixes and intros, judged from the title.
// A menu click also keeps the kind and catalog id Apple gives: an album entry then matches the album only, an artist
// entry the artist only, a song entry the playing song's catalog id. Other kinds (playlist, genre...) stay on the
// list but match nothing: a playing track does not say which playlist or genre it comes from. "Skip list" in Apple Music's main menu shows the
// list, removes entries, and exports or imports it as JSON. It lives in this extension's storage.
const { h, useState, useEffect } = Ampify.React;
const { Button, Text, TextField } = Ampify.components;

Ampify.i18n.define({
  en: {
    menu: "Never play this", button: "Skip and never play again", empty: "Nothing is skipped yet. Use \"Never play this\" in a menu.",
    remove: "Remove", more: "Showing the newest {shown} of {count}.", live: "live version", remix: "remix",
    filler: "intro or interlude", listed: "in your list", list: "Skip list",
    export: "Export", import: "Import", exportLabel: "Skip list as JSON", importLabel: "Skip list to import",
    exportHint: "Select the text, copy it, and paste it into Import on another PC.",
    importHint: "Paste a list exported from Skip list, then add it to yours.",
    part: "Part {part} of {parts}", previous: "Previous", next: "Next", done: "Done", merge: "Add to the list",
    added: "Added {added}, skipped {skipped}.", invalid: "This is not valid JSON.",
    notList: "The text must be a JSON list of entries.", tooLong: "This text is too long to import.",
  },
  fr: {
    menu: "Ne jamais jouer", button: "Passer et ne plus jamais jouer", empty: "Rien n'est ignoré pour l'instant. Utilisez « Ne jamais jouer » dans un menu.",
    remove: "Retirer", more: "Les {shown} plus récents sur {count}.", live: "version live", remix: "remix",
    filler: "intro ou interlude", listed: "dans votre liste", list: "Liste d'exclusion",
    export: "Exporter", import: "Importer", exportLabel: "Liste d'exclusion en JSON", importLabel: "Liste d'exclusion à importer",
    exportHint: "Sélectionnez le texte, copiez-le, puis collez-le dans Importer sur un autre PC.",
    importHint: "Collez une liste exportée depuis la Liste d'exclusion, puis ajoutez-la à la vôtre.",
    part: "Partie {part} sur {parts}", previous: "Précédent", next: "Suivant", done: "Terminé", merge: "Ajouter à la liste",
    added: "{added} ajoutées, {skipped} ignorées.", invalid: "Ce texte n'est pas du JSON valide.",
    notList: "Le texte doit être une liste JSON d'entrées.", tooLong: "Ce texte est trop long pour être importé.",
  },
  de: {
    menu: "Nie abspielen", button: "Überspringen und nie wieder abspielen", empty: "Noch wird nichts übersprungen. Nutzen Sie „Nie abspielen“ in einem Menü.",
    remove: "Entfernen", more: "Die neuesten {shown} von {count}.", live: "Live-Version", remix: "Remix",
    filler: "Intro oder Interlude", listed: "in Ihrer Liste", list: "Sperrliste",
    export: "Exportieren", import: "Importieren", exportLabel: "Sperrliste als JSON", importLabel: "Zu importierende Sperrliste",
    exportHint: "Text markieren, kopieren und auf einem anderen PC unter „Importieren“ einfügen.",
    importHint: "Eine aus der Sperrliste exportierte Liste einfügen und zur eigenen hinzufügen.",
    part: "Teil {part} von {parts}", previous: "Zurück", next: "Weiter", done: "Fertig", merge: "Zur Liste hinzufügen",
    added: "{added} hinzugefügt, {skipped} übersprungen.", invalid: "Das ist kein gültiges JSON.",
    notList: "Der Text muss eine JSON-Liste von Einträgen sein.", tooLong: "Dieser Text ist zu lang zum Importieren.",
  },
  es: {
    menu: "No reproducir nunca", button: "Saltar y no reproducir más", empty: "Todavía no se salta nada. Usa «No reproducir nunca» en un menú.",
    remove: "Quitar", more: "Mostrando los {shown} más recientes de {count}.", live: "versión en directo", remix: "remix",
    filler: "intro o interludio", listed: "en tu lista", list: "Lista de exclusión",
    export: "Exportar", import: "Importar", exportLabel: "Lista de exclusión en JSON", importLabel: "Lista de exclusión que importar",
    exportHint: "Selecciona el texto, cópialo y pégalo en Importar en otro PC.",
    importHint: "Pega una lista exportada desde la Lista de exclusión y añádela a la tuya.",
    part: "Parte {part} de {parts}", previous: "Anterior", next: "Siguiente", done: "Listo", merge: "Añadir a la lista",
    added: "{added} añadidos, {skipped} omitidos.", invalid: "Este texto no es un JSON válido.",
    notList: "El texto debe ser una lista JSON de entradas.", tooLong: "Este texto es demasiado largo para importarlo.",
  },
  pt: {
    menu: "Nunca tocar", button: "Pular e nunca mais tocar", empty: "Nada é pulado ainda. Use «Nunca tocar» em um menu.",
    remove: "Remover", more: "Mostrando os {shown} mais recentes de {count}.", live: "versão ao vivo", remix: "remix",
    filler: "intro ou interlúdio", listed: "na sua lista", list: "Lista de exclusão",
    export: "Exportar", import: "Importar", exportLabel: "Lista de exclusão em JSON", importLabel: "Lista de exclusão para importar",
    exportHint: "Selecione o texto, copie e cole em Importar em outro PC.",
    importHint: "Cole uma lista exportada da Lista de exclusão e adicione à sua.",
    part: "Parte {part} de {parts}", previous: "Anterior", next: "Próxima", done: "Concluído", merge: "Adicionar à lista",
    added: "{added} adicionados, {skipped} ignorados.", invalid: "Este texto não é um JSON válido.",
    notList: "O texto deve ser uma lista JSON de entradas.", tooLong: "Este texto é longo demais para importar.",
  },
  ru: {
    menu: "Никогда не играть", button: "Пропустить и больше не играть", empty: "Пока ничего не пропускается. Используйте «Никогда не играть» в меню.",
    remove: "Убрать", more: "Показаны {shown} последних из {count}.", live: "концертная версия", remix: "ремикс",
    filler: "интро или интерлюдия", listed: "в вашем списке", list: "Список исключений",
    export: "Экспорт", import: "Импорт", exportLabel: "Список исключений в JSON", importLabel: "Список исключений для импорта",
    exportHint: "Выделите текст, скопируйте его и вставьте в «Импорт» на другом компьютере.",
    importHint: "Вставьте список, экспортированный из «Списка исключений», и добавьте его к своему.",
    part: "Часть {part} из {parts}", previous: "Назад", next: "Далее", done: "Готово", merge: "Добавить в список",
    added: "Добавлено: {added}, пропущено: {skipped}.", invalid: "Это не корректный JSON.",
    notList: "Текст должен быть JSON-списком записей.", tooLong: "Этот текст слишком длинный для импорта.",
  },
  ja: {
    menu: "今後再生しない", button: "スキップして今後再生しない", empty: "スキップするものはまだありません。メニューの「今後再生しない」を使ってください。",
    remove: "削除", more: "{count} 件中、新しい {shown} 件を表示しています。", live: "ライブ版", remix: "リミックス",
    filler: "イントロ／インタールード", listed: "リストにあります", list: "除外リスト",
    export: "エクスポート", import: "インポート", exportLabel: "JSON 形式の除外リスト", importLabel: "インポートする除外リスト",
    exportHint: "テキストを選択してコピーし、別の PC の「インポート」に貼り付けてください。",
    importHint: "除外リストからエクスポートしたリストを貼り付けて、自分のリストに追加します。",
    part: "パート {part} / {parts}", previous: "前へ", next: "次へ", done: "完了", merge: "リストに追加",
    added: "{added} 件を追加、{skipped} 件をスキップしました。", invalid: "有効な JSON ではありません。",
    notList: "テキストはエントリの JSON リストである必要があります。", tooLong: "このテキストは長すぎてインポートできません。",
  },
  zh: {
    menu: "永不播放", button: "跳过并不再播放", empty: "还没有跳过任何内容。请在菜单中使用“永不播放”。",
    remove: "移除", more: "显示最新的 {shown} 项，共 {count} 项。", live: "现场版", remix: "混音版",
    filler: "前奏或间奏", listed: "在你的列表中", list: "排除列表",
    export: "导出", import: "导入", exportLabel: "JSON 格式的排除列表", importLabel: "要导入的排除列表",
    exportHint: "选中文本并复制，然后粘贴到另一台电脑的“导入”中。",
    importHint: "粘贴从排除列表导出的列表，并添加到你的列表中。",
    part: "第 {part} 部分，共 {parts} 部分", previous: "上一页", next: "下一页", done: "完成", merge: "添加到列表",
    added: "已添加 {added} 项，跳过 {skipped} 项。", invalid: "这不是有效的 JSON。",
    notList: "文本必须是条目的 JSON 列表。", tooLong: "此文本太长，无法导入。",
  },
  it: {
    menu: "Non riprodurre mai", button: "Salta e non riprodurre più", empty: "Non è stato escluso ancora nulla. Usa «Non riprodurre mai» in un menu.",
    remove: "Rimuovi", more: "Ultimi {shown} di {count}.", live: "versione live", remix: "remix",
    filler: "intro o interludio", listed: "nella tua lista", list: "Lista di esclusione",
    export: "Esporta", import: "Importa", exportLabel: "Lista di esclusione in JSON", importLabel: "Lista di esclusione da importare",
    exportHint: "Seleziona il testo, copialo e incollalo in Importa su un altro PC.",
    importHint: "Incolla una lista esportata da Lista di esclusione, poi aggiungila alla tua.",
    part: "Parte {part} di {parts}", previous: "Indietro", next: "Avanti", done: "Fatto", merge: "Aggiungi alla lista",
    added: "Aggiunti {added}, saltati {skipped}.", invalid: "Questo non è un JSON valido.",
    notList: "Il testo deve essere una lista JSON di voci.", tooLong: "Questo testo è troppo lungo da importare.",
  },
  ko: {
    menu: "다시는 재생하지 않기", button: "건너뛰고 다시는 재생하지 않기", empty: "아직 건너뛰는 항목이 없습니다. 메뉴의 \"다시는 재생하지 않기\"를 사용하세요.",
    remove: "삭제", more: "전체 {count}개 중 최신 {shown}개를 표시합니다.", live: "라이브 버전", remix: "리믹스",
    filler: "인트로 또는 인터루드", listed: "내 목록에 있음", list: "건너뛰기 목록",
    export: "내보내기", import: "가져오기", exportLabel: "JSON 형식의 건너뛰기 목록", importLabel: "가져올 건너뛰기 목록",
    exportHint: "텍스트를 선택해 복사한 다음 다른 PC의 \"가져오기\"에 붙여넣으세요.",
    importHint: "건너뛰기 목록에서 내보낸 목록을 붙여넣어 내 목록에 추가합니다.",
    part: "{parts}개 중 {part}번째", previous: "이전", next: "다음", done: "완료", merge: "목록에 추가",
    added: "{added}개를 추가하고 {skipped}개를 건너뛰었습니다.", invalid: "올바른 JSON이 아닙니다.",
    notList: "텍스트는 항목의 JSON 목록이어야 합니다.", tooLong: "이 텍스트는 너무 길어서 가져올 수 없습니다.",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const MAX_ENTRIES = 500;
const MAX_NAME = 200;
const MAX_SHOWN = 80;  // the interface holds 300 nodes at most, and an entry takes three
const KINDS = ["song", "album", "artist", "playlist", "composer", "genre", "station", "video", "concert", "container"];
const CATALOG_ID = /^\d{1,19}$/;
const PART_BYTES = 480;  // the interface refuses a text over 500 UTF-8 bytes, so an export comes in parts
const CUT_BYTES = 80;  // for an entry too long to fit one part: each name is cut so that its part stays valid JSON
const MAX_IMPORT_CHARS = 200000;
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

// Storage: entries = [{ name, artist, kind?, id? }], oldest first. Without a kind: with an artist only that song
// matches, without any title, artist or album of that name. kind "album" or "artist" matches that field only; kind
// "song" matches the playing song's catalog id (id) when that is known, else its title; other kinds match nothing.
let entries = Ampify.storage.get("entries", []);
const watchers = new Set();

function save() {
  Ampify.storage.set("entries", entries);
  watchers.forEach((watcher) => watcher());
}

const sameEntry = (a, b) => plain(a.name) === plain(b.name) && plain(a.artist) === plain(b.artist)
  && (a.kind || "") === (b.kind || "") && (a.id || "") === (b.id || "");

// A clean entry, or null: the name must be a text, an artist a text if given; an unknown kind or a bad id is dropped.
function makeEntry(name, artist, kind, id) {
  if (typeof name !== "string" || (artist !== undefined && typeof artist !== "string")) return null;
  const entry = { name: name.trim().slice(0, MAX_NAME), artist: (artist || "").trim().slice(0, MAX_NAME) };
  if (!plain(entry.name)) return null;
  const catalogId = typeof id === "number" ? String(id) : id;
  return {
    ...entry,
    ...(KINDS.includes(kind) ? { kind } : {}),
    ...(typeof catalogId === "string" && CATALOG_ID.test(catalogId) ? { id: catalogId } : {}),
  };
}

function add(name, artist = "", item = null) {
  const entry = makeEntry(String(name || ""), String(artist || ""), item && item.kind, item && item.id);
  if (!entry || entries.some((known) => sameEntry(known, entry))) return false;
  entries = [...entries, entry].slice(-MAX_ENTRIES);
  save();
  return true;
}

// Import: JSON text -> { error } or { added, skipped }. It never throws and keeps what the list already holds.
function parseList(text) {
  if (text.length > MAX_IMPORT_CHARS) return { error: "tooLong" };
  let data;
  try {
    data = JSON.parse(text);
  } catch {
    return { error: "invalid" };
  }
  return Array.isArray(data) ? { list: data } : { error: "notList" };
}

function mergeImport(text) {
  const parsed = parseList(String(text || ""));
  if (parsed.error) return parsed;
  let merged = entries;
  let added = 0;
  for (const raw of parsed.list) {
    const entry = raw && typeof raw === "object" && !Array.isArray(raw) ? makeEntry(raw.name, raw.artist, raw.kind, raw.id) : null;
    if (!entry || merged.length >= MAX_ENTRIES || merged.some((known) => sameEntry(known, entry))) continue;
    merged = [...merged, entry];
    added += 1;
  }
  if (added > 0) {
    entries = merged;
    save();
  }
  return { added, skipped: parsed.list.length - added };
}

// Export: the list as compact JSON, cut into arrays that each fit a text field and import on their own. The kernel
// counts UTF-8 bytes and the extension runtime has no TextEncoder, so the bytes are counted by code point.
function utf8Bytes(text) {
  let bytes = 0;
  for (const char of text) {
    const code = char.codePointAt(0);
    bytes += code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
  }
  return bytes;
}

// The text cut to at most `budget` bytes as it weighs inside JSON (a quote costs two), never inside a character.
function cutBytes(text, budget) {
  let used = 0;
  let out = "";
  for (const char of text) {
    used += utf8Bytes(JSON.stringify(char)) - 2;
    if (used > budget) break;
    out += char;
  }
  return out;
}

const partBytes = (list) => utf8Bytes(JSON.stringify(list));

function fitted(entry) {
  if (partBytes([entry]) <= PART_BYTES) return entry;
  return { ...entry, name: cutBytes(entry.name, CUT_BYTES), artist: cutBytes(entry.artist, CUT_BYTES) };
}

function exportParts() {
  const parts = [];
  let current = [];
  for (const entry of entries.map(fitted)) {
    if (current.length > 0 && partBytes([...current, entry]) > PART_BYTES) {
      parts.push(current);
      current = [];
    }
    current = [...current, entry];
  }
  return [...parts, current].map((part) => JSON.stringify(part));
}

function removeAt(index) {
  entries = entries.filter((entry, i) => i !== index);
  save();
}

// The playing song's catalog id from the queue's current item, which must be that song. "" when Apple gives none.
function playingCatalogId(track) {
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

function matches(entry, track, catalogId) {
  const name = plain(entry.name);
  const title = plain(track.title);
  const artist = plain(track.artist);
  if (entry.kind === "album") return name === plain(track.album);
  if (entry.kind === "artist") return name === artist;
  if (entry.kind === "song") {
    if (entry.id && catalogId) return entry.id === catalogId;
    return name === title && (!entry.artist || plain(entry.artist) === artist);
  }
  // Any other kind (playlist, genre, composer, station...): the track carries nothing to tell, so it never matches.
  if (entry.kind) return false;
  if (entry.artist) return name === title && plain(entry.artist) === artist;
  return name === title || name === artist || name === plain(track.album);
}

function listed(track) {
  const needsId = entries.some((entry) => entry.kind === "song" && entry.id);
  const catalogId = needsId ? playingCatalogId(track) : "";
  return entries.some((entry) => matches(entry, track, catalogId));
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
    if (add(item && item.title, "", item && item.item)) Ampify.log("listed \"" + item.title + "\"");
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

// The list, newest entry first.
function ListView() {
  if (entries.length === 0) return h(Text, { wrap: true }, t("empty"));
  const shown = entries.map((entry, index) => ({ entry, index })).reverse().slice(0, MAX_SHOWN);
  return h("stack", null,
    ...shown.map(({ entry, index }) => h("row", { key: index + ":" + entry.name },
      h(Text, { grow: true, maxLines: 1 }, entry.artist ? entry.name + " · " + entry.artist : entry.name),
      h(Button, { label: t("remove"), variant: "tertiary", onClick: () => removeAt(index) }))),
    entries.length > MAX_SHOWN ? h(Text, { variant: "caption" }, t("more", { shown: MAX_SHOWN, count: entries.length })) : null);
}

// The export, one part at a time: select the text and copy it.
function ExportView({ part, setPart, close }) {
  const parts = exportParts();
  const index = Math.min(part, parts.length - 1);
  return h("stack", null,
    h(Text, { wrap: true }, t("exportHint")),
    h(TextField, { key: "export-" + index, label: t("exportLabel"), value: parts[index] }),
    parts.length > 1 ? h(Text, { variant: "caption" }, t("part", { part: index + 1, parts: parts.length })) : null,
    h("row", null,
      index > 0 ? h(Button, { label: t("previous"), variant: "secondary", onClick: () => setPart(index - 1) }) : null,
      index < parts.length - 1 ? h(Button, { label: t("next"), variant: "secondary", onClick: () => setPart(index + 1) }) : null,
      h(Button, { label: t("done"), variant: "tertiary", onClick: close })));
}

// The pasted text stays out of the component's state: a text field never gets its value handed back, which the
// interface would refuse beyond 500 characters. A new key empties the field after an import.
let pasted = "";

function ImportView({ round, message, merge, close }) {
  return h("stack", null,
    h(Text, { wrap: true }, t("importHint")),
    h(TextField, { key: "import-" + round, label: t("importLabel"), placeholder: "[{\"name\": \"...\"}]", onChange: (text) => { pasted = String(text || ""); } }),
    h("row", null,
      h(Button, { label: t("merge"), variant: "primary", onClick: merge }),
      h(Button, { label: t("done"), variant: "tertiary", onClick: close })),
    message ? h(Text, { wrap: true }, message) : null);
}

function Panel() {
  const [, setStamp] = useState(0);
  const [mode, setMode] = useState("");
  const [part, setPart] = useState(0);
  const [round, setRound] = useState(0);
  const [message, setMessage] = useState("");
  useEffect(() => {
    const watcher = () => setStamp((n) => n + 1);
    watchers.add(watcher);
    return () => watchers.delete(watcher);
  }, []);

  const show = (next) => {
    pasted = "";
    setMessage("");
    setPart(0);
    setMode(next);
  };
  const merge = () => {
    const result = mergeImport(pasted);
    setMessage(result.error ? t(result.error) : t("added", result));
    if (result.error) return;
    pasted = "";
    setRound(round + 1);
  };
  const view = mode === "export" ? h(ExportView, { part, setPart, close: () => show("") })
    : mode === "import" ? h(ImportView, { round, message, merge, close: () => show("") })
    : h(ListView);
  return h("stack", null,
    h("row", null,
      h(Button, { label: t("export"), variant: "secondary", onClick: () => show("export") }),
      h(Button, { label: t("import"), variant: "secondary", onClick: () => show("import") })),
    view);
}

// The list opens from Apple Music's main menu, never on its own: a panel would show at every start.
Ampify.ui.menu.addItem("app", { label: t("list"), onClick: () => Ampify.ReactDOM.render(h(Panel), "popup:skip-list") });
check();
