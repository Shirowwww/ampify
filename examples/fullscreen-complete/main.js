// Full screen, complete: a full-screen view of the playing song with its lyrics, its Up Next list and the transport
// buttons. Ctrl+Alt+F opens and closes it; inside the view F closes it, L and Q show its lyrics and Up Next panes.
// The controls fade after 3 s without pointer movement and come back on movement. Escape closes the view itself.

Ampify.i18n.define({
  en: { full: "Full screen", lyrics: "Lyrics", queue: "Up next", nothing: "Nothing playing", noLyrics: "No lyrics yet", noQueue: "Up Next is not loaded yet", queueEmpty: "Nothing after this song", play: "Play", pause: "Pause", next: "Next", previous: "Previous", after: "Then", hint: "F or Ctrl+Alt+F: close · L: lyrics · Q: up next" },
  fr: { full: "Plein écran", lyrics: "Paroles", queue: "À suivre", nothing: "Rien en lecture", noLyrics: "Pas encore de paroles", noQueue: "À suivre n'est pas encore chargé", queueEmpty: "Rien après ce morceau", play: "Lecture", pause: "Pause", next: "Suivant", previous: "Précédent", after: "Ensuite", hint: "F ou Ctrl+Alt+F : fermer · L : paroles · Q : à suivre" },
  de: { full: "Vollbild", lyrics: "Songtext", queue: "Als Nächstes", nothing: "Nichts läuft", noLyrics: "Noch kein Songtext", noQueue: "Als Nächstes ist noch nicht geladen", queueEmpty: "Danach kommt nichts", play: "Abspielen", pause: "Pause", next: "Weiter", previous: "Zurück", after: "Danach", hint: "F oder Strg+Alt+F: schließen · L: Songtext · Q: Als Nächstes" },
  es: { full: "Pantalla completa", lyrics: "Letra", queue: "A continuación", nothing: "No suena nada", noLyrics: "Aún no hay letra", noQueue: "A continuación aún no está cargado", queueEmpty: "Nada después de esta canción", play: "Reproducir", pause: "Pausa", next: "Siguiente", previous: "Anterior", after: "Después", hint: "F o Ctrl+Alt+F: cerrar · L: letra · Q: a continuación" },
  pt: { full: "Tela cheia", lyrics: "Letra", queue: "A seguir", nothing: "Nada a tocar", noLyrics: "Ainda sem letra", noQueue: "A seguir ainda não foi carregado", queueEmpty: "Nada depois desta música", play: "Tocar", pause: "Pausa", next: "Seguinte", previous: "Anterior", after: "Depois", hint: "F ou Ctrl+Alt+F: fechar · L: letra · Q: a seguir" },
  ru: { full: "Во весь экран", lyrics: "Текст", queue: "Далее", nothing: "Ничего не играет", noLyrics: "Текста пока нет", noQueue: "Список «Далее» ещё не загружен", queueEmpty: "После этого трека ничего нет", play: "Играть", pause: "Пауза", next: "Следующий", previous: "Предыдущий", after: "Потом", hint: "F или Ctrl+Alt+F: закрыть · L: текст · Q: далее" },
  ja: { full: "全画面", lyrics: "歌詞", queue: "次に再生", nothing: "再生していません", noLyrics: "歌詞はまだありません", noQueue: "次に再生はまだ読み込まれていません", queueEmpty: "この曲の後はありません", play: "再生", pause: "一時停止", next: "次へ", previous: "前へ", after: "その後", hint: "F / Ctrl+Alt+F: 閉じる · L: 歌詞 · Q: 次に再生" },
  zh: { full: "全屏", lyrics: "歌词", queue: "接下来", nothing: "没有正在播放", noLyrics: "暂无歌词", noQueue: "“接下来”尚未加载", queueEmpty: "这首之后没有歌曲", play: "播放", pause: "暂停", next: "下一首", previous: "上一首", after: "然后", hint: "F 或 Ctrl+Alt+F：关闭 · L：歌词 · Q：接下来" },
});

const LYRIC_LINES = 40;
const QUEUE_LINES = 20;
const IDLE_MS = 3000;
// view.last is the tree last sent: an identical tree is not sent again, so nothing is rebuilt when nothing changed.
const view = { open: false, lyrics: false, queue: false, idle: false, last: "", timer: null };

// The next song Apple Music will play: the first entry of Up Next that is still to come, or null.
function upNext(queue) {
  if (!queue || !Array.isArray(queue.items)) return null;
  return queue.items.find((item) => item.section === "next-added" || item.section === "next") || null;
}

// The lines of Apple's lyrics as plain text, or [] when there are none yet.
function lyricLines(lyrics) {
  if (!lyrics || !Array.isArray(lyrics.lines)) return [];
  return lyrics.lines.map((line) => line.text).filter((text) => text).slice(0, LYRIC_LINES);
}

function textNode(text, variant) {
  return { type: "text", text, variant, wrap: true };
}

function artworkNode(src, size) {
  if (src) return { type: "image", src, width: size, height: size, cornerRadius: 12, fit: "cover" };
  return textNode("", "body");
}

// Up Next is not loaded (or Apple has not built it yet) until the queue says it is complete.
function upNextEmptyText(queue) {
  if (!queue || !Array.isArray(queue.items) || queue.complete === false) return Ampify.i18n.t("noQueue");
  return Ampify.i18n.t("queueEmpty");
}

function upNextNode(queue) {
  const caption = textNode(Ampify.i18n.t("queue"), "caption");
  const item = upNext(queue);
  if (!item) return { type: "stack", gap: 4, children: [caption, textNode(upNextEmptyText(queue), "body")] };
  const subtitle = item.title + (item.artist ? " · " + item.artist : "");
  const card = { type: "card", title: Ampify.i18n.t("after"), subtitle, children: [] };
  // item.artwork is an image source (an address, or "artwork:<id>" drawn by Apple's loader), empty without a cover.
  if (item.artwork) card.children.push(artworkNode(item.artwork, 64));
  return { type: "stack", gap: 4, children: [caption, card] };
}

function panelNode(title, lines) {
  const children = [textNode(title, "subtitle")];
  for (const line of lines) children.push(textNode(line, "body"));
  return { type: "stack", gap: 6, children };
}

function transportNode(state) {
  const label = state.playing ? Ampify.i18n.t("pause") : Ampify.i18n.t("play");
  return {
    type: "row",
    gap: 8,
    children: [
      { type: "button", label: Ampify.i18n.t("previous"), variant: "tertiary", onClick: () => Ampify.player.previous() },
      { type: "button", label, variant: "primary", onClick: () => Ampify.player.toggle() },
      { type: "button", label: Ampify.i18n.t("next"), variant: "tertiary", onClick: () => Ampify.player.next() },
    ],
  };
}

// The buttons and the key hint fade out after IDLE_MS without pointer movement, and come back with it. Faded, they
// are inert: a click there reaches the view, not an invisible button.
function controlsNode(state) {
  return {
    type: "stack",
    gap: 8,
    opacity: view.idle ? 0 : 1,
    inert: view.idle,
    transition: "opacity 400ms",
    children: [transportNode(state), textNode(Ampify.i18n.t("hint"), "caption")],
  };
}

function songNode(state, panes) {
  const track = state.track;
  const title = track ? track.title : Ampify.i18n.t("nothing");
  const artist = track ? track.artist : "";
  return {
    type: "stack",
    gap: 12,
    children: [
      artworkNode(track ? track.artwork : "", 320),
      textNode(title, "title"),
      textNode(artist, "subtitle"),
      upNextNode(panes.queue),
      controlsNode(state),
    ],
  };
}

// The view as a tree of nodes, from what the player and its panes hold now.
function buildView(state, panes) {
  const columns = [songNode(state, panes)];
  if (panes.lyrics !== undefined && view.lyrics) {
    const lines = lyricLines(panes.lyrics);
    columns.push(panelNode(Ampify.i18n.t("lyrics"), lines.length ? lines : [Ampify.i18n.t("noLyrics")]));
  }
  if (view.queue) {
    const lines = panes.queue && Array.isArray(panes.queue.items) ? panes.queue.items.slice(0, QUEUE_LINES).map((item) => item.title) : [];
    columns.push(panelNode(Ampify.i18n.t("queue"), lines.length ? lines : [Ampify.i18n.t("noQueue")]));
  }
  return { type: "row", gap: 32, padding: 24, children: columns };
}

// The current song, its lyrics (when its pane is open) and the queue, as the view shows them.
function snapshot() {
  return {
    state: Ampify.player.state(),
    panes: { lyrics: view.lyrics ? Ampify.player.lyrics() : undefined, queue: Ampify.player.queue.list() },
  };
}

function refresh() {
  if (!view.open) return;
  const shot = snapshot();
  const tree = buildView(shot.state, shot.panes);
  const text = JSON.stringify(tree);
  if (text === view.last) return;
  view.last = text;
  Ampify.ui.fullscreen.open(tree);
}

function clearIdle() {
  if (view.timer !== null) clearTimeout(view.timer);
  view.timer = null;
}

function armIdle() {
  clearIdle();
  view.timer = setTimeout(() => {
    view.timer = null;
    view.idle = true;
    refresh();
  }, IDLE_MS);
}

// Each pointer move (passed at most every 250 ms by the kernel) brings the controls back and restarts the 3 s wait.
function pointerMoved() {
  if (!view.open) return;
  if (view.idle) {
    view.idle = false;
    refresh();
  }
  armIdle();
}

function show(on) {
  view.open = on;
  view.last = "";
  view.idle = false;
  if (on) {
    refresh();
    armIdle();
  } else {
    clearIdle();
    Ampify.ui.fullscreen.close();
  }
}

function toggleFullScreen() {
  show(!view.open);
}

function togglePane(name) {
  view[name] = !view[name];
  if (!view.open) show(true);
  else refresh();
}

// F, L and Q, while the view has the keyboard.
function keyPressed(key) {
  if (!view.open) return;
  if (key === "F") return toggleFullScreen();
  // A key counts as activity, like a pointer move: the controls come back and the idle wait restarts.
  pointerMoved();
  if (key === "L") togglePane("lyrics");
  else if (key === "Q") togglePane("queue");
}

Ampify.ui.fullscreen.onClose(() => {
  view.open = false;
  view.last = "";
  clearIdle();
});
Ampify.ui.fullscreen.onKey(keyPressed);
Ampify.ui.fullscreen.onPointer(pointerMoved);
Ampify.keyboard.bind("Ctrl+Alt+F", toggleFullScreen);
Ampify.keyboard.bind("Ctrl+Alt+L", () => togglePane("lyrics"));
Ampify.keyboard.bind("Ctrl+Alt+Q", () => togglePane("queue"));
Ampify.player.on("trackchange", refresh);
Ampify.player.on("lyricschange", refresh);
Ampify.player.on("statechange", refresh);
